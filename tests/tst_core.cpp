/*
 * ADV-Explorer - unit tests of the core library
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 *
 * Reference values were computed with numpy/pandas using the algorithms of
 * the tke-calculator predecessor code (flowstat.py, rmspike.py,
 * https://tke-calculator.readthedocs.io/) on tests/data/8_46.5_6_T3.vna.
 */
#include <QtTest>

#include "core/Crs.h"
#include "core/CsvReader.h"
#include "core/Despike.h"
#include "core/FlowStats.h"
#include "core/FlowTrackerCsvReader.h"
#include "core/FlowTrackerReader.h"
#include "core/FormatRegistry.h"
#include "core/GeoPointImport.h"
#include "core/MeasurementPoint.h"
#include "core/ProfileStatsExport.h"
#include "core/Project.h"
#include "core/ProjectModel.h"
#include "core/Rotation.h"
#include "core/StatsLabels.h"
#include "core/VnaReader.h"
#include "core/ZipArchive.h"

using namespace adv;

namespace {
const QString kVnaFile = QStringLiteral(TEST_DATA_DIR "/8_46.5_6_T3.vna");
/// SonTek FlowTracker2 survey of the Isar side channel, 30 September 2025:
/// 8 stations (2 of them banks), 14 point measurements, 60 samples each at 2 Hz.
const QString kFtFile = QStringLiteral(TEST_DATA_DIR "/flowtracker.ft");

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
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
    void zipArchiveReadsFlowTrackerZip64();
    void flowTrackerSurveyStructure();
    void flowTrackerParityAgainstInstrument();
    void flowTrackerCanonicalRoundTrip();
    void formatRegistryDispatch();
    void flowTrackerCsvFallbackMatchesFt();
    void modeLabelsAndEps();
    void projectFieldModeRoundTrip();
    void projectRejectsFutureFormatVersion();
    void geoPackageImport();
    void crossSectionPlacement();
    void crsLookup();
    void crsReference();
    void crsRoundTrip();
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

void TestCore::zipArchiveReadsFlowTrackerZip64()
{
    // FlowTracker2 writes central-directory entries with 0xFFFFFFFF sizes and a
    // ZIP64 extra field, but no end-of-central-directory-64 record. This test is
    // the gate for the vendored unpacker: a reader that ignores extra fields
    // reports 4 GiB entries instead of failing loudly.
    ZipArchive archive;
    QString error;
    QVERIFY2(archive.open(readAll(kFtFile), &error), qPrintable(error));

    const QStringList names = archive.entryNames();
    QCOMPARE(names.size(), 16);
    QVERIFY(names.contains(QStringLiteral("DataFile.json")));
    QVERIFY(archive.contains(QStringLiteral("DataFile.json")));

    const QByteArray dataFile = archive.read(QStringLiteral("DataFile.json"), &error);
    QVERIFY2(!dataFile.isEmpty(), qPrintable(error));
    QCOMPARE(dataFile.size(), 60846);
    QVERIFY(QJsonDocument::fromJson(dataFile).isObject());

    // every point measurement entry must decompress as well
    int pointMeasurements = 0;
    for (const QString &name : names) {
        if (!name.startsWith(QStringLiteral("PointMeasurements/")))
            continue;
        ++pointMeasurements;
        QVERIFY2(!archive.read(name, &error).isEmpty(), qPrintable(name + ": " + error));
    }
    QCOMPARE(pointMeasurements, 14);

    // a missing entry reports an error rather than returning silently
    QVERIFY(archive.read(QStringLiteral("nope.json"), &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("nope.json")));

    // and a non-archive is rejected
    ZipArchive bogus;
    QVERIFY(!bogus.open(QByteArray("not a zip at all"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(bogus.read(QStringLiteral("x"), &error).isEmpty());
}

void TestCore::flowTrackerSurveyStructure()
{
    FtSurvey survey;
    QString error;
    QVERIFY2(FlowTrackerReader::readFile(kFtFile, &survey, &error), qPrintable(error));

    QCOMPARE(survey.siteName, QStringLiteral("Ft-side-channel"));
    QCOMPARE(survey.samplingFrequency, 2.0);
    QCOMPARE(survey.averagingSeconds, 30.0);
    QCOMPARE(survey.snrThresholdDb, 10.0);
    QCOMPARE(survey.stations.size(), 8);
    QCOMPARE(survey.pointCount(), 14);

    // the banks bracket the cross section and hold no velocity data
    const FtStation &left = survey.stations.first();
    QCOMPARE(left.stationType, QStringLiteral("LeftBank"));
    QCOMPARE(left.location, 0.0);
    QVERIFY(left.points.isEmpty());
    QVERIFY(left.isBank());

    const FtStation &right = survey.stations.last();
    QCOMPARE(right.stationType, QStringLiteral("RightBank"));
    QCOMPARE(right.location, 4.2);
    QVERIFY(right.isBank());

    const FtStation &station = survey.stations.at(1);
    QVERIFY(!station.isBank());
    QCOMPARE(station.location, 0.5);
    QCOMPARE(station.depth, 0.326);
    QCOMPARE(station.points.size(), 3);
    QVERIFY(station.hasGps);

    const FtPoint &point = station.points.first();
    QCOMPARE(point.fractionalDepth, 0.2);
    // z is measured up from the bed, so 0.2 of the depth below the surface
    QVERIFY(std::fabs(point.distanceFromBottom - 0.8 * 0.326) < 1e-6);
    QCOMPARE(point.instrumentSpikes, QVector<int>({35, 43}));

    const AdvData &data = point.data;
    QCOMPARE(data.rowCount(), 60);
    QCOMPARE(data.samplingFrequency(), 2.0);
    QCOMPARE(data.format(), QStringLiteral("ft"));
    QVERIFY(!data.timeSynthesized());
    QCOMPARE(data.columnByRole(Role::Time).first(), 0.0);
    QCOMPARE(data.columnByRole(Role::Time).last(), 29.5);

    // a three-beam probe has one vertical component and no second-beam quality
    QVERIFY(data.hasRole(Role::U));
    QVERIFY(data.hasRole(Role::W1));
    QVERIFY(!data.hasRole(Role::W2));
    QVERIFY(data.hasRole(Role::CorrZ1));
    QVERIFY(!data.hasRole(Role::CorrZ2));
    QVERIFY(!data.hasRole(Role::SnrZ2));

    // correlation is rescaled from the instrument's 0..1 score to a percentage
    const QVector<double> &corr = data.columnByRole(Role::CorrX);
    QCOMPARE(corr.size(), 60);
    QVERIFY(std::fabs(corr.first() - 25.0) < 1e-6);

    // the flagged samples are blanked in the velocity columns only
    QVERIFY(std::isnan(data.columnByRole(Role::U).at(35)));
    QVERIFY(std::isnan(data.columnByRole(Role::V).at(43)));
    QVERIFY(std::isfinite(data.columnByRole(Role::SnrX).at(35)));
}

void TestCore::flowTrackerParityAgainstInstrument()
{
    // Each point measurement carries the instrument's own despiked statistics in
    // DataFile.json (SampleStatistics.DespikedVelocity). Reproducing them proves
    // the sample extraction, the spike mask and the ddof=0 convention of
    // flowstats::seriesStats all agree with the FlowTracker2 firmware.
    FtSurvey survey;
    QString error;
    QVERIFY2(FlowTrackerReader::readFile(kFtFile, &survey, &error), qPrintable(error));

    int checked = 0;
    for (const FtStation &station : survey.stations) {
        for (const FtPoint &point : station.points) {
            if (!std::isfinite(point.referenceMeanU))
                continue;
            const SeriesStats stats =
                flowstats::seriesStats(point.data.columnByRole(Role::U));

            QCOMPARE(stats.n, point.referenceCount);
            QVERIFY2(std::fabs(stats.mean - point.referenceMeanU) < 1e-9,
                     qPrintable(QStringLiteral("station %1 point %2: mean %3 != %4")
                                    .arg(station.index).arg(point.pointIndex)
                                    .arg(stats.mean, 0, 'g', 17)
                                    .arg(point.referenceMeanU, 0, 'g', 17)));
            // the firmware reports a sample standard deviation while
            // SeriesStats::std is the population one, so convert before
            // comparing rather than changing the application's convention
            const double sampleStd = stats.std * std::sqrt(double(stats.n) / (stats.n - 1));
            QVERIFY2(std::fabs(sampleStd - point.referenceSampleStdU) < 1e-9,
                     qPrintable(QStringLiteral("station %1 point %2: std %3 != %4")
                                    .arg(station.index).arg(point.pointIndex)
                                    .arg(sampleStd, 0, 'g', 17)
                                    .arg(point.referenceSampleStdU, 0, 'g', 17)));
            ++checked;
        }
    }
    QCOMPARE(checked, 14);

    // spot check against the values printed in the exported summary csv, which
    // is what a user would compare against by hand
    const FtStation &station = survey.stations.at(1);
    const double expectedMean[] = {0.278, 0.307, 0.251};
    const double expectedStdErr[] = {0.022, 0.023, 0.015};
    const int expectedCount[] = {58, 58, 55};
    for (int i = 0; i < 3; ++i) {
        const SeriesStats stats =
            flowstats::seriesStats(station.points.at(i).data.columnByRole(Role::U));
        QCOMPARE(stats.n, expectedCount[i]);
        QVERIFY(std::fabs(stats.mean - expectedMean[i]) < 5e-4);
        QVERIFY(std::fabs(stats.stderror - expectedStdErr[i]) < 5e-4);
    }
}

void TestCore::flowTrackerCanonicalRoundTrip()
{
    // A project file stores the extracted series, not the .ft archive, so the
    // canonical bytes must reload through CsvReader without losing a role, a
    // NaN, or precision.
    FtSurvey survey;
    QString error;
    QVERIFY2(FlowTrackerReader::readFile(kFtFile, &survey, &error), qPrintable(error));

    const AdvData &original = survey.stations.at(1).points.first().data;
    const AdvData restored = CsvReader::read(original.rawBytes(),
                                             FlowTrackerReader::canonicalMapping(original),
                                             &error);
    QVERIFY2(!restored.isEmpty(), qPrintable(error));

    QCOMPARE(restored.rowCount(), original.rowCount());
    QCOMPARE(restored.columnCount(), original.columnCount());
    QCOMPARE(restored.columnNames(), original.columnNames());
    QVERIFY(!restored.timeSynthesized());
    QVERIFY(std::fabs(restored.samplingFrequency() - 2.0) < 1e-9);

    for (int c = 0; c < original.columnCount(); ++c) {
        const QVector<double> &a = original.column(c);
        const QVector<double> &b = restored.column(c);
        QCOMPARE(b.size(), a.size());
        for (int i = 0; i < a.size(); ++i) {
            if (std::isnan(a.at(i))) {
                QVERIFY2(std::isnan(b.at(i)),
                         qPrintable(QStringLiteral("column %1 row %2 lost its NaN")
                                        .arg(original.columnNames().at(c)).arg(i)));
            } else {
                QVERIFY2(std::fabs(a.at(i) - b.at(i)) <= 1e-9 * std::fabs(a.at(i)),
                         qPrintable(QStringLiteral("column %1 row %2: %3 != %4")
                                        .arg(original.columnNames().at(c)).arg(i)
                                        .arg(a.at(i), 0, 'g', 17).arg(b.at(i), 0, 'g', 17)));
            }
        }
    }

    // the statistics of the restored series are identical, which is the property
    // that actually matters after a save and reload
    QCOMPARE(flowstats::seriesStats(restored.columnByRole(Role::U)).mean,
             flowstats::seriesStats(original.columnByRole(Role::U)).mean);

    // the canonical form stays small: this is why the whole archive is not embedded
    QVERIFY(original.rawBytes().size() < 8000);
}

void TestCore::formatRegistryDispatch()
{
    // The trap this registry exists for: QFileInfo("x.ft.dat.csv").suffix() is
    // "csv", so plain suffix dispatch hands a FlowTracker2 raw export to the
    // generic CSV reader, which then parses German headers and decimal commas
    // into nonsense instead of failing.
    QVERIFY(formats::byFilePath(QStringLiteral("survey.ft.dat.csv")) != nullptr);
    QCOMPARE(formats::byFilePath(QStringLiteral("survey.ft.dat.csv"))->id,
             QStringLiteral("ftcsv"));
    QCOMPARE(formats::byFilePath(QStringLiteral("survey.ft.sum.csv"))->id,
             QStringLiteral("ftcsv"));
    QCOMPARE(formats::byFilePath(QStringLiteral("/some/dir/survey.ft"))->id,
             QStringLiteral("ft"));
    QCOMPARE(formats::byFilePath(QStringLiteral("plain.csv"))->id, QStringLiteral("csv"));
    QCOMPARE(formats::byFilePath(QStringLiteral("a.vna"))->id, QStringLiteral("vna"));
    QCOMPARE(formats::byFilePath(QStringLiteral("vel1.dat"))->id, QStringLiteral("csv"));
    QCOMPARE(formats::byFilePath(QStringLiteral("notes.md")), nullptr);

    // case is irrelevant on the file system this may be read from
    QCOMPARE(formats::byFilePath(QStringLiteral("SURVEY.FT"))->id, QStringLiteral("ft"));

    // format ids are persisted in .advProj, so they must never go missing
    for (const QString &id : {QStringLiteral("vna"), QStringLiteral("csv"),
                              QStringLiteral("ft"), QStringLiteral("ftcsv")}) {
        const FileFormat *format = formats::byId(id);
        QVERIFY2(format != nullptr, qPrintable(id));
        QVERIFY(static_cast<bool>(format->read));
    }
    QCOMPARE(formats::byId(QStringLiteral("nope")), nullptr);

    // only the generic text format asks the user to map columns, and only the
    // FlowTracker formats hold more than one point per file
    QVERIFY(formats::byId(QStringLiteral("csv"))->needsColumnMapping);
    QVERIFY(!formats::byId(QStringLiteral("vna"))->needsColumnMapping);
    QVERIFY(!formats::byId(QStringLiteral("ft"))->needsColumnMapping);
    QVERIFY(formats::byId(QStringLiteral("ft"))->multiPoint);
    QVERIFY(!formats::byId(QStringLiteral("csv"))->multiPoint);

    QVERIFY(formats::openFileFilter().contains(QStringLiteral("*.ft.dat.csv")));
    QVERIFY(formats::openFileFilter().contains(QStringLiteral("*.vna")));
}

void TestCore::flowTrackerCsvFallbackMatchesFt()
{
    FtSurvey fromFt;
    FtSurvey fromCsv;
    QString error;
    QVERIFY2(FlowTrackerReader::readFile(kFtFile, &fromFt, &error), qPrintable(error));
    QVERIFY2(FlowTrackerCsvReader::readFile(
                 QStringLiteral(TEST_DATA_DIR "/flowtracker.ft.dat.csv"), &fromCsv, &error),
             qPrintable(error));

    QCOMPARE(FlowTrackerCsvReader::summaryPathFor(QStringLiteral("a/b.ft.dat.csv")),
             QStringLiteral("a/b.ft.sum.csv"));
    QVERIFY(FlowTrackerCsvReader::summaryPathFor(QStringLiteral("b.vna")).isEmpty());
    // an upper-case export must keep its case, or the sibling is unopenable on
    // a case-sensitive file system
    QCOMPARE(FlowTrackerCsvReader::summaryPathFor(QStringLiteral("a/B.FT.DAT.CSV")),
             QStringLiteral("a/B.FT.SUM.CSV"));
    // picking the summary half resolves to the same pair
    QCOMPARE(FlowTrackerCsvReader::summaryPathFor(QStringLiteral("a/b.ft.sum.csv")),
             QStringLiteral("a/b.ft.sum.csv"));

    // and the pair really opens when it is named in upper case, which is the
    // case a lower-cased sibling name used to break on Linux
    {
        QTemporaryDir upper;
        QVERIFY(upper.isValid());
        QVERIFY(QFile::copy(QStringLiteral(TEST_DATA_DIR "/flowtracker.ft.dat.csv"),
                            upper.filePath(QStringLiteral("SURVEY.FT.DAT.CSV"))));
        QVERIFY(QFile::copy(QStringLiteral(TEST_DATA_DIR "/flowtracker.ft.sum.csv"),
                            upper.filePath(QStringLiteral("SURVEY.FT.SUM.CSV"))));
        FtSurvey fromUpper;
        QVERIFY2(FlowTrackerCsvReader::readFile(
                     upper.filePath(QStringLiteral("SURVEY.FT.DAT.CSV")), &fromUpper, &error),
                 qPrintable(error));
        QCOMPARE(fromUpper.pointCount(), fromCsv.pointCount());
    }

    // the export omits bank stations, so only the open-water verticals appear
    QVector<const FtStation *> openWater;
    for (const FtStation &station : fromFt.stations) {
        if (!station.isBank())
            openWater.append(&station);
    }
    QCOMPARE(fromCsv.stations.size(), openWater.size());
    QCOMPARE(fromCsv.pointCount(), fromFt.pointCount());

    for (int s = 0; s < openWater.size(); ++s) {
        const FtStation &expected = *openWater.at(s);
        const FtStation &actual = fromCsv.stations.at(s);
        QCOMPARE(actual.location, expected.location);
        QCOMPARE(actual.depth, expected.depth);
        QCOMPARE(actual.points.size(), expected.points.size());

        for (int p = 0; p < expected.points.size(); ++p) {
            const FtPoint &want = expected.points.at(p);
            const FtPoint &got = actual.points.at(p);
            QCOMPARE(got.fractionalDepth, want.fractionalDepth);
            // z is derived as FinalD - MeasD here, and both are rounded to
            // millimetres in the export
            QVERIFY(std::fabs(got.distanceFromBottom - want.distanceFromBottom) < 1.5e-3);
            QCOMPARE(got.data.rowCount(), want.data.rowCount());

            // The export carries no spike indices, so its series is the raw
            // record while the .ft series has the flagged samples blanked.
            // Compare against the raw values, to the 3 decimals the export keeps.
            for (int i = 0; i < got.data.rowCount(); ++i) {
                if (want.instrumentSpikes.contains(i))
                    continue;
                QVERIFY2(std::fabs(got.data.columnByRole(Role::U).at(i)
                                   - want.data.columnByRole(Role::U).at(i)) < 1e-3,
                         qPrintable(QStringLiteral("station %1 point %2 sample %3")
                                        .arg(s).arg(p).arg(i)));
            }
        }
    }

    // correlation cannot be recovered from the export, so that filter is inert
    const AdvData &csvData = fromCsv.stations.first().points.first().data;
    QVERIFY(csvData.hasRole(Role::SnrX));
    QVERIFY(!csvData.hasRole(Role::CorrX));
    QVERIFY(!csvData.hasRole(Role::W2));
    QCOMPARE(csvData.samplingFrequency(), 2.0);

    // and without the spike mask the mean is the raw one, not the instrument's
    const FtPoint &reference = fromFt.stations.at(1).points.first();
    const double csvMean = flowstats::seriesStats(csvData.columnByRole(Role::U)).mean;
    QVERIFY(std::fabs(csvMean - 0.2748519653333333) < 1e-3);
    QVERIFY(std::fabs(csvMean - reference.referenceMeanU) > 1e-4);
}

void TestCore::modeLabelsAndEps()
{
    QCOMPARE(labels::tke(Mode::Lab), QStringLiteral("TKE"));
    QCOMPARE(labels::tke(Mode::Field), QStringLiteral("TKE proxy"));
    QCOMPARE(labels::tkeColumn(Mode::Field), QStringLiteral("TKE proxy (m^2/s^2)"));

    // the persisted identifier must not depend on the mode, or a project saved
    // in one mode loses its TKE curve when reopened in the other
    QCOMPARE(labels::tkeSeriesId(), QStringLiteral("@tke"));
    QVERIFY(labels::tkeSeriesId() != labels::tkeSeriesName(Mode::Lab));
    QVERIFY(labels::tkeSeriesId() != labels::tkeSeriesName(Mode::Field));

    // a FlowTracker2 record satisfies neither condition of the spectral estimator
    QVERIFY(!labels::epsEstimable(60, 2.0));
    QVERIFY(!labels::epsEstimable(60000, 2.0));  // rate too low
    QVERIFY(!labels::epsEstimable(60, 200.0));   // record too short
    QVERIFY(labels::epsEstimable(23952, 200.0)); // the Vectrino reference record
    QCOMPARE(labels::epsText(nan(), 60, 2.0),
             QStringLiteral("n/a (sampling rate too low)"));
    QCOMPARE(labels::epsText(nan(), 60, 200.0), QStringLiteral("n/a (record too short)"));

    // and that is exactly what the estimator itself does at 2 Hz, silently
    FtSurvey survey;
    QString error;
    QVERIFY2(FlowTrackerReader::readFile(kFtFile, &survey, &error), qPrintable(error));
    const AdvData &data = survey.stations.at(1).points.first().data;
    QVERIFY(std::isnan(flowstats::dissipationRate(data.columnByRole(Role::U), 2.0)));
}

void TestCore::projectFieldModeRoundTrip()
{
    FtSurvey survey;
    QString error;
    QVERIFY2(FlowTrackerReader::readFile(kFtFile, &survey, &error), qPrintable(error));

    ProjectModel model;
    model.setMode(Mode::Field);
    QVERIFY(model.setEpsg(25832));
    QVERIFY(!model.setEpsg(9999));
    QCOMPARE(model.epsg(), 25832); // a rejected code leaves the old one in place

    CrossSection section;
    section.name = survey.siteName;
    section.leftChainage = survey.stations.first().location;
    section.rightChainage = survey.stations.last().location;
    section.leftX = 677394.935;
    section.leftY = 5268148.607;
    section.rightX = 677398.0;
    section.rightY = 5268144.0;
    for (const FtStation &station : survey.stations)
        section.bed.append(QPointF(station.location, station.depth));
    model.addCrossSection(section);

    // place every open-water point along the section, computing the position
    // once per station so float noise cannot split one vertical into three
    for (const FtStation &station : survey.stations) {
        if (station.isBank())
            continue;
        double x = 0.0;
        double y = 0.0;
        QVERIFY(section.positionAt(station.location, &x, &y));
        for (const FtPoint &ftPoint : station.points) {
            MeasurementPoint point;
            point.x = x;
            point.y = y;
            point.z = ftPoint.distanceFromBottom;
            point.waterDepth = station.depth;
            point.chainage = station.location;
            point.stationName = QStringLiteral("station %1").arg(station.index);
            point.data = ftPoint.data;
            model.addPoint(point);
        }
    }
    QCOMPARE(model.points().size(), 14);
    QCOMPARE(model.profileKeys().size(), 6); // six verticals, not eighteen

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.advProj"));
    QVERIFY2(project::save(model, path, &error), qPrintable(error));

    // the extracted series are small; embedding the .ft archives per point
    // would have produced tens of megabytes
    QVERIFY2(QFileInfo(path).size() < 400 * 1024,
             qPrintable(QStringLiteral("project is %1 bytes").arg(QFileInfo(path).size())));

    ProjectModel restored;
    QString warning;
    QVERIFY2(project::load(&restored, path, &error, &warning), qPrintable(error));
    QVERIFY(warning.isEmpty());
    QCOMPARE(restored.mode(), Mode::Field);
    QCOMPARE(restored.epsg(), 25832);
    QCOMPARE(restored.points().size(), 14);
    QCOMPARE(restored.profileKeys().size(), 6);
    QCOMPARE(restored.crossSections().size(), 1);
    QCOMPARE(restored.crossSections().first().leftX, section.leftX);
    QCOMPARE(restored.crossSections().first().bed.size(), 8);

    for (int i = 0; i < model.points().size(); ++i) {
        const MeasurementPoint &before = model.points().at(i);
        const MeasurementPoint &after = restored.points().at(i);
        QCOMPARE(after.stationName, before.stationName);
        QCOMPARE(after.chainage, before.chainage);
        QCOMPARE(after.z, before.z);
        const auto a = model.processed(before.id);
        const auto b = restored.processed(after.id);
        QVERIFY(a && b);
        QCOMPARE(b->stats.u.n, a->stats.u.n);
        QVERIFY(std::fabs(b->stats.u.mean - a->stats.u.mean) < 1e-9);
        QVERIFY(std::fabs(b->stats.tke - a->stats.tke) < 1e-12);
    }

    // a lab project keeps defaulting to lab mode and no coordinate system
    ProjectModel lab;
    const QString labPath = dir.filePath(QStringLiteral("lab.advProj"));
    QVERIFY2(project::save(lab, labPath, &error), qPrintable(error));
    ProjectModel labRestored;
    QVERIFY2(project::load(&labRestored, labPath, &error), qPrintable(error));
    QCOMPARE(labRestored.mode(), Mode::Lab);
    QCOMPARE(labRestored.epsg(), 0);
}

void TestCore::projectRejectsFutureFormatVersion()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("future.advProj"));

    QJsonObject root;
    root[QStringLiteral("application")] = QStringLiteral("ADV-Explorer");
    root[QStringLiteral("formatVersion")] = 99;
    root[QStringLiteral("points")] = QJsonArray();
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();

    ProjectModel model;
    QString error;
    QVERIFY(!project::load(&model, path, &error));
    QVERIFY2(error.contains(QStringLiteral("99")), qPrintable(error));

    // a version 1 file has no mode, coordinate system or sections and must
    // still load, since that is every project written before this change
    QJsonObject v1;
    v1[QStringLiteral("application")] = QStringLiteral("ADV-Explorer");
    v1[QStringLiteral("formatVersion")] = 1;
    v1[QStringLiteral("points")] = QJsonArray();
    const QString v1Path = dir.filePath(QStringLiteral("v1.advProj"));
    QFile v1File(v1Path);
    QVERIFY(v1File.open(QIODevice::WriteOnly));
    v1File.write(QJsonDocument(v1).toJson(QJsonDocument::Compact));
    v1File.close();

    QVERIFY2(project::load(&model, v1Path, &error), qPrintable(error));
    QCOMPARE(model.mode(), Mode::Lab);
    QCOMPARE(model.epsg(), 0);
    QVERIFY(model.crossSections().isEmpty());
}

void TestCore::geoPackageImport()
{
    if (!geoimport::geoPackageSupported())
        QSKIP("Qt was built without the SQLite driver");

    const QString path = QStringLiteral(TEST_DATA_DIR "/positions.gpkg");
    QString error;
    const QVector<GeoPackageLayer> layers = geoimport::geoPackageLayers(path, &error);
    QVERIFY2(!layers.isEmpty(), qPrintable(error));
    QCOMPARE(layers.size(), 1);
    QCOMPARE(layers.first().name, QStringLiteral("isar_sep_tke"));
    QCOMPARE(layers.first().epsg, 25832);
    QCOMPARE(layers.first().featureCount, 5);

    const QVector<GeoPoint> points = geoimport::readGeoPackage(path, layers.first().name, &error);
    QCOMPARE(points.size(), 5);
    QCOMPARE(points.first().name, QStringLiteral("Isar FT 1"));
    QVERIFY(std::fabs(points.first().x - 677394.935) < 1e-6);
    QVERIFY(std::fabs(points.first().y - 5268148.607) < 1e-6);
    QVERIFY(std::fabs(points.at(4).x - 677395.526) < 1e-6);

    // and the coordinates land where they should on the basemap
    double lon = 0.0;
    double lat = 0.0;
    QVERIFY(crs::toWgs84(layers.first().epsg, points.first().x, points.first().y, &lon, &lat));
    QVERIFY(std::fabs(lon - 11.35738455) < 1e-6);
    QVERIFY(std::fabs(lat - 47.54252167) < 1e-6);

    // a missing layer and a non-GeoPackage both report rather than crash
    error.clear();
    QVERIFY(geoimport::readGeoPackage(path, QStringLiteral("nope"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(geoimport::geoPackageLayers(kVnaFile, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestCore::crossSectionPlacement()
{
    // The reference GeoPackage was built by interpolating stations along a
    // straight cross-section line at 0.5 m spacing. Reproducing that placement
    // is exactly what the import wizard's cross-section option does.
    // stations at chainage 17.00, 16.50 and 16.00 of survey 30_09_25-4; the
    // three GeoPackage features in between are the three depths of one station
    CrossSection section;
    section.leftChainage = 17.0;
    section.rightChainage = 16.0;
    section.leftX = 677394.935;
    section.leftY = 5268148.607;
    section.rightX = 677395.526;
    section.rightY = 5268147.8;

    QVERIFY(section.isValid());

    double x = 0.0;
    double y = 0.0;
    QVERIFY(section.positionAt(17.0, &x, &y));
    QCOMPARE(x, section.leftX);
    QCOMPARE(y, section.leftY);

    QVERIFY(section.positionAt(16.0, &x, &y));
    QCOMPARE(x, section.rightX);
    QCOMPARE(y, section.rightY);

    // the station half way along must land on the GeoPackage feature between
    // them, to the millimetre the file is written with
    QVERIFY(section.positionAt(16.5, &x, &y));
    QVERIFY2(std::fabs(x - 677395.231) < 1e-3 && std::fabs(y - 5268148.203) < 1e-3,
             qPrintable(QStringLiteral("got %1, %2").arg(x, 0, 'f', 3).arg(y, 0, 'f', 3)));

    // and 0.5 m along the tape stays 0.5 m on the ground
    double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
    QVERIFY(section.positionAt(17.0, &x1, &y1));
    QVERIFY(section.positionAt(16.5, &x2, &y2));
    QVERIFY(std::fabs(std::hypot(x2 - x1, y2 - y1) - 0.5) < 2e-3);

    // an incomplete section refuses to place anything
    CrossSection empty;
    QVERIFY(!empty.isValid());
    QVERIFY(!empty.positionAt(1.0, &x, &y));
}

void TestCore::crsLookup()
{
    QVERIFY(crs::isSupported(4326));
    QVERIFY(crs::isSupported(3857));
    QVERIFY(crs::isSupported(25832));
    QVERIFY(crs::isSupported(32632));
    QVERIFY(crs::isSupported(32732));
    QVERIFY(crs::isSupported(31468));
    QVERIFY(!crs::isSupported(9999));
    QVERIFY(!crs::isSupported(31465));

    QCOMPARE(crs::name(25832), QStringLiteral("ETRS89 / UTM zone 32N"));
    QCOMPARE(crs::name(32701), QStringLiteral("WGS 84 / UTM zone 1S"));
    QVERIFY(crs::name(9999).isEmpty());

    // only the Gauss-Krueger family carries a datum shift
    QVERIFY(crs::isApproximate(31468));
    QVERIFY(!crs::isApproximate(25832));

    QVERIFY(crs::supportedCodes().contains(25832));
    QVERIFY(!crs::supportedCodes().contains(9999));
}

void TestCore::crsReference()
{
    // Reference longitudes and latitudes were produced independently with
    // pyproj (PROJ 9), Transformer.from_crs(..., always_xy=True). The UTM
    // tolerance of 1e-7 degrees is about 1 cm; the residual comes from ETRS89
    // using GRS80 while this implementation uses the WGS 84 ellipsoid.
    struct Case {
        int epsg;
        double x, y;
        double lon, lat;
        double tolerance;
    };
    const QVector<Case> cases = {
        // the corner and a feature of input-data/flowtracker-data/TKE_Isar_Sep25.gpkg
        {25832, 677394.935, 5268148.607, 11.35738455000776, 47.54252166568013, 1e-7},
        {25832, 677105.430649, 5267927.481978, 11.353451679064134, 47.540612670241316, 1e-7},
        // exactly on the central meridian of zone 32
        {25832, 500000.0, 5268000.0, 9.0, 47.56541730630816, 1e-7},
        // Gauss-Krueger goes through an approximate countrywide datum shift, so
        // it is only expected to land within a couple of metres (1e-4 deg)
        {31468, 4468000.0, 5333000.0, 11.568634763771048, 48.13423133093679, 1e-4},
    };

    for (const Case &c : cases) {
        double lon = 0.0;
        double lat = 0.0;
        QVERIFY(crs::toWgs84(c.epsg, c.x, c.y, &lon, &lat));
        QVERIFY2(std::fabs(lon - c.lon) < c.tolerance,
                 qPrintable(QStringLiteral("EPSG:%1 lon %2 != %3")
                                .arg(c.epsg).arg(lon, 0, 'f', 10).arg(c.lon, 0, 'f', 10)));
        QVERIFY2(std::fabs(lat - c.lat) < c.tolerance,
                 qPrintable(QStringLiteral("EPSG:%1 lat %2 != %3")
                                .arg(c.epsg).arg(lat, 0, 'f', 10).arg(c.lat, 0, 'f', 10)));
    }

    // an unsupported code must fail rather than return garbage
    double lon = 12.34;
    double lat = 56.78;
    QVERIFY(!crs::toWgs84(9999, 1.0, 2.0, &lon, &lat));
    QCOMPARE(lon, 12.34);
    QCOMPARE(lat, 56.78);
}

void TestCore::crsRoundTrip()
{
    // projecting and unprojecting must return the input to well below the
    // measurement accuracy; Gauss-Krueger loses a fraction of a millimetre in
    // the datum shift because the ellipsoidal height is assumed to be zero
    struct Case {
        int epsg;
        double x, y;
        double tolerance;
    };
    const QVector<Case> cases = {
        {25832, 677394.935, 5268148.607, 1e-6},
        {32632, 677394.935, 5268148.607, 1e-6},
        {32732, 500000.0, 7000000.0, 1e-6},
        {31468, 4468000.0, 5333000.0, 2e-3},
        {3857, 1263000.0, 6021000.0, 1e-6},
        {4326, 11.35, 47.54, 1e-12},
    };

    for (const Case &c : cases) {
        double lon = 0.0;
        double lat = 0.0;
        QVERIFY(crs::toWgs84(c.epsg, c.x, c.y, &lon, &lat));
        double x = 0.0;
        double y = 0.0;
        QVERIFY(crs::fromWgs84(c.epsg, lon, lat, &x, &y));
        QVERIFY2(std::fabs(x - c.x) < c.tolerance && std::fabs(y - c.y) < c.tolerance,
                 qPrintable(QStringLiteral("EPSG:%1 round trip off by (%2, %3)")
                                .arg(c.epsg).arg(x - c.x, 0, 'g').arg(y - c.y, 0, 'g')));
    }

    // the tile projection helpers are each other's inverse as well
    double mx = 0.0;
    double my = 0.0;
    crs::wgs84ToWebMercator(11.35, 47.54, &mx, &my);
    double lon = 0.0;
    double lat = 0.0;
    crs::webMercatorToWgs84(mx, my, &lon, &lat);
    QVERIFY(std::fabs(lon - 11.35) < 1e-9);
    QVERIFY(std::fabs(lat - 47.54) < 1e-9);

    // latitudes beyond the square tile pyramid are clamped, not wrapped
    crs::wgs84ToWebMercator(0.0, 89.0, &mx, &my);
    QVERIFY(std::isfinite(my));
}

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
