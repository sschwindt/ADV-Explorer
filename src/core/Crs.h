/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include <QString>
#include <QVector>

namespace adv {

/// Coordinate reference system conversions for the georeferenced (field) mode.
///
/// Only the projections needed to place river measurements on a web basemap are
/// implemented, so the application stays free of a PROJ/GDAL dependency:
///
/// * EPSG:4326  - WGS 84 geographic (x = longitude, y = latitude, both degrees)
/// * EPSG:3857  - WGS 84 / Pseudo-Mercator (the basemap tile projection)
/// * EPSG:326xx - WGS 84 / UTM zones 1N..60N
/// * EPSG:327xx - WGS 84 / UTM zones 1S..60S
/// * EPSG:258xx - ETRS89 / UTM zones 28N..38N
/// * EPSG:3146x - DHDN / 3-degree Gauss-Krueger zones 2..5
///
/// Transverse Mercator uses the Krueger series to sixth order, which is exact to
/// well below a millimetre inside a zone. ETRS89 is treated as identical to
/// WGS 84: the two differ by a slow continental drift of a few decimetres, which
/// is a uniform offset that does not distort the relative geometry of a survey.
/// Gauss-Krueger additionally needs a datum change from Bessel 1841 to WGS 84,
/// which is done with a single seven-parameter Helmert transformation valid for
/// Germany as a whole; isApproximate() reports the resulting metre-level error.
namespace crs {

/// True if the EPSG code is one of the supported systems.
bool isSupported(int epsg);

/// Human-readable name, e.g. "ETRS89 / UTM zone 32N"; empty if unsupported.
QString name(int epsg);

/// All supported EPSG codes, ordered for presentation in a chooser.
QVector<int> supportedCodes();

/// True when the conversion involves an approximate datum shift (~1 m).
bool isApproximate(int epsg);

/// Sentence naming the supported systems, for error messages.
QString supportedRangesText();

/// Convert projected or geographic coordinates to WGS 84 degrees.
/// Returns false (leaving the outputs untouched) if the code is unsupported.
bool toWgs84(int epsg, double x, double y, double *lon, double *lat);

/// Inverse of toWgs84().
bool fromWgs84(int epsg, double lon, double lat, double *x, double *y);

/// Web Mercator (EPSG:3857) metres from WGS 84 degrees. Latitude is clamped to
/// the +/- 85.0511 degrees that the square tile pyramid can represent.
void wgs84ToWebMercator(double lon, double lat, double *x, double *y);

/// Inverse of wgs84ToWebMercator().
void webMercatorToWgs84(double x, double y, double *lon, double *lat);

} // namespace crs
} // namespace adv
