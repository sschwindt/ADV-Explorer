/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "core/FormatRegistry.h"

#include "core/CsvReader.h"
#include "core/VnaReader.h"

#include <QFileInfo>

namespace adv {
namespace formats {
namespace {

/// FlowTracker2 points are stored as the canonical comma-separated form written
/// by FlowTrackerReader::canonicalBytes(), which CsvReader already understands.
/// Only the format tag differs, so that provenance survives a save and reload
/// and the point wizard knows not to offer a column-mapping table.
AdvData readExtractedFlowTracker(const QByteArray &bytes, const QHash<Role, int> &mapping,
                                 QString *errorString)
{
    AdvData data = CsvReader::read(bytes, mapping, errorString);
    if (!data.isEmpty())
        data.setFormat(QStringLiteral("ft"));
    return data;
}

QVector<FileFormat> makeFormats()
{
    QVector<FileFormat> list;

    FileFormat vna;
    vna.id = QStringLiteral("vna");
    vna.displayName = QStringLiteral("Nortek Vectrino ASCII");
    vna.nameEndings = {QStringLiteral(".vna")};
    vna.read = [](const QByteArray &bytes, const QHash<Role, int> &, QString *error) {
        return VnaReader::read(bytes, error);
    };
    list.append(vna);

    FileFormat ft;
    ft.id = QStringLiteral("ft");
    ft.displayName = QStringLiteral("SonTek FlowTracker2 measurement");
    ft.nameEndings = {QStringLiteral(".ft")};
    ft.multiPoint = true;
    ft.read = readExtractedFlowTracker;
    list.append(ft);

    // must be tested before the generic ".csv" entry
    FileFormat ftCsv;
    ftCsv.id = QStringLiteral("ftcsv");
    ftCsv.displayName = QStringLiteral("SonTek FlowTracker2 CSV export");
    ftCsv.nameEndings = {QStringLiteral(".ft.dat.csv"), QStringLiteral(".ft.sum.csv")};
    ftCsv.multiPoint = true;
    ftCsv.read = readExtractedFlowTracker;
    list.append(ftCsv);

    FileFormat csv;
    csv.id = QStringLiteral("csv");
    csv.displayName = QStringLiteral("Delimited text");
    csv.nameEndings = {QStringLiteral(".csv"), QStringLiteral(".txt"), QStringLiteral(".dat")};
    csv.needsColumnMapping = true;
    csv.read = [](const QByteArray &bytes, const QHash<Role, int> &mapping, QString *error) {
        return CsvReader::read(bytes, mapping, error);
    };
    list.append(csv);

    return list;
}

} // namespace

const QVector<FileFormat> &all()
{
    static const QVector<FileFormat> formats = makeFormats();
    return formats;
}

const FileFormat *byId(const QString &id)
{
    for (const FileFormat &format : all()) {
        if (format.id == id)
            return &format;
    }
    return nullptr;
}

const FileFormat *byFilePath(const QString &path)
{
    const QString fileName = QFileInfo(path).fileName();

    // longest ending wins, so ".ft.dat.csv" is never shadowed by ".csv"
    const FileFormat *best = nullptr;
    int bestLength = 0;
    for (const FileFormat &format : all()) {
        for (const QString &ending : format.nameEndings) {
            if (ending.size() > bestLength && fileName.endsWith(ending, Qt::CaseInsensitive)) {
                best = &format;
                bestLength = ending.size();
            }
        }
    }
    return best;
}

QString openFileFilter()
{
    QStringList allPatterns;
    QStringList perFormat;
    for (const FileFormat &format : all()) {
        QStringList patterns;
        for (const QString &ending : format.nameEndings)
            patterns.append(QLatin1Char('*') + ending);
        allPatterns += patterns;
        perFormat.append(QStringLiteral("%1 (%2)")
                             .arg(format.displayName, patterns.join(QLatin1Char(' '))));
    }

    QStringList filters;
    filters.append(QStringLiteral("Measurement data (%1)").arg(allPatterns.join(QLatin1Char(' '))));
    filters += perFormat;
    filters.append(QStringLiteral("All files (*)"));
    return filters.join(QStringLiteral(";;"));
}

} // namespace formats
} // namespace adv
