/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "core/FlowTrackerCsvReader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cmath>
#include <utility>

namespace adv {
namespace {

/// Leading lines of both exports: a build-version line, the header row and the
/// unit row. None of them are read; only the column positions below matter.
constexpr int kHeaderLines = 3;

/// Fixed column positions of `*.ft.dat.csv`.
enum DatColumn {
    DatStation = 0,
    DatVelX = 7, DatVelY = 8, DatVelZ = 9,
    DatSnr1 = 10, DatSnr2 = 11, DatSnr3 = 12,
    DatTemp = 16,
    DatColumnCount = 17,
};

/// Fixed column positions of `*.ft.sum.csv`.
enum SumColumn {
    SumStation = 1,
    SumFractionalDepth = 3,
    SumMeasuredDepth = 4,
    SumSampleCount = 5,
    SumSpikeCount = 6,
    SumLocation = 8,
    SumFinalDepth = 12,
    SumColumnCount = 20,
};

/// FlowTracker2 writes decimal commas and the literal "NaN".
double toDouble(const QString &token)
{
    QString text = token.trimmed();
    if (text.isEmpty())
        return nan();
    text.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double value = text.toDouble(&ok);
    return ok ? value : nan();
}

QVector<QStringList> parseRows(const QByteArray &bytes, int minimumColumns)
{
    QVector<QStringList> rows;
    QString text = QString::fromUtf8(bytes);
    if (!text.isEmpty() && text.at(0) == QChar(0xFEFF))
        text.remove(0, 1); // byte order mark

    const QStringList lines = text.split(QLatin1Char('\n'));
    int lineNo = 0;
    for (const QString &rawLine : lines) {
        ++lineNo;
        if (lineNo <= kHeaderLines)
            continue;
        const QString line = rawLine.trimmed(); // also drops a trailing CR
        if (line.isEmpty())
            continue;
        const QStringList tokens = line.split(QLatin1Char(';'));
        if (tokens.size() < minimumColumns)
            continue;
        rows.append(tokens);
    }
    return rows;
}

} // namespace

namespace {

/// Offset from the end of ".ft.dat.csv" / ".ft.sum.csv" at which the three
/// distinguishing characters sit.
constexpr int kKindOffset = 7;

/// Swap "dat" for "sum" (or back) in place, keeping the case the instrument
/// software happened to write. Rebuilding the ending from a lower-case literal
/// works on Windows, whose file system is case insensitive, but leaves an
/// unopenable path on Linux whenever the export is named in upper case.
QString withKind(const QString &path, const QString &kind)
{
    QString result = path;
    const int at = result.size() - kKindOffset;
    const bool upper = result.at(at).isUpper();
    result.replace(at, 3, upper ? kind.toUpper() : kind);
    return result;
}

/// Resolve a sibling that only differs from an existing file in case. Linux
/// needs this; on Windows the direct path already opens.
QString resolveCaseInsensitively(const QString &path)
{
    if (QFileInfo::exists(path))
        return path;

    const QFileInfo info(path);
    const QDir dir = info.absoluteDir();
    const QStringList siblings =
        dir.entryList(QDir::Files | QDir::Hidden, QDir::Name);
    for (const QString &sibling : siblings) {
        if (sibling.compare(info.fileName(), Qt::CaseInsensitive) == 0)
            return dir.filePath(sibling);
    }
    return path; // let the caller report the original name in its error
}

} // namespace

QString FlowTrackerCsvReader::summaryPathFor(const QString &path)
{
    if (path.endsWith(QStringLiteral(".ft.sum.csv"), Qt::CaseInsensitive))
        return path;
    if (path.endsWith(QStringLiteral(".ft.dat.csv"), Qt::CaseInsensitive))
        return withKind(path, QStringLiteral("sum"));
    return QString();
}

bool FlowTrackerCsvReader::read(const QByteArray &datBytes, const QByteArray &sumBytes,
                                FtSurvey *survey, QString *errorString)
{
    if (!survey)
        return false;
    *survey = FtSurvey();

    const QVector<QStringList> datRows = parseRows(datBytes, DatColumnCount);
    const QVector<QStringList> sumRows = parseRows(sumBytes, SumColumnCount);
    if (sumRows.isEmpty()) {
        if (errorString)
            *errorString = QStringLiteral("The summary export contains no point measurements.");
        return false;
    }
    if (datRows.isEmpty()) {
        if (errorString)
            *errorString = QStringLiteral("The raw sample export contains no samples.");
        return false;
    }

    // walk the summary in order and hand each point measurement the next block
    // of samples belonging to its station
    int cursor = 0;
    int previousStation = -1;
    for (const QStringList &sumRow : sumRows) {
        // the two exports pad the station number to different widths ("001" in
        // the summary, "01" in the raw samples), so compare the numbers
        const int stationNumber = int(toDouble(sumRow.at(SumStation)));
        const QString stationLabel = QString::number(stationNumber);
        const double location = toDouble(sumRow.at(SumLocation));
        const double finalDepth = toDouble(sumRow.at(SumFinalDepth));
        const double measuredDepth = toDouble(sumRow.at(SumMeasuredDepth));
        const int expected = int(toDouble(sumRow.at(SumSampleCount)));

        if (expected <= 0) {
            if (errorString)
                *errorString = QStringLiteral("Station %1 reports %2 samples.")
                                   .arg(stationLabel).arg(expected);
            return false;
        }

        // skip any samples of an earlier station that were not consumed
        while (cursor < datRows.size()
               && int(toDouble(datRows.at(cursor).at(DatStation))) != stationNumber)
            ++cursor;

        if (cursor + expected > datRows.size()) {
            if (errorString)
                *errorString = QStringLiteral(
                                   "The exports do not match: station %1 needs %2 samples but "
                                   "the raw export has only %3 left.")
                                   .arg(stationLabel).arg(expected)
                                   .arg(datRows.size() - cursor);
            return false;
        }

        FtPoint point;
        point.fractionalDepth = toDouble(sumRow.at(SumFractionalDepth));
        // the summary reports the depth below the surface; z is measured up
        // from the bed
        point.distanceFromBottom = finalDepth - measuredDepth;

        QVector<double> time(expected);
        QVector<double> u(expected), v(expected), w(expected);
        QVector<double> snr1(expected), snr2(expected), snr3(expected);
        QVector<double> temperature(expected);

        for (int i = 0; i < expected; ++i) {
            const QStringList &row = datRows.at(cursor + i);
            time[i] = i / survey->samplingFrequency;
            u[i] = toDouble(row.at(DatVelX));
            v[i] = toDouble(row.at(DatVelY));
            w[i] = toDouble(row.at(DatVelZ));
            snr1[i] = toDouble(row.at(DatSnr1));
            snr2[i] = toDouble(row.at(DatSnr2));
            snr3[i] = toDouble(row.at(DatSnr3));
            temperature[i] = toDouble(row.at(DatTemp));
        }
        cursor += expected;

        AdvData data;
        data.addColumn(roleName(Role::Time), std::move(time), Role::Time);
        data.addColumn(roleName(Role::U), std::move(u), Role::U);
        data.addColumn(roleName(Role::V), std::move(v), Role::V);
        data.addColumn(roleName(Role::W1), std::move(w), Role::W1);
        data.addColumn(roleName(Role::SnrX), std::move(snr1), Role::SnrX);
        data.addColumn(roleName(Role::SnrY), std::move(snr2), Role::SnrY);
        data.addColumn(roleName(Role::SnrZ1), std::move(snr3), Role::SnrZ1);
        data.addColumn(QStringLiteral("temperature (C)"), std::move(temperature), Role::Other);
        data.setSamplingFrequency(survey->samplingFrequency);
        data.setFormat(QStringLiteral("ft"));
        data.setRawBytes(FlowTrackerReader::canonicalBytes(data));
        point.data = std::move(data);

        // the export names stations by measurement order, so a new number starts
        // a new vertical
        if (survey->stations.isEmpty() || stationNumber != previousStation) {
            previousStation = stationNumber;
            FtStation station;
            station.id = stationLabel;
            station.index = survey->stations.size();
            station.location = location;
            station.depth = finalDepth;
            // banks are not exported, so everything present holds velocity data
            station.stationType = QStringLiteral("OpenWater");
            survey->stations.append(station);
        }

        FtStation &station = survey->stations.last();
        point.stationIndex = station.index;
        point.pointIndex = station.points.size();
        station.points.append(point);
    }

    return true;
}

bool FlowTrackerCsvReader::readFile(const QString &datPath, FtSurvey *survey,
                                    QString *errorString)
{
    const QString namedSumPath = summaryPathFor(datPath);
    if (namedSumPath.isEmpty()) {
        if (errorString)
            *errorString = QStringLiteral(
                               "%1 does not follow the FlowTracker2 export naming convention "
                               "(<survey>.ft.dat.csv next to <survey>.ft.sum.csv).")
                               .arg(QFileInfo(datPath).fileName());
        return false;
    }

    const QString sumPath = resolveCaseInsensitively(namedSumPath);

    QString actualDatPath = datPath;
    if (actualDatPath.endsWith(QStringLiteral(".ft.sum.csv"), Qt::CaseInsensitive))
        actualDatPath = resolveCaseInsensitively(withKind(actualDatPath, QStringLiteral("dat")));

    QFile datFile(actualDatPath);
    if (!datFile.open(QIODevice::ReadOnly)) {
        if (errorString)
            *errorString = QStringLiteral("Cannot open %1: %2")
                               .arg(actualDatPath, datFile.errorString());
        return false;
    }
    QFile sumFile(sumPath);
    if (!sumFile.open(QIODevice::ReadOnly)) {
        if (errorString)
            *errorString = QStringLiteral(
                               "Cannot open the matching summary export %1: %2")
                               .arg(QFileInfo(sumPath).fileName(), sumFile.errorString());
        return false;
    }

    if (!read(datFile.readAll(), sumFile.readAll(), survey, errorString))
        return false;

    survey->sourceFileName = QFileInfo(actualDatPath).fileName();
    for (FtStation &station : survey->stations) {
        for (FtPoint &point : station.points)
            point.data.setSourceFileName(survey->sourceFileName);
    }
    return true;
}

} // namespace adv
