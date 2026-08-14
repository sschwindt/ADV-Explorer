/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace adv {

/// A surveyed position read from an external file.
struct GeoPoint {
    QString name;
    double x = 0.0;
    double y = 0.0;
};

/// One point layer of a GeoPackage.
struct GeoPackageLayer {
    QString name;
    QString geometryColumn;
    int epsg = 0;
    int featureCount = 0;
};

/// Import of measurement positions surveyed elsewhere, so a FlowTracker2 cross
/// section can reuse coordinates that were already worked out in a GIS.
///
/// GeoPackages are plain SQLite databases, so they are read through Qt's SQLite
/// driver and a small WKB point decoder rather than by linking GDAL. Only point
/// layers are of interest here; anything else is ignored.
namespace geoimport {

/// True when Qt's SQLite driver is available. GeoPackage import is unavailable
/// without it and the caller should fall back to the plain CSV path.
bool geoPackageSupported();

QVector<GeoPackageLayer> geoPackageLayers(const QString &filePath, QString *errorString = nullptr);

/// Read the point features of one layer, in feature id order.
QVector<GeoPoint> readGeoPackage(const QString &filePath, const QString &layer,
                                 QString *errorString = nullptr);

/// Read positions from a delimited text file. Column indices are zero based;
/// pass -1 for nameColumn when the file has no names.
QVector<GeoPoint> readPointCsv(const QString &filePath, int xColumn, int yColumn,
                               int nameColumn, QString *errorString = nullptr);

/// Column names of a delimited text file, for letting the user pick columns.
QStringList pointCsvColumns(const QString &filePath, QString *errorString = nullptr);

} // namespace geoimport
} // namespace adv
