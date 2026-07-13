/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include <QGraphicsView>
#include <QUuid>
#include <QWidget>

class QDoubleSpinBox;
class QGraphicsScene;

namespace adv {
class ProjectModel;
}

/// Interactive top view of the flume (aspect ratio 1:5, light blue) with the
/// coordinate origin at the center of the inlet: x downstream, y toward the
/// right bank (left bank negative), flow from left to right.
/// Clicking into the flume requests a new measurement point; clicking a
/// marker requests editing it. One circle marker is shown per z position;
/// markers turn dark blue once a water depth is defined.
class FlumeView : public QWidget
{
    Q_OBJECT
public:
    explicit FlumeView(adv::ProjectModel *model, QWidget *parent = nullptr);

    double flumeLength() const;
    double flumeWidth() const;
    void setFlumeSize(double length, double width);

signals:
    /// User clicked into empty flume space at flume coordinates (m).
    void newPointRequested(double x, double y);
    /// User clicked an existing marker.
    void editPointRequested(const QUuid &pointId);

public slots:
    void rebuild();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void drawFlume();
    void fitView();

    adv::ProjectModel *m_model;
    QGraphicsView *m_view;
    QGraphicsScene *m_scene;
    QDoubleSpinBox *m_lengthSpin;
    QDoubleSpinBox *m_widthSpin;
};
