/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "SiteView.h"

#include <QCache>
#include <QHash>
#include <QImage>
#include <QPointF>
#include <QSet>
#include <QString>

class QCheckBox;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;

namespace adv {
class ProjectModel;
}

/// Georeferenced site view for field campaigns: measurement stations drawn on a
/// slippy-map basemap.
///
/// Coordinates flow from the project system through WGS 84 to Web Mercator
/// pixels: (easting, northing) -> adv::crs::toWgs84 -> world pixels at the
/// current integer zoom -> widget pixels. Unlike FlumeView this mapping is
/// strictly conformal, with the same scale on both axes; stretching one axis to
/// fill the widget, as the flume view deliberately does, would shear the
/// terrain and misplace everything relative to the basemap. Do not copy that
/// pattern here.
///
/// Painting happens in paintEvent rather than through a QGraphicsScene so that
/// asynchronously arriving tiles simply trigger a repaint, and so that
/// exportPng() renders exactly what is on screen, attribution included.
///
/// Tiles come from an OpenStreetMap-compatible server. The OSM Foundation tile
/// usage policy is binding, so this class sends an identifying User-Agent,
/// keeps at most a couple of requests in flight, fetches only tiles that are
/// actually visible, caches to disk between sessions, and always paints the
/// "(c) OpenStreetMap contributors" attribution. Without a network the view
/// falls back to a plain coordinate grid and stays fully usable, which is also
/// what happens in the offscreen documentation build.
class MapView : public SiteView
{
    Q_OBJECT
public:
    explicit MapView(adv::ProjectModel *model, QWidget *parent = nullptr);
    ~MapView() override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

    /// Render the current view, attribution included, for the PNG export.
    QImage renderImage(const QSize &size, int dpi) const;

    /// Centre on a WGS 84 position, e.g. the handheld GPS fix of a survey.
    void centerOnWgs84(double longitude, double latitude, int zoom = 17);

public slots:
    void rebuild() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Marker {
        QPointF widgetPos;
        QList<QUuid> pointIds;
        bool hasDepth = false;
        QString tooltip;
    };

    // --- projection ---------------------------------------------------------
    /// Project coordinates to world pixels at the current zoom; returns false
    /// when no usable coordinate system is set.
    bool modelToWorld(double x, double y, QPointF *world) const;
    /// Same projection against an explicit world size, so an extent can be
    /// measured before the zoom that would define worldSize() is chosen.
    bool modelToWorldAtSize(double x, double y, double size, QPointF *world) const;
    bool worldToModel(const QPointF &world, double *x, double *y) const;
    QPointF worldToWidget(const QPointF &world) const;
    QPointF widgetToWorld(const QPointF &widget) const;
    double worldSize() const; ///< pixels spanned by the whole earth at this zoom
    /// Deepest tile level available, which may be shallower than the view zoom.
    int tileZoom() const;
    /// World pixels one tile covers at the current view zoom.
    double tileWorldSize() const;

    // --- tiles ---------------------------------------------------------------
    void requestVisibleTiles();
    void requestTile(int x, int y, int z);
    void tileFinished(QNetworkReply *reply);
    static QString tileKey(int x, int y, int z);

    void paintTiles(QPainter *painter) const;
    void paintGraticule(QPainter *painter) const;
    void paintCrossSections(QPainter *painter) const;
    void paintMarkers(QPainter *painter) const;
    void paintScaleBar(QPainter *painter) const;
    void paintAttribution(QPainter *painter) const;

    void rebuildMarkers();
    const Marker *markerAt(const QPointF &widgetPos) const;
    void fitToPoints();
    void updateReadout(const QPointF &widgetPos);

    adv::ProjectModel *m_model;

    /// Web Mercator world pixel at the centre of the widget.
    QPointF m_center;
    int m_zoom = 17;

    QNetworkAccessManager *m_network = nullptr;
    QCache<QString, QImage> m_tiles;
    QSet<QString> m_pending;
    QSet<QString> m_failed;
    int m_inFlight = 0;
    bool m_offline = false;   ///< set after a hard failure; stops retrying
    /// A fit was asked for before the widget had a geometry to fit against.
    bool m_pendingFit = false;
    bool m_tlsUnavailable = false; ///< this build cannot do HTTPS at all
    bool m_basemapEnabled = true;
    QString m_tileUrl;
    QString m_attribution;

    QVector<Marker> m_markers;
    QPointF m_dragStart;
    QPointF m_dragCenter;
    bool m_dragging = false;
    bool m_dragMoved = false;

    QCheckBox *m_basemapCheck = nullptr;
    QLabel *m_readout = nullptr;
    QLabel *m_status = nullptr;
};
