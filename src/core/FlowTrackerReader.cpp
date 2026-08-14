/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "core/FlowTrackerReader.h"

#include "core/ZipArchive.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cmath>
#include <iterator>
#include <utility>

namespace adv {
namespace {

const QString kDataFile = QStringLiteral("DataFile.json");

/// JSON numbers are doubles, but absent and null members must become NaN rather
/// than zero, or a missing depth would silently read as a depth of 0 m.
double number(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toDouble() : nan();
}

QDateTime dateTime(const QJsonObject &object, const QString &key)
{
    return QDateTime::fromString(object.value(key).toString(), Qt::ISODateWithMs);
}

/// "00:00:30" as seconds; NaN when the member is missing or malformed.
double durationSeconds(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char(':'));
    if (parts.size() != 3)
        return nan();
    bool okH = false, okM = false, okS = false;
    const double h = parts.at(0).toDouble(&okH);
    const double m = parts.at(1).toDouble(&okM);
    const double s = parts.at(2).toDouble(&okS);
    if (!okH || !okM || !okS)
        return nan();
    return h * 3600.0 + m * 60.0 + s;
}

/// Column layout of the extracted series. Order is fixed because it is also the
/// column order of the canonical bytes stored in a project file.
struct ColumnSpec {
    Role role;
    const char *path; ///< see sampleValue()
};

const ColumnSpec kColumns[] = {
    {Role::U, "vx"},   {Role::V, "vy"},     {Role::W1, "vz"},
    {Role::SnrX, "s0"}, {Role::SnrY, "s1"}, {Role::SnrZ1, "s2"},
    {Role::CorrX, "c0"}, {Role::CorrY, "c1"}, {Role::CorrZ1, "c2"},
};

double sampleValue(const QJsonObject &adv, const char *what)
{
    const QJsonObject velocity = adv.value(QStringLiteral("Velocity (m/s)")).toObject();
    const QJsonObject snr = adv.value(QStringLiteral("Snr (dB)")).toObject();
    const QJsonObject corr = adv.value(QStringLiteral("CorrelationScore")).toObject();

    switch (what[0]) {
    case 'v':
        return number(velocity, QString(QLatin1Char(what[1])).toUpper());
    case 's':
        return number(snr, QStringLiteral("Beam%1").arg(what[1]));
    case 'c':
        // the instrument reports 0 to 1; scale to the percentage the rest of the
        // application and the despiking filters expect
        return number(corr, QStringLiteral("Beam%1").arg(what[1])) * 100.0;
    default:
        return nan();
    }
}

/// Build the AdvData of one point measurement from its JSON document.
bool readPointMeasurement(const QByteArray &json, double surveyRate, FtPoint *point,
                          QString *errorString)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (root.isEmpty()) {
        if (errorString)
            *errorString = QStringLiteral("Point measurement %1 is not a JSON object.")
                               .arg(point->id);
        return false;
    }

    const QJsonArray samples = root.value(QStringLiteral("Samples")).toArray();
    if (samples.isEmpty()) {
        if (errorString)
            *errorString = QStringLiteral("Point measurement %1 has no samples.").arg(point->id);
        return false;
    }

    double rate = number(root, QStringLiteral("SamplingRate (Hz)"));
    if (!std::isfinite(rate) || rate <= 0.0)
        rate = surveyRate;

    for (const QJsonValue &value : root.value(QStringLiteral("Spikes")).toArray())
        point->instrumentSpikes.append(value.toInt(-1));

    const int n = samples.size();
    constexpr int kColumnCount = int(std::size(kColumns));

    QVector<double> time(n);
    QVector<QVector<double>> values(kColumnCount, QVector<double>(n, nan()));
    QVector<double> temperature(n, nan());

    for (int i = 0; i < n; ++i) {
        // the instrument samples at a fixed rate, so deriving the time base from
        // the index is exact and avoids parsing 7-digit fractional seconds
        time[i] = i / rate;

        const QJsonObject sample = samples.at(i).toObject();
        const QJsonObject adv = sample.value(QStringLiteral("Adv")).toObject();
        for (int c = 0; c < kColumnCount; ++c)
            values[c][i] = sampleValue(adv, kColumns[c].path);

        temperature[i] = number(sample.value(QStringLiteral("Sensors")).toObject(),
                                QStringLiteral("Temperature (C)"));
    }

    // blank the samples the instrument flagged; every estimator downstream skips
    // NaN, so this is what reproduces the instrument's own despiked statistics
    for (const int index : std::as_const(point->instrumentSpikes)) {
        if (index < 0 || index >= n)
            continue;
        for (int c = 0; c < kColumnCount; ++c) {
            if (kColumns[c].role == Role::U || kColumns[c].role == Role::V
                || kColumns[c].role == Role::W1)
                values[c][index] = nan();
        }
    }

    AdvData data;
    data.addColumn(roleName(Role::Time), std::move(time), Role::Time);
    for (int c = 0; c < kColumnCount; ++c)
        data.addColumn(roleName(kColumns[c].role), std::move(values[c]), kColumns[c].role);
    data.addColumn(QStringLiteral("temperature (C)"), std::move(temperature), Role::Other);

    data.setSamplingFrequency(rate);
    data.setFormat(QStringLiteral("ft"));
    data.setRawBytes(FlowTrackerReader::canonicalBytes(data));

    point->data = std::move(data);
    return true;
}

} // namespace

int FtSurvey::pointCount() const
{
    int total = 0;
    for (const FtStation &station : stations)
        total += station.points.size();
    return total;
}

double FtSurvey::firstChainage() const
{
    return stations.isEmpty() ? nan() : stations.first().location;
}

double FtSurvey::lastChainage() const
{
    return stations.isEmpty() ? nan() : stations.last().location;
}

bool FlowTrackerReader::read(const QByteArray &ftBytes, FtSurvey *survey, QString *errorString)
{
    if (!survey)
        return false;
    *survey = FtSurvey();

    ZipArchive archive;
    if (!archive.open(ftBytes, errorString))
        return false;

    QString readError;
    const QByteArray dataFile = archive.read(kDataFile, &readError);
    if (dataFile.isEmpty()) {
        if (errorString)
            *errorString = QStringLiteral("The file is not a FlowTracker2 measurement: %1")
                               .arg(readError);
        return false;
    }

    const QJsonObject root = QJsonDocument::fromJson(dataFile).object();
    if (root.isEmpty()) {
        if (errorString)
            *errorString = QStringLiteral("%1 is not a JSON object.").arg(kDataFile);
        return false;
    }

    const QJsonObject properties = root.value(QStringLiteral("Properties")).toObject();
    survey->siteName = properties.value(QStringLiteral("SiteName")).toString();
    survey->siteNumber = properties.value(QStringLiteral("SiteNumber")).toString();
    survey->operatorName = properties.value(QStringLiteral("Operator")).toString();
    survey->startTime = dateTime(properties, QStringLiteral("StartTime"));
    survey->endTime = dateTime(properties, QStringLiteral("EndTime"));

    const QJsonObject configuration = root.value(QStringLiteral("Configuration")).toObject();
    const double rate = number(configuration, QStringLiteral("SamplingRate (Hz)"));
    if (std::isfinite(rate) && rate > 0.0)
        survey->samplingFrequency = rate;
    survey->averagingSeconds =
        durationSeconds(configuration.value(QStringLiteral("AveragingTime")).toString());
    survey->snrThresholdDb =
        number(configuration.value(QStringLiteral("QualityControl")).toObject(),
               QStringLiteral("SnrThreshold (dB)"));

    const QJsonArray stations = root.value(QStringLiteral("Stations")).toArray();
    if (stations.isEmpty()) {
        if (errorString)
            *errorString = QStringLiteral("The measurement contains no stations.");
        return false;
    }

    for (int s = 0; s < stations.size(); ++s) {
        const QJsonObject stationJson = stations.at(s).toObject();

        FtStation station;
        station.id = stationJson.value(QStringLiteral("Id")).toString();
        station.index = s;
        station.location = number(stationJson, QStringLiteral("Location (m)"));
        station.depth = number(stationJson, QStringLiteral("Depth (m)"));
        station.stationType = stationJson.value(QStringLiteral("StationType")).toString();
        station.velocityMethod = stationJson.value(QStringLiteral("VelocityMethod")).toString();

        const QJsonValue gps = stationJson.value(QStringLiteral("Gps"));
        if (gps.isObject()) {
            const QJsonObject gpsObject = gps.toObject();
            station.latitude = number(gpsObject, QStringLiteral("Latitude"));
            station.longitude = number(gpsObject, QStringLiteral("Longitude"));
            station.hasGps = std::isfinite(station.latitude) && std::isfinite(station.longitude);
        }

        const QJsonArray points = stationJson.value(QStringLiteral("PointMeasurements")).toArray();
        for (int p = 0; p < points.size(); ++p) {
            const QJsonObject pointJson = points.at(p).toObject();

            FtPoint point;
            point.id = pointJson.value(QStringLiteral("Id")).toString();
            point.stationIndex = s;
            point.pointIndex = p;
            point.fractionalDepth = number(pointJson, QStringLiteral("FractionalDepth"));
            point.distanceFromBottom = number(pointJson, QStringLiteral("DistanceFromBottom"));
            point.startTime = dateTime(pointJson, QStringLiteral("StartTime"));

            const QJsonObject despiked =
                pointJson.value(QStringLiteral("SampleStatistics")).toObject()
                    .value(QStringLiteral("DespikedVelocity (m/s)")).toObject()
                    .value(QStringLiteral("X")).toObject();
            point.referenceMeanU = number(despiked, QStringLiteral("Average"));
            point.referenceSampleStdU = number(despiked, QStringLiteral("StandardDeviation"));
            point.referenceCount = despiked.value(QStringLiteral("Count")).toInt();

            const QString entry =
                QStringLiteral("PointMeasurements/PointMeasurement_%1.json").arg(point.id);
            const QByteArray json = archive.read(entry, &readError);
            if (json.isEmpty()) {
                if (errorString)
                    *errorString = QStringLiteral("Station %1, point %2: %3")
                                       .arg(s + 1).arg(p + 1).arg(readError);
                return false;
            }
            if (!readPointMeasurement(json, survey->samplingFrequency, &point, errorString))
                return false;

            station.points.append(point);
        }

        survey->stations.append(station);
    }

    return true;
}

bool FlowTrackerReader::readFile(const QString &filePath, FtSurvey *survey, QString *errorString)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorString)
            *errorString = QStringLiteral("Cannot open %1: %2").arg(filePath, file.errorString());
        return false;
    }
    if (!read(file.readAll(), survey, errorString))
        return false;

    survey->sourceFileName = QFileInfo(filePath).fileName();
    for (FtStation &station : survey->stations) {
        for (FtPoint &point : station.points)
            point.data.setSourceFileName(survey->sourceFileName);
    }
    return true;
}

QByteArray FlowTrackerReader::canonicalBytes(const AdvData &data)
{
    QByteArray out;
    out.reserve(data.rowCount() * data.columnCount() * 12);

    out += data.columnNames().join(QLatin1Char(',')).toUtf8();
    out += '\n';

    for (int row = 0; row < data.rowCount(); ++row) {
        for (int col = 0; col < data.columnCount(); ++col) {
            if (col > 0)
                out += ',';
            const double value = data.column(col).at(row);
            // %.10g round-trips through the reader well inside instrument
            // resolution; non-finite values are written as "nan" so the
            // instrument's spike flags survive a save and reload
            out += std::isfinite(value) ? QByteArray::number(value, 'g', 10)
                                        : QByteArrayLiteral("nan");
        }
        out += '\n';
    }
    return out;
}

QHash<Role, int> FlowTrackerReader::canonicalMapping(const AdvData &data)
{
    // the canonical form writes the columns in their existing order, so the
    // stored role map is already the mapping CsvReader needs
    return data.roleMap();
}

} // namespace adv
