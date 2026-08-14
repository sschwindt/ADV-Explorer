/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include <QJsonObject>
#include <QUuid>
#include <QWidget>

/// Common contract of the panel that shows where the measurement points are.
///
/// Two implementations exist and deliberately share no drawing code:
/// FlumeView draws a schematic flume and stretches its two axes independently
/// so the channel fills whatever space the splitter gives it, while MapView
/// draws a conformal web map where doing that would visibly shear the terrain.
/// What they do share is how the main window talks to them, which is all this
/// interface covers.
class SiteView : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;
    ~SiteView() override = default;

    /// View state persisted with the project (extent, zoom, basemap choice).
    virtual QJsonObject saveState() const = 0;
    virtual void restoreState(const QJsonObject &state) = 0;

signals:
    /// The user clicked empty space, at model coordinates in metres. In field
    /// mode these are eastings and northings in the project coordinate system.
    void newPointRequested(double x, double y);
    /// The user clicked an existing marker.
    void editPointRequested(const QUuid &pointId);

public slots:
    /// Rebuild from the model, e.g. after a point was added or removed.
    virtual void rebuild() = 0;
};
