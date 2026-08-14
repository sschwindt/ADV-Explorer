/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "core/Crs.h"

#include <QtMath>

#include <array>
#include <cmath>

namespace adv {
namespace crs {
namespace {

constexpr double kWebMercatorRadius = 6378137.0;
constexpr double kMaxWebMercatorLat = 85.05112877980659;

struct Ellipsoid {
    double a;
    double f;
};

const Ellipsoid kWgs84{6378137.0, 1.0 / 298.257223563};
const Ellipsoid kBessel1841{6377397.155, 1.0 / 299.1528128};

/// Seven-parameter Helmert transformation, position vector convention.
struct Helmert {
    double dx, dy, dz;    ///< translation (m)
    double rx, ry, rz;    ///< rotation (arc seconds)
    double scale;         ///< scale correction (ppm)
};

/// EPSG:1777, "DHDN to WGS 84 (2)", the countrywide German set. Its stated
/// accuracy is about 1 m, which is why isApproximate() flags Gauss-Krueger.
const Helmert kDhdnToWgs84{598.1, 73.7, 418.2, 0.202, 0.045, -2.455, 6.7};

enum class Kind { Geographic, WebMercator, TransverseMercator };

struct Definition {
    Kind kind = Kind::Geographic;
    Ellipsoid ellipsoid = kWgs84;
    bool needsDatumShift = false; ///< source is Bessel/DHDN, not WGS 84
    double lon0 = 0.0;            ///< central meridian (degrees)
    double k0 = 1.0;
    double falseEasting = 0.0;
    double falseNorthing = 0.0;
    QString name;
};

bool lookup(int epsg, Definition *def)
{
    if (epsg == 4326) {
        *def = Definition{Kind::Geographic, kWgs84, false, 0.0, 1.0, 0.0, 0.0,
                          QStringLiteral("WGS 84 (geographic)")};
        return true;
    }
    if (epsg == 3857) {
        *def = Definition{Kind::WebMercator, kWgs84, false, 0.0, 1.0, 0.0, 0.0,
                          QStringLiteral("WGS 84 / Pseudo-Mercator")};
        return true;
    }

    // WGS 84 / UTM, northern and southern hemisphere
    if (epsg >= 32601 && epsg <= 32660) {
        const int zone = epsg - 32600;
        *def = Definition{Kind::TransverseMercator, kWgs84, false, 6.0 * zone - 183.0,
                          0.9996, 500000.0, 0.0,
                          QStringLiteral("WGS 84 / UTM zone %1N").arg(zone)};
        return true;
    }
    if (epsg >= 32701 && epsg <= 32760) {
        const int zone = epsg - 32700;
        *def = Definition{Kind::TransverseMercator, kWgs84, false, 6.0 * zone - 183.0,
                          0.9996, 500000.0, 10000000.0,
                          QStringLiteral("WGS 84 / UTM zone %1S").arg(zone)};
        return true;
    }

    // ETRS89 / UTM zones 28N..38N, treated as WGS 84
    if (epsg >= 25828 && epsg <= 25838) {
        const int zone = epsg - 25800;
        *def = Definition{Kind::TransverseMercator, kWgs84, false, 6.0 * zone - 183.0,
                          0.9996, 500000.0, 0.0,
                          QStringLiteral("ETRS89 / UTM zone %1N").arg(zone)};
        return true;
    }

    // DHDN / 3-degree Gauss-Krueger zones 2..5
    if (epsg >= 31466 && epsg <= 31469) {
        const int zone = epsg - 31464;
        *def = Definition{Kind::TransverseMercator, kBessel1841, true, 3.0 * zone, 1.0,
                          zone * 1000000.0 + 500000.0, 0.0,
                          QStringLiteral("DHDN / 3-degree Gauss-Krueger zone %1").arg(zone)};
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Krueger series transverse Mercator (sixth order)
// ---------------------------------------------------------------------------

struct Series {
    double A;                    ///< rectifying radius times (1 + n)
    double e;                    ///< first eccentricity
    std::array<double, 7> alpha; ///< 1-based, index 0 unused
    std::array<double, 7> beta;
    std::array<double, 7> delta;
};

Series series(const Ellipsoid &ell)
{
    Series s{};
    const double f = ell.f;
    const double n = f / (2.0 - f);
    const double n2 = n * n, n3 = n2 * n, n4 = n3 * n, n5 = n4 * n, n6 = n5 * n;

    s.e = std::sqrt(f * (2.0 - f));
    s.A = ell.a / (1.0 + n) * (1.0 + n2 / 4.0 + n4 / 64.0 + n6 / 256.0);

    s.alpha[1] = 1.0 / 2.0 * n - 2.0 / 3.0 * n2 + 5.0 / 16.0 * n3
                 + 41.0 / 180.0 * n4 - 127.0 / 288.0 * n5 + 7891.0 / 37800.0 * n6;
    s.alpha[2] = 13.0 / 48.0 * n2 - 3.0 / 5.0 * n3 + 557.0 / 1440.0 * n4
                 + 281.0 / 630.0 * n5 - 1983433.0 / 1935360.0 * n6;
    s.alpha[3] = 61.0 / 240.0 * n3 - 103.0 / 140.0 * n4 + 15061.0 / 26880.0 * n5
                 + 167603.0 / 181440.0 * n6;
    s.alpha[4] = 49561.0 / 161280.0 * n4 - 179.0 / 168.0 * n5 + 6601661.0 / 7257600.0 * n6;
    s.alpha[5] = 34729.0 / 80640.0 * n5 - 3418889.0 / 1995840.0 * n6;
    s.alpha[6] = 212378941.0 / 319334400.0 * n6;

    s.beta[1] = 1.0 / 2.0 * n - 2.0 / 3.0 * n2 + 37.0 / 96.0 * n3
                - 1.0 / 360.0 * n4 - 81.0 / 512.0 * n5 + 96199.0 / 604800.0 * n6;
    s.beta[2] = 1.0 / 48.0 * n2 + 1.0 / 15.0 * n3 - 437.0 / 1440.0 * n4
                + 46.0 / 105.0 * n5 - 1118711.0 / 3870720.0 * n6;
    s.beta[3] = 17.0 / 480.0 * n3 - 37.0 / 840.0 * n4 - 209.0 / 4480.0 * n5
                + 5569.0 / 90720.0 * n6;
    s.beta[4] = 4397.0 / 161280.0 * n4 - 11.0 / 504.0 * n5 - 830251.0 / 7257600.0 * n6;
    s.beta[5] = 4583.0 / 161280.0 * n5 - 108847.0 / 3991680.0 * n6;
    s.beta[6] = 20648693.0 / 638668800.0 * n6;

    s.delta[1] = 2.0 * n - 2.0 / 3.0 * n2 - 2.0 * n3 + 116.0 / 45.0 * n4
                 + 26.0 / 45.0 * n5 - 2854.0 / 675.0 * n6;
    s.delta[2] = 7.0 / 3.0 * n2 - 8.0 / 5.0 * n3 - 227.0 / 45.0 * n4
                 + 2704.0 / 315.0 * n5 + 2323.0 / 945.0 * n6;
    s.delta[3] = 56.0 / 15.0 * n3 - 136.0 / 35.0 * n4 - 1262.0 / 105.0 * n5
                 + 73814.0 / 2835.0 * n6;
    s.delta[4] = 4279.0 / 630.0 * n4 - 332.0 / 35.0 * n5 - 399572.0 / 14175.0 * n6;
    s.delta[5] = 4174.0 / 315.0 * n5 - 144838.0 / 6237.0 * n6;
    s.delta[6] = 601676.0 / 22275.0 * n6;

    return s;
}

/// Normalise a longitude difference in radians to [-pi, pi].
double wrapPi(double radians)
{
    while (radians > M_PI)
        radians -= 2.0 * M_PI;
    while (radians < -M_PI)
        radians += 2.0 * M_PI;
    return radians;
}

void tmForward(const Definition &def, double lonDeg, double latDeg, double *east, double *north)
{
    const Series s = series(def.ellipsoid);
    const double phi = qDegreesToRadians(latDeg);
    const double lam = wrapPi(qDegreesToRadians(lonDeg - def.lon0));

    const double sinPhi = std::sin(phi);
    const double t = std::sinh(std::atanh(sinPhi) - s.e * std::atanh(s.e * sinPhi));

    const double xiP = std::atan2(t, std::cos(lam));
    const double etaP = std::asinh(std::sin(lam) / std::hypot(t, std::cos(lam)));

    double xi = xiP;
    double eta = etaP;
    for (int j = 1; j <= 6; ++j) {
        xi += s.alpha[j] * std::sin(2.0 * j * xiP) * std::cosh(2.0 * j * etaP);
        eta += s.alpha[j] * std::cos(2.0 * j * xiP) * std::sinh(2.0 * j * etaP);
    }

    *east = def.falseEasting + def.k0 * s.A * eta;
    *north = def.falseNorthing + def.k0 * s.A * xi;
}

void tmInverse(const Definition &def, double east, double north, double *lonDeg, double *latDeg)
{
    const Series s = series(def.ellipsoid);
    const double xi = (north - def.falseNorthing) / (def.k0 * s.A);
    const double eta = (east - def.falseEasting) / (def.k0 * s.A);

    double xiP = xi;
    double etaP = eta;
    for (int j = 1; j <= 6; ++j) {
        xiP -= s.beta[j] * std::sin(2.0 * j * xi) * std::cosh(2.0 * j * eta);
        etaP -= s.beta[j] * std::cos(2.0 * j * xi) * std::sinh(2.0 * j * eta);
    }

    const double chi = std::asin(std::sin(xiP) / std::cosh(etaP));
    double phi = chi;
    for (int j = 1; j <= 6; ++j)
        phi += s.delta[j] * std::sin(2.0 * j * chi);

    const double lam = std::atan2(std::sinh(etaP), std::cos(xiP));

    *latDeg = qRadiansToDegrees(phi);
    *lonDeg = def.lon0 + qRadiansToDegrees(lam);
}

// ---------------------------------------------------------------------------
// Datum shift (Bessel/DHDN <-> WGS 84)
// ---------------------------------------------------------------------------

/// Geodetic (degrees, ellipsoidal height 0) to geocentric cartesian metres.
void geodeticToCartesian(const Ellipsoid &ell, double lonDeg, double latDeg,
                         double *x, double *y, double *z)
{
    const double phi = qDegreesToRadians(latDeg);
    const double lam = qDegreesToRadians(lonDeg);
    const double e2 = ell.f * (2.0 - ell.f);
    const double sinPhi = std::sin(phi);
    const double nu = ell.a / std::sqrt(1.0 - e2 * sinPhi * sinPhi);

    *x = nu * std::cos(phi) * std::cos(lam);
    *y = nu * std::cos(phi) * std::sin(lam);
    *z = nu * (1.0 - e2) * sinPhi;
}

/// Inverse of geodeticToCartesian(); the height component is discarded.
void cartesianToGeodetic(const Ellipsoid &ell, double x, double y, double z,
                         double *lonDeg, double *latDeg)
{
    const double e2 = ell.f * (2.0 - ell.f);
    const double p = std::hypot(x, y);

    // Bowring's method converges to well below a millimetre in a few passes.
    double phi = std::atan2(z, p * (1.0 - e2));
    for (int i = 0; i < 8; ++i) {
        const double sinPhi = std::sin(phi);
        const double nu = ell.a / std::sqrt(1.0 - e2 * sinPhi * sinPhi);
        const double next = std::atan2(z + e2 * nu * sinPhi, p);
        if (std::fabs(next - phi) < 1e-14) {
            phi = next;
            break;
        }
        phi = next;
    }

    *latDeg = qRadiansToDegrees(phi);
    *lonDeg = qRadiansToDegrees(std::atan2(y, x));
}

void helmertApply(const Helmert &h, bool inverse, double *x, double *y, double *z)
{
    constexpr double kArcSecToRad = M_PI / (180.0 * 3600.0);
    const double rx = h.rx * kArcSecToRad;
    const double ry = h.ry * kArcSecToRad;
    const double rz = h.rz * kArcSecToRad;
    const double m = 1.0 + h.scale * 1e-6;

    if (!inverse) {
        const double xs = *x, ys = *y, zs = *z;
        *x = h.dx + m * (xs - rz * ys + ry * zs);
        *y = h.dy + m * (rz * xs + ys - rx * zs);
        *z = h.dz + m * (-ry * xs + rx * ys + zs);
    } else {
        // small-angle rotations are orthogonal to first order, so the inverse
        // is the transposed rotation applied after removing shift and scale
        const double xs = (*x - h.dx) / m;
        const double ys = (*y - h.dy) / m;
        const double zs = (*z - h.dz) / m;
        *x = xs + rz * ys - ry * zs;
        *y = -rz * xs + ys + rx * zs;
        *z = ry * xs - rx * ys + zs;
    }
}

} // namespace

bool isSupported(int epsg)
{
    Definition def;
    return lookup(epsg, &def);
}

QString name(int epsg)
{
    Definition def;
    if (!lookup(epsg, &def))
        return QString();
    return def.name;
}

bool isApproximate(int epsg)
{
    Definition def;
    return lookup(epsg, &def) && def.needsDatumShift;
}

QVector<int> supportedCodes()
{
    QVector<int> codes;
    codes << 4326 << 3857;
    for (int epsg = 25828; epsg <= 25838; ++epsg) // ETRS89 / UTM
        codes << epsg;
    for (int epsg = 31466; epsg <= 31469; ++epsg) // DHDN / Gauss-Krueger
        codes << epsg;
    for (int zone = 1; zone <= 60; ++zone) // WGS 84 / UTM north
        codes << 32600 + zone;
    for (int zone = 1; zone <= 60; ++zone) // WGS 84 / UTM south
        codes << 32700 + zone;
    return codes;
}

QString supportedRangesText()
{
    return QStringLiteral(
        "Supported systems are EPSG:4326 (WGS 84), EPSG:3857 (Pseudo-Mercator), "
        "EPSG:32601-32660 and EPSG:32701-32760 (WGS 84 / UTM), "
        "EPSG:25828-25838 (ETRS89 / UTM) and EPSG:31466-31469 "
        "(DHDN / 3-degree Gauss-Krueger).");
}

bool toWgs84(int epsg, double x, double y, double *lon, double *lat)
{
    Definition def;
    if (!lookup(epsg, &def))
        return false;

    double lonDeg = 0.0;
    double latDeg = 0.0;
    switch (def.kind) {
    case Kind::Geographic:
        lonDeg = x;
        latDeg = y;
        break;
    case Kind::WebMercator:
        webMercatorToWgs84(x, y, &lonDeg, &latDeg);
        break;
    case Kind::TransverseMercator:
        tmInverse(def, x, y, &lonDeg, &latDeg);
        break;
    }

    if (def.needsDatumShift) {
        double cx = 0.0, cy = 0.0, cz = 0.0;
        geodeticToCartesian(def.ellipsoid, lonDeg, latDeg, &cx, &cy, &cz);
        helmertApply(kDhdnToWgs84, false, &cx, &cy, &cz);
        cartesianToGeodetic(kWgs84, cx, cy, cz, &lonDeg, &latDeg);
    }

    *lon = lonDeg;
    *lat = latDeg;
    return true;
}

bool fromWgs84(int epsg, double lon, double lat, double *x, double *y)
{
    Definition def;
    if (!lookup(epsg, &def))
        return false;

    double lonDeg = lon;
    double latDeg = lat;
    if (def.needsDatumShift) {
        double cx = 0.0, cy = 0.0, cz = 0.0;
        geodeticToCartesian(kWgs84, lonDeg, latDeg, &cx, &cy, &cz);
        helmertApply(kDhdnToWgs84, true, &cx, &cy, &cz);
        cartesianToGeodetic(def.ellipsoid, cx, cy, cz, &lonDeg, &latDeg);
    }

    switch (def.kind) {
    case Kind::Geographic:
        *x = lonDeg;
        *y = latDeg;
        break;
    case Kind::WebMercator:
        wgs84ToWebMercator(lonDeg, latDeg, x, y);
        break;
    case Kind::TransverseMercator:
        tmForward(def, lonDeg, latDeg, x, y);
        break;
    }
    return true;
}

void wgs84ToWebMercator(double lon, double lat, double *x, double *y)
{
    const double clamped = qBound(-kMaxWebMercatorLat, lat, kMaxWebMercatorLat);
    *x = kWebMercatorRadius * qDegreesToRadians(lon);
    *y = kWebMercatorRadius
         * std::log(std::tan(M_PI / 4.0 + qDegreesToRadians(clamped) / 2.0));
}

void webMercatorToWgs84(double x, double y, double *lon, double *lat)
{
    *lon = qRadiansToDegrees(x / kWebMercatorRadius);
    *lat = qRadiansToDegrees(2.0 * std::atan(std::exp(y / kWebMercatorRadius)) - M_PI / 2.0);
}

} // namespace crs
} // namespace adv
