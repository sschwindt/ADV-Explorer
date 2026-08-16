/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "ExampleProject.h"

#include "core/CsvReader.h"
#include "core/Despike.h"
#include "core/FlowTrackerReader.h"
#include "core/MeasurementPoint.h"
#include "core/ProjectModel.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>

#include <cmath>

/// advcore is a static library, so the linker keeps the resource initialiser
/// only if something references it. The macro has to be expanded outside any
/// namespace, which is why this sits here rather than next to its callers.
static void initExampleResources()
{
    Q_INIT_RESOURCE(examples);
}

using namespace adv;

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("examples", text);
}

QByteArray readResource(const QString &path, QString *errorString)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorString) {
            *errorString = QCoreApplication::translate(
                               "examples", "The example data is missing from this build (%1).")
                               .arg(path);
        }
        return {};
    }
    return file.readAll();
}

/// One time-series entry of the plot frame state, in the shape PlotFrame
/// restores. Keeping it here means the examples open with something already
/// plotted instead of an empty axis the user has to populate first.
QJsonObject makeSeries(const QUuid &id, const QString &column, const QString &color)
{
    QJsonObject style;
    style[QStringLiteral("lineColor")] = color;
    style[QStringLiteral("markerColor")] = color;
    QJsonObject series;
    series[QStringLiteral("pointId")] = id.toString(QUuid::WithoutBraces);
    series[QStringLiteral("column")] = column;
    series[QStringLiteral("style")] = style;
    return series;
}

} // namespace

namespace examples {

bool loadLab(ProjectModel *model, QString *errorString)
{
    if (!model)
        return false;
    initExampleResources();

    // The Vectrino tables carry three velocity columns and no time column, so
    // the mapping and the sampling rate are supplied here exactly as the import
    // wizard would ask the user for them.
    auto addPoint = [model, errorString](const QString &resource, double x, double y,
                                         double z, double depth, double uScale) -> QUuid {
        const QByteArray bytes = readResource(resource, errorString);
        if (bytes.isEmpty())
            return QUuid();

        QHash<Role, int> mapping;
        mapping.insert(Role::U, 0);
        mapping.insert(Role::V, 1);
        mapping.insert(Role::W1, 2);

        MeasurementPoint point;
        point.data = CsvReader::read(bytes, mapping, errorString);
        if (point.data.isEmpty())
            return QUuid();
        point.data.setSourceFileName(resource.section(QLatin1Char('/'), -1));
        point.data.synthesizeTime(200.0);

        // The six tables are repeated measurements rather than a real vertical,
        // so the streamwise component is scaled per height into a log-law-like
        // shape. That makes the profile panel show something recognisable; it is
        // a demonstration, never an analysis result.
        const int uColumn = point.data.columnOfRole(Role::U);
        if (uColumn >= 0 && uScale != 1.0) {
            for (double &u : point.data.column(uColumn))
                u *= uScale;
        }

        point.x = x;
        point.y = y;
        point.z = z;
        point.waterDepth = depth;
        point.despike.velEnabled = true;
        return model->addPoint(point);
    };

    model->clear();
    model->setMode(Mode::Lab);

    QList<QUuid> profileIds;
    for (int i = 1; i <= 5; ++i) {
        const double z = 0.05 * i;
        const double uScale = 0.6 + 0.4 * std::pow(z / 0.30, 0.4);
        const QUuid id = addPoint(QStringLiteral(":/examples/lab/vel%1.dat").arg(i),
                                  0.5, 0.0, z, 0.30, uScale);
        if (!id.isNull())
            profileIds.append(id);
    }
    addPoint(QStringLiteral(":/examples/lab/vel7.dat"), 1.5, 0.2, 0.10, 0.28, 1.0);

    if (profileIds.size() < 2) {
        if (errorString && errorString->isEmpty())
            *errorString = tr("The embedded laboratory example could not be read.");
        model->clear();
        return false;
    }

    QJsonArray series;
    series.append(makeSeries(profileIds.at(0), QStringLiteral("u (m/s)"),
                             QStringLiteral("#0072B2")));
    series.append(makeSeries(profileIds.at(0), QStringLiteral("w1 (m/s)"),
                             QStringLiteral("#009E73")));
    series.append(makeSeries(profileIds.at(3), QStringLiteral("u (m/s)"),
                             QStringLiteral("#E69F00")));
    QJsonObject frame;
    frame[QStringLiteral("palette")] = 0;
    frame[QStringLiteral("series")] = series;
    QJsonArray frames;
    frames.append(frame);

    QJsonObject profileState;
    profileState[QStringLiteral("profile")] = MeasurementPoint::makeXyKey(0.5, 0.0);
    profileState[QStringLiteral("u")] = true;
    profileState[QStringLiteral("v")] = true;
    profileState[QStringLiteral("w")] = true;
    profileState[QStringLiteral("relative")] = false;

    QJsonObject settings;
    settings[QStringLiteral("plotFrames")] = frames;
    settings[QStringLiteral("profileFrame")] = profileState;
    settings[QStringLiteral("flumeLength")] = 2.0;
    settings[QStringLiteral("flumeWidth")] = 0.4;
    model->setPlotSettings(settings);
    return true;
}

bool loadField(ProjectModel *model, QString *errorString)
{
    if (!model)
        return false;
    initExampleResources();

    const QByteArray bytes = readResource(QStringLiteral(":/examples/field/isar.ft"),
                                          errorString);
    if (bytes.isEmpty())
        return false;

    FtSurvey survey;
    if (!FlowTrackerReader::read(bytes, &survey, errorString))
        return false;

    QVector<const FtStation *> openWater;
    for (const FtStation &station : survey.stations) {
        if (!station.isBank())
            openWater.append(&station);
    }
    if (openWater.isEmpty()) {
        if (errorString)
            *errorString = tr("The embedded field example holds no velocity stations.");
        return false;
    }

    model->clear();
    model->setMode(Mode::Field);
    // ETRS89 / UTM zone 32N, the system the survey was recorded in
    if (!model->setEpsg(25832)) {
        if (errorString)
            *errorString = tr("EPSG:25832 is not available in this build.");
        return false;
    }

    // The instrument records a chainage along the tape, not a coordinate, so the
    // cross section is anchored at the surveyed left bank and run at the bearing
    // of the real tape. Deriving the far end from the tape length rather than
    // hard-coding it keeps the drawn line exactly as long as the survey says,
    // which is what the import wizard checks for when a user does this by hand.
    const double leftChainage = survey.firstChainage();
    const double rightChainage = survey.lastChainage();
    if (!std::isfinite(leftChainage) || !std::isfinite(rightChainage)
        || leftChainage == rightChainage) {
        if (errorString)
            *errorString = tr("The embedded field example has no usable cross-section ends.");
        return false;
    }

    constexpr double kLeftBankX = 677394.94;  // m, ETRS89 / UTM 32N
    constexpr double kLeftBankY = 5268148.61;
    constexpr double kBearingRad = 2.2016; // tape direction, radians from east
    const double tapeLength = std::abs(rightChainage - leftChainage);

    CrossSection section;
    section.name = survey.siteName.isEmpty() ? tr("Isar side channel") : survey.siteName;
    section.leftChainage = leftChainage;
    section.rightChainage = rightChainage;
    section.leftX = kLeftBankX;
    section.leftY = kLeftBankY;
    section.rightX = kLeftBankX + tapeLength * std::cos(kBearingRad);
    section.rightY = kLeftBankY + tapeLength * std::sin(kBearingRad);
    for (const FtStation &station : survey.stations) {
        if (std::isfinite(station.location) && std::isfinite(station.depth))
            section.bed.append(QPointF(station.location, station.depth));
    }
    model->addCrossSection(section);

    const DespikeConfig despike = fieldDespikeDefaults(survey.snrThresholdDb);

    QList<QUuid> stationFirstPoint;
    for (const FtStation *station : openWater) {
        // computed once per station: recomputing per point could differ in the
        // last bits, and profiles are keyed on the formatted coordinates, so one
        // vertical would split into several
        double x = 0.0;
        double y = 0.0;
        if (!section.positionAt(station->location, &x, &y))
            continue;

        bool first = true;
        for (const FtPoint &ftPoint : station->points) {
            MeasurementPoint point;
            point.x = x;
            point.y = y;
            point.z = ftPoint.distanceFromBottom;
            point.waterDepth = station->depth;
            point.chainage = station->location;
            point.stationName = tr("%1 station %2")
                                    .arg(section.name).arg(station->index + 1);
            point.despike = despike;
            point.data = ftPoint.data;
            const QUuid id = model->addPoint(point);
            if (first && !id.isNull()) {
                stationFirstPoint.append(id);
                first = false;
            }
        }
    }

    if (stationFirstPoint.isEmpty()) {
        if (errorString)
            *errorString = tr("No measurement points could be built from the field example.");
        model->clear();
        return false;
    }

    QJsonArray series;
    series.append(makeSeries(stationFirstPoint.first(), QStringLiteral("u (m/s)"),
                             QStringLiteral("#0072B2")));
    if (stationFirstPoint.size() > 1) {
        series.append(makeSeries(stationFirstPoint.at(stationFirstPoint.size() / 2),
                                 QStringLiteral("u (m/s)"), QStringLiteral("#E69F00")));
    }
    QJsonObject frame;
    frame[QStringLiteral("palette")] = 0;
    frame[QStringLiteral("series")] = series;
    QJsonArray frames;
    frames.append(frame);

    const MeasurementPoint *deepest = model->point(stationFirstPoint.first());
    QJsonObject profileState;
    if (deepest)
        profileState[QStringLiteral("profile")] = deepest->xyKey();
    profileState[QStringLiteral("u")] = true;
    profileState[QStringLiteral("v")] = true;
    profileState[QStringLiteral("w")] = true;
    profileState[QStringLiteral("relative")] = false;

    QJsonObject settings;
    settings[QStringLiteral("plotFrames")] = frames;
    settings[QStringLiteral("profileFrame")] = profileState;
    model->setPlotSettings(settings);
    return true;
}

} // namespace examples
