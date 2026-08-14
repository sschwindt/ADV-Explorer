/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "core/GeoPointImport.h"

#include "core/CsvReader.h"

#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QtEndian>

namespace adv {
namespace geoimport {
namespace {

const QString kDriver = QStringLiteral("QSQLITE");

/// RAII holder so the connection is always closed and removed, even on an early
/// return; leaving connections open keeps the file locked on Windows.
class Connection
{
public:
    explicit Connection(const QString &filePath)
        : m_name(QStringLiteral("advgpkg-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(kDriver, m_name);
        db.setDatabaseName(filePath);
        m_open = db.open();
        m_error = db.lastError().text();
    }

    ~Connection()
    {
        {
            QSqlDatabase db = QSqlDatabase::database(m_name, false);
            if (db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(m_name);
    }

    bool isOpen() const { return m_open; }
    QString error() const { return m_error; }
    QSqlDatabase db() const { return QSqlDatabase::database(m_name, false); }

private:
    QString m_name;
    bool m_open = false;
    QString m_error;
};

/// Decode a GeoPackage geometry blob holding a point.
///
/// Layout: the magic "GP", a version byte, a flags byte, the srs id, an
/// optional envelope whose size the flags encode, then standard WKB.
bool decodePoint(const QByteArray &blob, double *x, double *y)
{
    if (blob.size() < 8 || blob.at(0) != 'G' || blob.at(1) != 'P')
        return false;

    const quint8 flags = quint8(blob.at(3));
    const int envelopeIndicator = (flags >> 1) & 0x07;
    static const int envelopeSizes[] = {0, 32, 48, 48, 64};
    if (envelopeIndicator > 4)
        return false;
    const int headerSize = 8 + envelopeSizes[envelopeIndicator];
    if (blob.size() < headerSize + 21)
        return false;

    const char *wkb = blob.constData() + headerSize;
    const bool little = quint8(wkb[0]) == 1;
    const quint32 type = little ? qFromLittleEndian<quint32>(wkb + 1)
                                : qFromBigEndian<quint32>(wkb + 1);
    // 1 = Point; the 1000/2000/3000 offsets are the Z, M and ZM variants, whose
    // first two ordinates are still x and y
    if (type % 1000 != 1)
        return false;

    *x = little ? qFromLittleEndian<double>(wkb + 5) : qFromBigEndian<double>(wkb + 5);
    *y = little ? qFromLittleEndian<double>(wkb + 13) : qFromBigEndian<double>(wkb + 13);
    return true;
}

} // namespace

bool geoPackageSupported()
{
    return QSqlDatabase::isDriverAvailable(kDriver);
}

QVector<GeoPackageLayer> geoPackageLayers(const QString &filePath, QString *errorString)
{
    QVector<GeoPackageLayer> layers;
    if (!geoPackageSupported()) {
        if (errorString)
            *errorString = QStringLiteral(
                "This build has no SQLite database driver, so GeoPackage files cannot be "
                "read. Export the positions as a delimited text file instead.");
        return layers;
    }

    Connection connection(filePath);
    if (!connection.isOpen()) {
        if (errorString)
            *errorString = QStringLiteral("Cannot open %1: %2")
                               .arg(QFileInfo(filePath).fileName(), connection.error());
        return layers;
    }

    QSqlQuery query(connection.db());
    if (!query.exec(QStringLiteral(
            "SELECT c.table_name, g.column_name, g.srs_id, g.geometry_type_name "
            "FROM gpkg_contents c JOIN gpkg_geometry_columns g "
            "ON c.table_name = g.table_name WHERE c.data_type = 'features'"))) {
        if (errorString)
            *errorString = QStringLiteral("%1 is not a GeoPackage: %2")
                               .arg(QFileInfo(filePath).fileName(), query.lastError().text());
        return layers;
    }

    while (query.next()) {
        const QString type = query.value(3).toString().toUpper();
        if (!type.contains(QStringLiteral("POINT")))
            continue; // only point layers can position a measurement station

        GeoPackageLayer layer;
        layer.name = query.value(0).toString();
        layer.geometryColumn = query.value(1).toString();
        layer.epsg = query.value(2).toInt();

        QSqlQuery count(connection.db());
        if (count.exec(QStringLiteral("SELECT COUNT(*) FROM \"%1\"").arg(layer.name))
            && count.next())
            layer.featureCount = count.value(0).toInt();

        layers.append(layer);
    }

    if (layers.isEmpty() && errorString)
        *errorString = QStringLiteral("%1 contains no point layers.")
                           .arg(QFileInfo(filePath).fileName());
    return layers;
}

QVector<GeoPoint> readGeoPackage(const QString &filePath, const QString &layerName,
                                 QString *errorString)
{
    QVector<GeoPoint> points;

    const QVector<GeoPackageLayer> layers = geoPackageLayers(filePath, errorString);
    const GeoPackageLayer *layer = nullptr;
    for (const GeoPackageLayer &candidate : layers) {
        if (candidate.name == layerName) {
            layer = &candidate;
            break;
        }
    }
    if (!layer) {
        if (errorString && errorString->isEmpty())
            *errorString = QStringLiteral("The GeoPackage has no point layer \"%1\".")
                               .arg(layerName);
        return points;
    }

    Connection connection(filePath);
    if (!connection.isOpen()) {
        if (errorString)
            *errorString = connection.error();
        return points;
    }

    // a name column is optional; prefer one that looks like a label
    QStringList nameCandidates;
    QSqlQuery columns(connection.db());
    if (columns.exec(QStringLiteral("PRAGMA table_info(\"%1\")").arg(layer->name))) {
        while (columns.next()) {
            const QString column = columns.value(1).toString();
            if (column.compare(QStringLiteral("name"), Qt::CaseInsensitive) == 0
                || column.compare(QStringLiteral("point"), Qt::CaseInsensitive) == 0
                || column.compare(QStringLiteral("label"), Qt::CaseInsensitive) == 0
                || column.compare(QStringLiteral("station"), Qt::CaseInsensitive) == 0)
                nameCandidates.append(column);
        }
    }
    const QString nameColumn = nameCandidates.value(0);

    const QString sql =
        nameColumn.isEmpty()
            ? QStringLiteral("SELECT \"%1\" FROM \"%2\"").arg(layer->geometryColumn, layer->name)
            : QStringLiteral("SELECT \"%1\", \"%2\" FROM \"%3\"")
                  .arg(layer->geometryColumn, nameColumn, layer->name);

    QSqlQuery query(connection.db());
    if (!query.exec(sql)) {
        if (errorString)
            *errorString = QStringLiteral("Cannot read layer \"%1\": %2")
                               .arg(layer->name, query.lastError().text());
        return points;
    }

    int skipped = 0;
    while (query.next()) {
        GeoPoint point;
        if (!decodePoint(query.value(0).toByteArray(), &point.x, &point.y)) {
            ++skipped;
            continue;
        }
        if (!nameColumn.isEmpty())
            point.name = query.value(1).toString();
        points.append(point);
    }

    if (points.isEmpty() && errorString) {
        *errorString = QStringLiteral("Layer \"%1\" holds no readable point geometries.")
                           .arg(layer->name);
    } else if (skipped > 0 && errorString) {
        *errorString = QStringLiteral("%1 of %2 features were not simple points and were skipped.")
                           .arg(skipped).arg(skipped + points.size());
    }
    return points;
}

QStringList pointCsvColumns(const QString &filePath, QString *errorString)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorString)
            *errorString = QStringLiteral("Cannot open %1: %2").arg(filePath, file.errorString());
        return {};
    }
    return CsvReader::preview(file.readAll()).columnNames;
}

QVector<GeoPoint> readPointCsv(const QString &filePath, int xColumn, int yColumn,
                               int nameColumn, QString *errorString)
{
    QVector<GeoPoint> points;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorString)
            *errorString = QStringLiteral("Cannot open %1: %2").arg(filePath, file.errorString());
        return points;
    }
    const QByteArray bytes = file.readAll();
    const CsvReader::Preview preview = CsvReader::preview(bytes, 1000000);

    if (xColumn < 0 || yColumn < 0 || xColumn >= preview.columnCount
        || yColumn >= preview.columnCount) {
        if (errorString)
            *errorString = QStringLiteral("The chosen coordinate columns are outside the "
                                          "%1 columns of the file.")
                               .arg(preview.columnCount);
        return points;
    }

    for (const QStringList &row : preview.sampleRows) {
        if (row.size() <= qMax(xColumn, yColumn))
            continue;
        bool okX = false;
        bool okY = false;
        GeoPoint point;
        point.x = row.at(xColumn).trimmed().toDouble(&okX);
        point.y = row.at(yColumn).trimmed().toDouble(&okY);
        if (!okX || !okY)
            continue; // a stray header or a blank line
        if (nameColumn >= 0 && nameColumn < row.size())
            point.name = row.at(nameColumn).trimmed();
        points.append(point);
    }

    if (points.isEmpty() && errorString)
        *errorString = QStringLiteral("No usable coordinate rows were found in %1.")
                           .arg(QFileInfo(filePath).fileName());
    return points;
}

} // namespace geoimport
} // namespace adv
