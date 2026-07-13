/*
 * ADV-Explorer - unit tests of the core library
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 *
 * Reference values were computed with numpy/pandas using the algorithms of
 * the tke-calculator predecessor code (flowstat.py, rmspike.py) on
 * tke-calculator-main/data/test-example/8_46.5_6_T3.vna.
 */
#include <QtTest>

#include "core/CsvReader.h"
#include "core/Despike.h"
#include "core/FlowStats.h"
#include "core/MeasurementPoint.h"
#include "core/ProfileStatsExport.h"
#include "core/Project.h"
#include "core/ProjectModel.h"
#include "core/Rotation.h"
#include "core/VnaReader.h"

using namespace adv;

namespace {
const QString kVnaFile = QStringLiteral(TEST_DATA_DIR "/8_46.5_6_T3.vna");
}

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    void vnaReader();
    void vnaCoordinatesFromFileName();
    void flowStatsParity();
    void goringNikoraParity();
    void qualityThresholdCounts();
    void gapFilling();
    void rotationZeroesMeans();
    void csvReader();
    void projectRoundTrip();
    void realDataPipeline();
};

void TestCore::vnaReader()
{
    QString error;
    const AdvData data = VnaReader::readFile(kVnaFile, &error);
    QVERIFY2(!data.isEmpty(), qPrintable(error));
    QCOMPARE(data.rowCount(), 23952);
    QCOMPARE(data.columnCount(), 18);

    QCOMPARE(data.columnByRole(Role::Time).first(), 47.455);
    QCOMPARE(data.columnByRole(Role::Sample).first(), 9491.0);
    QCOMPARE(data.columnByRole(Role::U).first(), 0.31);
    QCOMPARE(data.columnByRole(Role::V).first(), -0.089);
    QCOMPARE(data.columnByRole(Role::W1).first(), -0.013);
    QCOMPARE(data.columnByRole(Role::W2).first(), 0.0);
    QCOMPARE(data.columnByRole(Role::SnrX).first(), 23.5);
    QCOMPARE(data.columnByRole(Role::CorrZ2).first(), 80.0);

    QVERIFY(qAbs(data.samplingFrequency() - 200.0) < 0.5);
}

void TestCore::vnaCoordinatesFromFileName()
{
    double x, y, z;
    VnaReader::coordinatesFromFileName(QStringLiteral("8_46.5_6_T3.vna"), &x, &y, &z);
    QCOMPARE(x, 0.08);
    QCOMPARE(y, 0.465);
    QCOMPARE(z, 0.06);

    VnaReader::coordinatesFromFileName(QStringLiteral("__8_31.5_6_T3.vna"), &x, &y, &z);
    QCOMPARE(x, -0.08);
    QCOMPARE(y, 0.315);
    QCOMPARE(z, 0.06);
}

void TestCore::flowStatsParity()
{
    const AdvData data = VnaReader::readFile(kVnaFile);
    const PointStats s = flowstats::compute(data.columnByRole(Role::U),
                                            data.columnByRole(Role::V),
                                            data.columnByRole(Role::W1),
                                            data.samplingFrequency());
    // numpy nanmean/nanstd reference values
    QVERIFY(qAbs(s.u.mean - 0.327270749833) < 1e-9);
    QVERIFY(qAbs(s.u.std - 0.0831259279001) < 1e-9);
    QVERIFY(qAbs(s.u.stderror - 0.00053711293909) < 1e-12);
    QVERIFY(qAbs(s.v.mean - (-0.103364478958)) < 1e-9);
    QVERIFY(qAbs(s.v.std - 0.0434215003291) < 1e-9);
    QVERIFY(qAbs(s.w.mean - (-0.0615283901136)) < 1e-9);
    QVERIFY(qAbs(s.w.std - 0.143322253094) < 1e-9);
    QVERIFY(qAbs(s.uv - (-0.000808204465346)) < 1e-12);
    QVERIFY(qAbs(s.uw - 0.00302238492025) < 1e-12);
    QVERIFY(qAbs(s.tke - 0.0146683074061) < 1e-12);
    QVERIFY(qAbs(s.u.skewness - (-0.74348524484)) < 1e-8);
    QVERIFY(qAbs(s.u.kurtosis - 1.9100549488) < 1e-8);
    // dissipation must be positive and physically plausible for this record
    QVERIFY(s.eps > 0.0);
    QVERIFY(s.eps < 1.0);
}

void TestCore::goringNikoraParity()
{
    const AdvData data = VnaReader::readFile(kVnaFile);

    DespikeInput input;
    input.u = data.columnByRole(Role::U);
    input.v = data.columnByRole(Role::V);
    input.w1 = data.columnByRole(Role::W1);
    input.samplingFrequency = 200.0;

    DespikeConfig velocity;
    velocity.gnMethod = DespikeConfig::GnMethod::Velocity;
    velocity.gnK = 3.0;
    velocity.replace = DespikeConfig::Replace::NaN;
    const DespikeResult velResult = despike::apply(input, velocity);
    QCOMPARE(velResult.spikeCounts.value(QStringLiteral("GN velocity u")), 290);
    QCOMPARE(velResult.spikeCounts.value(QStringLiteral("GN velocity v")), 175);
    QCOMPARE(velResult.spikeCounts.value(QStringLiteral("GN velocity w1")), 306);

    DespikeConfig acceleration;
    acceleration.gnMethod = DespikeConfig::GnMethod::Acceleration;
    acceleration.gnLambdaA = 1.0;
    acceleration.replace = DespikeConfig::Replace::NaN;
    const DespikeResult accResult = despike::apply(input, acceleration);
    QCOMPARE(accResult.spikeCounts.value(QStringLiteral("GN acceleration u")), 5880);
    QCOMPARE(accResult.spikeCounts.value(QStringLiteral("GN acceleration v")), 149);
    QCOMPARE(accResult.spikeCounts.value(QStringLiteral("GN acceleration w1")), 12758);
}

void TestCore::qualityThresholdCounts()
{
    // correlation filter removes each velocity component of a low-quality row;
    // reference: 1767 rows with average correlation below 70
    MeasurementPoint point;
    point.data = VnaReader::readFile(kVnaFile);
    point.despike.corrEnabled = true;
    point.despike.corrThreshold = 70.0;
    point.despike.replace = DespikeConfig::Replace::NaN;

    const ProcessedSeries series = processPoint(point, RotationAngles());
    QVERIFY(series.isValid());
    // 4 velocity components affected per removed row (u, v, w1, w2)
    QCOMPARE(series.spikeCounts.value(QStringLiteral("correlation")), 4 * 1767);

    // SNR of this record is high; nothing must be removed at threshold 20
    MeasurementPoint snrPoint;
    snrPoint.data = point.data;
    snrPoint.despike.snrEnabled = true;
    snrPoint.despike.snrThreshold = 20.0;
    const ProcessedSeries snrSeries = processPoint(snrPoint, RotationAngles());
    QCOMPARE(snrSeries.spikeCounts.value(QStringLiteral("SNR")), 0);
}

void TestCore::gapFilling()
{
    QVector<double> x = {nan(), 1.0, nan(), nan(), 4.0, nan()};
    const int filled = despike::fillGapsLinear(&x);
    QCOMPARE(filled, 4);
    QCOMPARE(x, (QVector<double>{1.0, 1.0, 2.0, 3.0, 4.0, 4.0}));
}

void TestCore::rotationZeroesMeans()
{
    const AdvData data = VnaReader::readFile(kVnaFile);
    QVector<double> u = data.columnByRole(Role::U);
    QVector<double> v = data.columnByRole(Role::V);
    QVector<double> w = data.columnByRole(Role::W1);

    const PointStats before = flowstats::compute(u, v, w, 200.0);
    const RotationAngles angles = rotation::propose(
        before.u.mean, before.v.mean, before.w.mean,
        before.vw, before.v.std * before.v.std, before.w.std * before.w.std);
    rotation::apply(angles, &u, &v, &w);
    const PointStats after = flowstats::compute(u, v, w, 200.0);

    // corrected mean transverse and vertical velocities vanish
    QVERIFY(qAbs(after.v.mean) < 1e-10);
    QVERIFY(qAbs(after.w.mean) < 1e-10);
    // velocity magnitude is preserved by the rotations
    QVERIFY(qAbs(after.magnitude - before.magnitude) < 1e-9);
    QVERIFY(after.u.mean > before.u.mean - 1e-9);
}

void TestCore::csvReader()
{
    const QByteArray csv =
        "time;vel_x;vel_y;vel_z;quality\n"
        "0.0;0.30;-0.09;-0.01;95\n"
        "0.1;0.32;-0.11;-0.02;96\n"
        "0.2;0.31;-0.10;-0.015;94\n";

    const CsvReader::Preview preview = CsvReader::preview(csv);
    QCOMPARE(preview.delimiter, QChar(';'));
    QVERIFY(preview.hasHeader);
    QCOMPARE(preview.columnCount, 5);

    const QHash<Role, int> mapping = CsvReader::guessMapping(preview.columnNames);
    QCOMPARE(mapping.value(Role::Time), 0);
    QCOMPARE(mapping.value(Role::U), 1);
    QCOMPARE(mapping.value(Role::V), 2);
    QCOMPARE(mapping.value(Role::W1), 3);

    QString error;
    const AdvData data = CsvReader::read(csv, mapping, &error);
    QVERIFY2(!data.isEmpty(), qPrintable(error));
    QCOMPARE(data.rowCount(), 3);
    QCOMPARE(data.columnByRole(Role::U).at(1), 0.32);
    QCOMPARE(data.columnByRole(Role::Time).at(2), 0.2);
    QVERIFY(qAbs(data.samplingFrequency() - 10.0) < 1e-9);
    QVERIFY(!data.timeSynthesized());

    // file without time column (e.g. plain u v w tables): time is synthesized
    // from the sample index and rescaled with the user-provided frequency
    // instrument export with free-text header block and CRLF line endings
    // (as produced by Nortek .adv-to-.dat conversions)
    const QByteArray headerBlock =
        "ADV File  : C:\\Users\\lab\\Desktop\\converted\\z0086.adv\r\n"
        "ADV PROBE : 1\r\n"
        "ADV VELOCITIES (U, V, W):\r\n"
        "11.60 -1.00 -1.52\r\n"
        "24.45 -3.48 -0.68\r\n";
    const CsvReader::Preview headerPreview = CsvReader::preview(headerBlock);
    QCOMPARE(headerPreview.headerLines, 3);
    QCOMPARE(headerPreview.columnCount, 3);
    QHash<Role, int> headerMapping;
    headerMapping.insert(Role::U, 0);
    headerMapping.insert(Role::V, 1);
    headerMapping.insert(Role::W1, 2);
    const AdvData headerData = CsvReader::read(headerBlock, headerMapping, &error);
    QVERIFY2(!headerData.isEmpty(), qPrintable(error));
    QCOMPARE(headerData.rowCount(), 2);
    QCOMPARE(headerData.columnByRole(Role::U).first(), 11.60);
    QCOMPARE(headerData.columnByRole(Role::W1).at(1), -0.68);

    const QByteArray noTime = "0.30 -0.09 -0.01\n0.32 -0.11 -0.02\n0.31 -0.10 -0.015\n";
    QHash<Role, int> uvwMapping;
    uvwMapping.insert(Role::U, 0);
    uvwMapping.insert(Role::V, 1);
    uvwMapping.insert(Role::W1, 2);
    AdvData synth = CsvReader::read(noTime, uvwMapping, &error);
    QVERIFY2(!synth.isEmpty(), qPrintable(error));
    QVERIFY(synth.timeSynthesized());
    synth.synthesizeTime(200.0);
    QCOMPARE(synth.samplingFrequency(), 200.0);
    QVERIFY(qAbs(synth.columnByRole(Role::Time).at(1) - 0.005) < 1e-12);
}

void TestCore::projectRoundTrip()
{
    ProjectModel model;
    MeasurementPoint point;
    point.x = 0.08;
    point.y = 0.465;
    point.z = 0.06;
    point.waterDepth = 0.25;
    point.tStart = 50.0;
    point.tEnd = 100.0;
    point.despike.corrEnabled = true;
    point.despike.gnMethod = DespikeConfig::GnMethod::Velocity;
    point.data = VnaReader::readFile(kVnaFile);
    const QUuid id = model.addPoint(point);

    RotationAngles angles;
    angles.heading = 0.05;
    angles.pitch = -0.01;
    model.setCorrection(MeasurementPoint::makeXyKey(0.08, 0.465), angles);

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("test.advProj"));
    QString error;
    QVERIFY2(project::save(model, path, &error), qPrintable(error));

    ProjectModel restored;
    QVERIFY2(project::load(&restored, path, &error), qPrintable(error));
    QCOMPARE(restored.points().size(), 1);
    const MeasurementPoint &r = restored.points().first();
    QCOMPARE(r.id, id);
    QCOMPARE(r.x, 0.08);
    QCOMPARE(r.waterDepth, 0.25);
    QCOMPARE(r.tStart, 50.0);
    QCOMPARE(r.tEnd, 100.0);
    QVERIFY(r.despike.corrEnabled);
    QCOMPARE(int(r.despike.gnMethod), int(DespikeConfig::GnMethod::Velocity));
    QCOMPARE(r.data.rowCount(), 23952);
    QCOMPARE(r.data.sourceFileName(), QStringLiteral("8_46.5_6_T3.vna"));

    const RotationAngles restoredAngles =
        restored.correction(MeasurementPoint::makeXyKey(0.08, 0.465));
    QCOMPARE(restoredAngles.heading, 0.05);
    QCOMPARE(restoredAngles.pitch, -0.01);

    // processing works on the restored, embedded data
    const auto series = restored.processed(id);
    QVERIFY(series && series->isValid());
    QVERIFY(series->time.first() >= 50.0);
    QVERIFY(series->time.last() <= 100.0);
}

void TestCore::realDataPipeline()
{
    // end-to-end pipeline over user-provided data (u v w tables without a
    // time column); skipped when no such data is present
    const QString dataFile = QStringLiteral(REPO_DIR "/input-data/vel1.dat");
    if (!QFile::exists(dataFile))
        QSKIP("no real measurement data in input-data/");

    QHash<Role, int> mapping;
    mapping.insert(Role::U, 0);
    mapping.insert(Role::V, 1);
    mapping.insert(Role::W1, 2);
    QString error;
    AdvData data = CsvReader::readFile(dataFile, mapping, &error);
    QVERIFY2(!data.isEmpty(), qPrintable(error));
    QVERIFY(data.timeSynthesized());
    data.synthesizeTime(200.0);

    ProjectModel model;
    MeasurementPoint point;
    point.x = 0.5;
    point.y = 0.0;
    point.z = 0.05;
    point.waterDepth = 0.30;
    point.despike.velEnabled = true;
    point.despike.velK = 3.0;
    point.data = data;
    const QUuid id = model.addPoint(point);

    const auto series = model.processed(id);
    QVERIFY(series && series->isValid());
    QVERIFY(std::isfinite(series->stats.u.mean));
    QVERIFY(series->stats.tke > 0.0);

    QTemporaryDir dir;
    const QString statsPath = dir.filePath(QStringLiteral("points.xlsx"));
    QVERIFY2(statsexport::writePointStats(model, statsPath, &error), qPrintable(error));
    QVERIFY(QFileInfo(statsPath).size() > 0);

    const QString profilePath = dir.filePath(QStringLiteral("profiles.xlsx"));
    QVERIFY2(statsexport::fillProfileTemplate(
                 model, QStringLiteral(REPO_DIR "/templates/ADV-profiles.xlsx"),
                 profilePath, &error),
             qPrintable(error));
    QVERIFY(QFileInfo(profilePath).size() > 0);
}

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
