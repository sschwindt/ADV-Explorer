/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "SiteView.h"

#include <QGraphicsView>
#include <QUuid>

class QDoubleSpinBox;
class QGraphicsScene;

namespace adv {
class ProjectModel;
}

/// Interactive top view of the flume (light blue) with the coordinate origin
/// at the center of the inlet: x downstream, y toward the right bank (left
/// bank negative), flow from left to right. The drawing always fills the
/// available widget area: the flume length spans the full view width and the
/// flume width is vertically exaggerated to fill the view height.
/// Clicking into the flume requests a new measurement point; clicking a
/// marker requests editing it. One circle marker is shown per z position;
/// markers turn dark blue once a water depth is defined.
class FlumeView : public SiteView
{
    Q_OBJECT
public:
    explicit FlumeView(adv::ProjectModel *model, QWidget *parent = nullptr);

    double flumeLength() const;
    double flumeWidth() const;
    void setFlumeSize(double length, double width);

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

public slots:
    void rebuild() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void drawFlume();
    void redrawScene();
    void fitView();
    QPointF toScene(double x, double y) const;
    QPointF fromScene(const QPointF &scenePos) const;
    double xMargin() const;

    adv::ProjectModel *m_model;
    QGraphicsView *m_view;
    QGraphicsScene *m_scene;
    QDoubleSpinBox *m_lengthSpin;
    QDoubleSpinBox *m_widthSpin;
    double m_yStretch = 1.0; // vertical exaggeration so the flume fills the view
};
