/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "MapView.h"

#include "core/Crs.h"
#include "core/ProjectModel.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QSslSocket>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>

#include <cmath>
#include <limits>
#include <utility>

using namespace adv;

namespace {

constexpr int kTileSize = 256;
constexpr int kMinZoom = 2;
/// The OSM tile pyramid stops here, and asking beyond it just wastes requests.
constexpr int kMaxTileZoom = 19;
/// The view may go closer than the tiles do, scaling the deepest tiles up.
/// Field stations sit half a metre apart, which is only two pixels at zoom 19,
/// so without this they would be impossible to tell apart or click.
constexpr int kMaxZoom = 23;
/// The usage policy asks for a low, steady request rate; two at a time keeps
/// panning responsive without ever looking like a bulk download.
constexpr int kMaxInFlight = 2;
constexpr int kMarkerRadius = 7;
/// Breathing room left around the survey when the view is fitted to it.
constexpr double kFitMargin = 48.0;
/// Zoom used when the survey has no extent to fit, roughly a street view.
constexpr int kSinglePointZoom = 19;
/// Below this the widget has no usable geometry yet and a fit is deferred.
constexpr int kMinFitExtent = 64;
/// Movement beyond this many pixels makes a press a pan rather than a click.
constexpr int kDragThreshold = 4;

const QString kDefaultTileUrl =
    QStringLiteral("https://tile.openstreetmap.org/{z}/{x}/{y}.png");
const QString kDefaultAttribution = QStringLiteral("(c) OpenStreetMap contributors");

QString userAgent()
{
    // the tile usage policy requires an identifying agent naming the application
#ifdef ADV_EXPLORER_VERSION
    return QStringLiteral("ADV-Explorer/%1 (+%2)")
        .arg(QStringLiteral(ADV_EXPLORER_VERSION), QStringLiteral(ADV_EXPLORER_URL));
#else
    return QStringLiteral("ADV-Explorer (+https://github.com/sschwindt/ADV-Explorer)");
#endif
}

} // namespace

MapView::MapView(ProjectModel *model, QWidget *parent)
    : SiteView(parent)
    , m_model(model)
    , m_tiles(512)
    , m_tileUrl(kDefaultTileUrl)
    , m_attribution(kDefaultAttribution)
{
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setMinimumHeight(200);

    m_network = new QNetworkAccessManager(this);
    auto *cache = new QNetworkDiskCache(this);
    cache->setCacheDirectory(
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/osm-tiles"));
    cache->setMaximumCacheSize(200LL * 1024 * 1024);
    m_network->setCache(cache);
    connect(m_network, &QNetworkAccessManager::finished, this, &MapView::tileFinished);

    // Tile servers are HTTPS only. A deployment without a working TLS backend
    // (a bundled AppImage missing OpenSSL, for instance) would otherwise fail
    // every request silently and just look like a blank map.
    if (!QSslSocket::supportsSsl()) {
        m_offline = true;
        m_tlsUnavailable = true;
    }

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(2);

    auto *controlBar = new QWidget(this);
    // the guided tour points at this bar by name
    controlBar->setObjectName(QStringLiteral("mapControlBar"));
    controlBar->setAutoFillBackground(true);
    auto *controls = new QHBoxLayout(controlBar);
    controls->setContentsMargins(4, 2, 4, 2);
    m_basemapCheck = new QCheckBox(tr("Online basemap"), controlBar);
    m_basemapCheck->setChecked(true);
    m_basemapCheck->setToolTip(tr("Download map tiles from the configured tile server.\n"
                                  "Turn this off to work offline; the coordinate grid,\n"
                                  "the measurement points and every interaction keep working."));
    connect(m_basemapCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_basemapEnabled = on;
        if (on) {
            m_offline = false;
            m_failed.clear();
            requestVisibleTiles();
        }
        update();
    });
    controls->addWidget(m_basemapCheck);
    controls->addSpacing(12);
    m_readout = new QLabel(controlBar);
    m_readout->setMinimumWidth(260);
    controls->addWidget(m_readout);
    controls->addStretch();
    m_status = new QLabel(controlBar);
    controls->addWidget(m_status);
    outer->addWidget(controlBar);
    outer->addStretch(1); // the map is painted across the rest of the widget

    for (auto signal : {&ProjectModel::pointAdded, &ProjectModel::pointChanged,
                        &ProjectModel::pointRemoved})
        connect(m_model, signal, this, [this](const QUuid &) { rebuild(); });
    connect(m_model, &ProjectModel::modelReset, this, &MapView::rebuild);
    connect(m_model, &ProjectModel::crossSectionsChanged, this, &MapView::rebuild);
    connect(m_model, &ProjectModel::crsChanged, this, [this](int) { rebuild(); });

    // start over the centre of the earth; fitToPoints() takes over as soon as
    // the project has anything to show
    m_center = QPointF(worldSize() / 2.0, worldSize() / 2.0);
}

MapView::~MapView() = default;

// ---------------------------------------------------------------------------
// projection
// ---------------------------------------------------------------------------

double MapView::worldSize() const
{
    return double(kTileSize) * std::pow(2.0, m_zoom);
}

bool MapView::modelToWorldAtSize(double x, double y, double size, QPointF *world) const
{
    const int epsg = m_model->epsg();
    if (epsg == 0)
        return false;
    double lon = 0.0;
    double lat = 0.0;
    if (!crs::toWgs84(epsg, x, y, &lon, &lat))
        return false;

    const double wx = (lon + 180.0) / 360.0 * size;
    const double sinLat = std::sin(qDegreesToRadians(qBound(-85.05112878, lat, 85.05112878)));
    const double wy = (0.5 - std::log((1.0 + sinLat) / (1.0 - sinLat)) / (4.0 * M_PI)) * size;
    *world = QPointF(wx, wy);
    return true;
}

bool MapView::modelToWorld(double x, double y, QPointF *world) const
{
    return modelToWorldAtSize(x, y, worldSize(), world);
}

bool MapView::worldToModel(const QPointF &world, double *x, double *y) const
{
    const int epsg = m_model->epsg();
    if (epsg == 0)
        return false;
    const double size = worldSize();
    const double lon = world.x() / size * 360.0 - 180.0;
    const double n = M_PI - 2.0 * M_PI * world.y() / size;
    const double lat = qRadiansToDegrees(std::atan(std::sinh(n)));
    return crs::fromWgs84(epsg, lon, lat, x, y);
}

QPointF MapView::worldToWidget(const QPointF &world) const
{
    return QPointF(world.x() - m_center.x() + width() / 2.0,
                   world.y() - m_center.y() + height() / 2.0);
}

QPointF MapView::widgetToWorld(const QPointF &widget) const
{
    return QPointF(widget.x() - width() / 2.0 + m_center.x(),
                   widget.y() - height() / 2.0 + m_center.y());
}

// ---------------------------------------------------------------------------
// tiles
// ---------------------------------------------------------------------------

QString MapView::tileKey(int x, int y, int z)
{
    return QStringLiteral("%1/%2/%3").arg(z).arg(x).arg(y);
}

int MapView::tileZoom() const
{
    return qMin(m_zoom, kMaxTileZoom);
}

double MapView::tileWorldSize() const
{
    // one tile covers this many world pixels at the current view zoom
    return double(kTileSize) * std::pow(2.0, m_zoom - tileZoom());
}

void MapView::requestVisibleTiles()
{
    if (!m_basemapEnabled || m_offline || m_tileUrl.isEmpty())
        return;

    const int z = tileZoom();
    const int span = 1 << z;
    const double tileWorld = tileWorldSize();
    const QPointF topLeft = widgetToWorld(QPointF(0, 0));
    const QPointF bottomRight = widgetToWorld(QPointF(width(), height()));

    const int firstX = int(std::floor(topLeft.x() / tileWorld));
    const int lastX = int(std::floor(bottomRight.x() / tileWorld));
    const int firstY = qMax(0, int(std::floor(topLeft.y() / tileWorld)));
    const int lastY = qMin(span - 1, int(std::floor(bottomRight.y() / tileWorld)));

    // only what is actually on screen, never a prefetch ring
    for (int ty = firstY; ty <= lastY; ++ty) {
        for (int tx = firstX; tx <= lastX; ++tx) {
            const int wrapped = ((tx % span) + span) % span;
            requestTile(wrapped, ty, z);
        }
    }
}

void MapView::requestTile(int x, int y, int z)
{
    if (m_inFlight >= kMaxInFlight)
        return;
    const QString key = tileKey(x, y, z);
    if (m_tiles.contains(key) || m_pending.contains(key) || m_failed.contains(key))
        return;

    QString url = m_tileUrl;
    url.replace(QStringLiteral("{z}"), QString::number(z));
    url.replace(QStringLiteral("{x}"), QString::number(x));
    url.replace(QStringLiteral("{y}"), QString::number(y));

    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("User-Agent", userAgent().toUtf8());
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::PreferCache);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(10000);

    m_pending.insert(key);
    ++m_inFlight;
    QNetworkReply *reply = m_network->get(request);
    reply->setProperty("tileKey", key);
}

void MapView::tileFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    const QString key = reply->property("tileKey").toString();
    if (key.isEmpty())
        return;
    m_pending.remove(key);
    m_inFlight = qMax(0, m_inFlight - 1);

    if (reply->error() != QNetworkReply::NoError) {
        m_failed.insert(key);
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // 429 and 418 are how the tile servers say "stop"; anything at the
        // network level means there is nothing to reach. Either way, give up
        // for this session rather than hammering.
        if (status == 429 || status == 418
            || reply->error() == QNetworkReply::HostNotFoundError
            || reply->error() == QNetworkReply::UnknownNetworkError
            || reply->error() == QNetworkReply::TemporaryNetworkFailureError) {
            m_offline = true;
            if (m_status) {
                m_status->setText(status == 429 || status == 418
                                      ? tr("Basemap paused (tile server rate limit)")
                                      : tr("Basemap unavailable (offline)"));
            }
        }
        update();
        return;
    }

    QImage image;
    if (!image.loadFromData(reply->readAll())) {
        m_failed.insert(key);
        return;
    }
    m_tiles.insert(key, new QImage(std::move(image)));
    if (m_status)
        m_status->clear();
    update();
    requestVisibleTiles(); // keep the small pipeline full
}

// ---------------------------------------------------------------------------
// painting
// ---------------------------------------------------------------------------

void MapView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(232, 234, 228));

    if (m_basemapEnabled && !m_offline)
        paintTiles(&painter);
    paintGraticule(&painter);
    paintCrossSections(&painter);
    paintMarkers(&painter);
    paintScaleBar(&painter);
    paintAttribution(&painter);

    if (m_tlsUnavailable && m_basemapEnabled && m_status && m_status->text().isEmpty()) {
        m_status->setText(tr("Basemap unavailable (no TLS support in this build)"));
    }

    if (m_model->epsg() == 0) {
        painter.setPen(QColor(120, 40, 40));
        painter.drawText(rect().adjusted(16, 40, -16, 0), Qt::AlignTop | Qt::AlignHCenter,
                         tr("No project coordinate system set.\n"
                            "Choose one under Project > Coordinate system to place points."));
    }
}

void MapView::paintTiles(QPainter *painter) const
{
    const int z = tileZoom();
    const int span = 1 << z;
    const double tileWorld = tileWorldSize();
    const QPointF topLeft = widgetToWorld(QPointF(0, 0));
    const QPointF bottomRight = widgetToWorld(QPointF(width(), height()));

    const int firstX = int(std::floor(topLeft.x() / tileWorld));
    const int lastX = int(std::floor(bottomRight.x() / tileWorld));
    const int firstY = qMax(0, int(std::floor(topLeft.y() / tileWorld)));
    const int lastY = qMin(span - 1, int(std::floor(bottomRight.y() / tileWorld)));

    // beyond the deepest tile level the imagery is simply scaled up; it goes
    // soft, but the geometry stays right and the stations become separable
    const bool overzoomed = m_zoom > z;
    painter->setRenderHint(QPainter::SmoothPixmapTransform, overzoomed);

    for (int ty = firstY; ty <= lastY; ++ty) {
        for (int tx = firstX; tx <= lastX; ++tx) {
            const int wrapped = ((tx % span) + span) % span;
            const QPointF origin = worldToWidget(QPointF(tx * tileWorld, ty * tileWorld));
            const QRectF target(origin, QSizeF(tileWorld, tileWorld));
            if (const QImage *tile = m_tiles.object(tileKey(wrapped, ty, z)))
                painter->drawImage(target, *tile);
            else
                painter->fillRect(target, QColor(222, 224, 218));
        }
    }
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
}

void MapView::paintGraticule(QPainter *painter) const
{
    if (m_model->epsg() == 0)
        return;

    // a grid in whole project-CRS metres, so the view stays quantitative with
    // or without a basemap
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
    if (!worldToModel(widgetToWorld(QPointF(0, height())), &x0, &y0)
        || !worldToModel(widgetToWorld(QPointF(width(), 0)), &x1, &y1))
        return;
    if (!std::isfinite(x0) || !std::isfinite(x1) || x1 <= x0)
        return;

    // aim for roughly five lines across the view
    const double rawStep = (x1 - x0) / 5.0;
    const double magnitude = std::pow(10.0, std::floor(std::log10(qMax(rawStep, 1e-6))));
    double step = magnitude;
    for (const double factor : {1.0, 2.0, 5.0, 10.0}) {
        step = factor * magnitude;
        if (step >= rawStep)
            break;
    }

    const bool geographic = m_model->epsg() == 4326;
    painter->setPen(QPen(QColor(120, 130, 140, 110), 1, Qt::DotLine));
    QFont font = painter->font();
    font.setPointSizeF(qMax(7.0, font.pointSizeF() - 1.0));
    painter->setFont(font);

    for (double x = std::ceil(x0 / step) * step; x <= x1; x += step) {
        QPointF world;
        if (!modelToWorld(x, (y0 + y1) / 2.0, &world))
            continue;
        const double px = worldToWidget(world).x();
        painter->drawLine(QPointF(px, 0), QPointF(px, height()));
        painter->drawText(QPointF(px + 3, height() - 22),
                          QString::number(x, 'f', geographic ? 4 : 0));
    }
    for (double y = std::ceil(y0 / step) * step; y <= y1; y += step) {
        QPointF world;
        if (!modelToWorld((x0 + x1) / 2.0, y, &world))
            continue;
        const double py = worldToWidget(world).y();
        painter->drawLine(QPointF(0, py), QPointF(width(), py));
        painter->drawText(QPointF(4, py - 3), QString::number(y, 'f', geographic ? 4 : 0));
    }
}

void MapView::paintCrossSections(QPainter *painter) const
{
    painter->setPen(QPen(QColor(180, 60, 40, 200), 2, Qt::DashLine));
    for (const CrossSection &section : m_model->crossSections()) {
        if (!section.isValid())
            continue;
        QPointF left, right;
        if (!modelToWorld(section.leftX, section.leftY, &left)
            || !modelToWorld(section.rightX, section.rightY, &right))
            continue;
        painter->drawLine(worldToWidget(left), worldToWidget(right));
    }
}

void MapView::paintMarkers(QPainter *painter) const
{
    QFont badgeFont = painter->font();
    badgeFont.setPointSizeF(qMax(7.0, badgeFont.pointSizeF() - 1.5));
    badgeFont.setBold(true);

    for (const Marker &marker : m_markers) {
        painter->setPen(QPen(Qt::black, 1.2));
        painter->setBrush(marker.hasDepth ? QColor(20, 50, 120) : QColor(90, 90, 90));
        painter->drawEllipse(marker.widgetPos, kMarkerRadius, kMarkerRadius);

        // one marker per station: on a map the neighbouring station may be only
        // half a metre away, so fanning points out in pixels would collide with
        // it. Show how many z positions the station holds instead.
        if (marker.pointIds.size() > 1) {
            painter->setFont(badgeFont);
            painter->setPen(Qt::white);
            painter->drawText(QRectF(marker.widgetPos.x() - kMarkerRadius,
                                     marker.widgetPos.y() - kMarkerRadius,
                                     2 * kMarkerRadius, 2 * kMarkerRadius),
                              Qt::AlignCenter, QString::number(marker.pointIds.size()));
        }
    }
}

void MapView::paintScaleBar(QPainter *painter) const
{
    const double size = worldSize();
    const double n = M_PI - 2.0 * M_PI * m_center.y() / size;
    const double centerLat = qRadiansToDegrees(std::atan(std::sinh(n)));
    const double metresPerPixel =
        156543.03392 * std::cos(qDegreesToRadians(centerLat)) / std::pow(2.0, m_zoom);
    if (!std::isfinite(metresPerPixel) || metresPerPixel <= 0.0)
        return;

    // pick a round distance close to 100 px
    const double rawLength = 100.0 * metresPerPixel;
    const double magnitude = std::pow(10.0, std::floor(std::log10(rawLength)));
    double metres = magnitude;
    for (const double factor : {1.0, 2.0, 5.0, 10.0}) {
        metres = factor * magnitude;
        if (metres >= rawLength)
            break;
    }
    const double pixels = metres / metresPerPixel;

    const double y = height() - 12.0;
    const double x = 12.0;
    painter->setPen(QPen(Qt::black, 2));
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(QPointF(x, y), QPointF(x + pixels, y));
    painter->drawLine(QPointF(x, y - 4), QPointF(x, y + 4));
    painter->drawLine(QPointF(x + pixels, y - 4), QPointF(x + pixels, y + 4));
    painter->drawText(QPointF(x, y - 8),
                      metres >= 1000.0 ? tr("%1 km").arg(metres / 1000.0, 0, 'g', 2)
                                       : tr("%1 m").arg(metres, 0, 'g', 2));
}

void MapView::paintAttribution(QPainter *painter) const
{
    // required whenever OpenStreetMap data is displayed, and it must survive
    // into exported images, which it does because this is the paint path
    if (!m_basemapEnabled || m_attribution.isEmpty())
        return;

    QFont font = painter->font();
    font.setPointSizeF(qMax(7.0, font.pointSizeF() - 1.0));
    painter->setFont(font);

    const QString text = m_attribution;
    const QRectF bounds = painter->fontMetrics().boundingRect(text);
    const QRectF box(width() - bounds.width() - 12, height() - bounds.height() - 6,
                     bounds.width() + 8, bounds.height() + 4);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(255, 255, 255, 190));
    painter->drawRoundedRect(box, 3, 3);
    painter->setPen(QColor(40, 40, 40));
    painter->drawText(box, Qt::AlignCenter, text);
}

QImage MapView::renderImage(const QSize &size, int dpi) const
{
    QImage image(size, QImage::Format_ARGB32);
    image.setDotsPerMeterX(int(dpi / 0.0254));
    image.setDotsPerMeterY(int(dpi / 0.0254));
    image.fill(Qt::white);
    const_cast<MapView *>(this)->render(&image);
    return image;
}

// ---------------------------------------------------------------------------
// model and interaction
// ---------------------------------------------------------------------------

void MapView::rebuild()
{
    rebuildMarkers();
    requestVisibleTiles();
    update();
}

void MapView::rebuildMarkers()
{
    m_markers.clear();
    QHash<QString, int> byStation;

    for (const MeasurementPoint &point : m_model->points()) {
        QPointF world;
        if (!modelToWorld(point.x, point.y, &world))
            continue;
        const QString key = point.xyKey();
        const int index = byStation.value(key, -1);
        if (index < 0) {
            Marker marker;
            marker.widgetPos = worldToWidget(world);
            marker.pointIds.append(point.id);
            marker.hasDepth = point.hasWaterDepth();
            marker.tooltip = profileLabel(point, m_model->mode());
            byStation.insert(key, m_markers.size());
            m_markers.append(marker);
        } else {
            Marker &marker = m_markers[index];
            marker.pointIds.append(point.id);
            marker.hasDepth = marker.hasDepth || point.hasWaterDepth();
        }
    }
}

const MapView::Marker *MapView::markerAt(const QPointF &widgetPos) const
{
    for (const Marker &marker : m_markers) {
        if (QLineF(marker.widgetPos, widgetPos).length() <= kMarkerRadius + 3)
            return &marker;
    }
    return nullptr;
}

void MapView::fitToPoints()
{
    // The fit is only meaningful once the widget has its real geometry. Loading
    // a project or an example happens before the layout has run, and fitting
    // against a stub size picks a zoom several levels too far out, so defer to
    // the resize that follows.
    if (width() < kMinFitExtent || height() < kMinFitExtent) {
        m_pendingFit = true;
        return;
    }
    m_pendingFit = false;

    // Measured at zoom 0, so the extent can be compared against the widget
    // before a zoom is chosen; world coordinates scale by 2^zoom from here.
    constexpr double kUnitWorld = 256.0;

    QVector<QPointF> units;
    auto collect = [this, &units](double x, double y) {
        QPointF unit;
        if (modelToWorldAtSize(x, y, kUnitWorld, &unit))
            units.append(unit);
    };
    for (const MeasurementPoint &point : m_model->points())
        collect(point.x, point.y);
    // include the section ends, otherwise a survey whose stations sit on one
    // half of the tape is framed off to the side of the line that is drawn
    for (const CrossSection &section : m_model->crossSections()) {
        if (section.isValid()) {
            collect(section.leftX, section.leftY);
            collect(section.rightX, section.rightY);
        }
    }
    if (units.isEmpty())
        return;

    double minX = units.first().x(), maxX = minX;
    double minY = units.first().y(), maxY = minY;
    for (const QPointF &unit : units) {
        minX = qMin(minX, unit.x());
        maxX = qMax(maxX, unit.x());
        minY = qMin(minY, unit.y());
        maxY = qMax(maxY, unit.y());
    }

    // Choose the zoom before the centre: both the centre and everything else in
    // world coordinates depend on it. Without this the view only ever panned,
    // which left a survey a few metres wide as a dot at whatever zoom happened
    // to be current.
    const double availableW = qMax(64.0, width() - 2.0 * kFitMargin);
    const double availableH = qMax(64.0, height() - 2.0 * kFitMargin);
    const double spanX = maxX - minX;
    const double spanY = maxY - minY;
    if (spanX > 0.0 || spanY > 0.0) {
        const double scaleX = spanX > 0.0 ? availableW / spanX
                                          : std::numeric_limits<double>::max();
        const double scaleY = spanY > 0.0 ? availableH / spanY
                                          : std::numeric_limits<double>::max();
        const double scale = qMin(scaleX, scaleY);
        m_zoom = qBound(kMinZoom, int(std::floor(std::log2(scale))), kMaxZoom);
    } else {
        // a single station carries no extent; show it at a close, readable zoom
        m_zoom = qBound(kMinZoom, kSinglePointZoom, kMaxZoom);
    }

    const double factor = std::pow(2.0, m_zoom);
    m_center = QPointF((minX + maxX) / 2.0 * factor, (minY + maxY) / 2.0 * factor);
}

void MapView::centerOnWgs84(double longitude, double latitude, int zoom)
{
    m_zoom = qBound(kMinZoom, zoom, kMaxZoom);
    const double size = worldSize();
    const double sinLat =
        std::sin(qDegreesToRadians(qBound(-85.05112878, latitude, 85.05112878)));
    m_center = QPointF((longitude + 180.0) / 360.0 * size,
                       (0.5 - std::log((1.0 + sinLat) / (1.0 - sinLat)) / (4.0 * M_PI)) * size);
    rebuild();
}

void MapView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        SiteView::mousePressEvent(event);
        return;
    }
    m_dragging = true;
    m_dragMoved = false;
    m_dragStart = event->position();
    m_dragCenter = m_center;
}

void MapView::mouseMoveEvent(QMouseEvent *event)
{
    updateReadout(event->position());

    if (!m_dragging) {
        const Marker *marker = markerAt(event->position());
        setCursor(marker ? Qt::PointingHandCursor : Qt::CrossCursor);
        if (marker)
            setToolTip(marker->tooltip);
        else
            setToolTip(QString());
        return;
    }

    const QPointF delta = event->position() - m_dragStart;
    if (delta.manhattanLength() > kDragThreshold)
        m_dragMoved = true;
    if (m_dragMoved) {
        m_center = m_dragCenter - delta;
        rebuildMarkers();
        requestVisibleTiles();
        update();
    }
}

void MapView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_dragging) {
        SiteView::mouseReleaseEvent(event);
        return;
    }
    m_dragging = false;
    if (m_dragMoved)
        return; // that was a pan, not a click

    if (const Marker *marker = markerAt(event->position())) {
        // a station holds one point per depth; the first is the shallowest
        emit editPointRequested(marker->pointIds.first());
        return;
    }
    double x = 0.0;
    double y = 0.0;
    if (worldToModel(widgetToWorld(event->position()), &x, &y))
        emit newPointRequested(x, y);
}

void MapView::wheelEvent(QWheelEvent *event)
{
    const int steps = event->angleDelta().y() / 120;
    if (steps == 0)
        return;
    const int target = qBound(kMinZoom, m_zoom + steps, kMaxZoom);
    if (target == m_zoom)
        return;

    // keep the position under the cursor fixed while zooming
    // world coordinates scale by 2^(dz), so rescale the cursor position and
    // move the centre so that the same ground point stays under the pointer
    const QPointF cursorWorld = widgetToWorld(event->position());
    const double factor = std::pow(2.0, target - m_zoom);
    m_zoom = target;
    const QPointF scaledCursor = cursorWorld * factor;
    const QPointF widgetCenter(width() / 2.0, height() / 2.0);
    m_center = scaledCursor + widgetCenter - event->position();

    m_failed.clear();
    rebuild();
    event->accept();
}

void MapView::resizeEvent(QResizeEvent *event)
{
    SiteView::resizeEvent(event);
    if (m_pendingFit)
        fitToPoints(); // deferred from before the widget had its geometry
    rebuildMarkers();
    requestVisibleTiles();
}

void MapView::leaveEvent(QEvent *event)
{
    if (m_readout)
        m_readout->clear();
    SiteView::leaveEvent(event);
}

void MapView::updateReadout(const QPointF &widgetPos)
{
    if (!m_readout)
        return;
    double x = 0.0;
    double y = 0.0;
    if (!worldToModel(widgetToWorld(widgetPos), &x, &y)) {
        m_readout->clear();
        return;
    }
    const bool geographic = m_model->epsg() == 4326;
    m_readout->setText(tr("E %1  N %2  (EPSG:%3)")
                           .arg(x, 0, 'f', geographic ? 6 : 2)
                           .arg(y, 0, 'f', geographic ? 6 : 2)
                           .arg(m_model->epsg()));
}

// ---------------------------------------------------------------------------
// persistence
// ---------------------------------------------------------------------------

QJsonObject MapView::saveState() const
{
    const double size = worldSize();
    const double lon = m_center.x() / size * 360.0 - 180.0;
    const double n = M_PI - 2.0 * M_PI * m_center.y() / size;
    const double lat = qRadiansToDegrees(std::atan(std::sinh(n)));

    QJsonObject state;
    state[QStringLiteral("centerLon")] = lon;
    state[QStringLiteral("centerLat")] = lat;
    state[QStringLiteral("zoom")] = m_zoom;
    state[QStringLiteral("basemap")] = m_basemapEnabled;
    state[QStringLiteral("tileUrl")] = m_tileUrl;
    state[QStringLiteral("attribution")] = m_attribution;
    return state;
}

void MapView::restoreState(const QJsonObject &state)
{
    m_tileUrl = state[QStringLiteral("tileUrl")].toString(kDefaultTileUrl);
    m_attribution = state[QStringLiteral("attribution")].toString(kDefaultAttribution);
    m_basemapEnabled = state[QStringLiteral("basemap")].toBool(true);
    if (m_basemapCheck)
        m_basemapCheck->setChecked(m_basemapEnabled);

    if (state.contains(QStringLiteral("centerLat"))) {
        centerOnWgs84(state[QStringLiteral("centerLon")].toDouble(),
                      state[QStringLiteral("centerLat")].toDouble(),
                      state[QStringLiteral("zoom")].toInt(17));
    } else {
        fitToPoints();
        rebuild();
    }
}
