/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "FlumeView.h"

#include "core/ProjectModel.h"

#include <QDoubleSpinBox>
#include <QGraphicsEllipseItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>

using adv::MeasurementPoint;
using adv::ProjectModel;

namespace {

// scene units: 1 m = 100 scene units; y axis flipped (screen y is down)
constexpr double kScale = 100.0;
constexpr double kMarkerRadius = 5.0; // scene units

QPointF toScene(double x, double y)
{
    return QPointF(x * kScale, -y * kScale);
}

QPointF fromScene(const QPointF &scenePos)
{
    return QPointF(scenePos.x() / kScale, -scenePos.y() / kScale);
}

} // namespace

FlumeView::FlumeView(ProjectModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
    , m_view(new QGraphicsView(this))
    , m_scene(new QGraphicsScene(this))
{
    auto *controls = new QHBoxLayout;
    controls->setContentsMargins(4, 4, 4, 0);
    controls->addWidget(new QLabel(tr("Flume length (m):")));
    m_lengthSpin = new QDoubleSpinBox(this);
    m_lengthSpin->setRange(0.5, 500.0);
    m_lengthSpin->setDecimals(2);
    m_lengthSpin->setValue(5.0);
    controls->addWidget(m_lengthSpin);
    controls->addWidget(new QLabel(tr("width (m):")));
    m_widthSpin = new QDoubleSpinBox(this);
    m_widthSpin->setRange(0.1, 100.0);
    m_widthSpin->setDecimals(2);
    m_widthSpin->setValue(1.0);
    controls->addWidget(m_widthSpin);
    controls->addWidget(new QLabel(tr("Click into the flume to add a measurement point; "
                                      "click a marker to edit it.")));
    controls->addStretch();

    m_view->setScene(m_scene);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setBackgroundBrush(palette().window());
    m_view->viewport()->installEventFilter(this);
    m_view->viewport()->setCursor(Qt::CrossCursor);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(controls);
    layout->addWidget(m_view, 1);

    connect(m_lengthSpin, &QDoubleSpinBox::valueChanged, this, &FlumeView::rebuild);
    connect(m_widthSpin, &QDoubleSpinBox::valueChanged, this, &FlumeView::rebuild);
    connect(m_model, &ProjectModel::pointAdded, this, &FlumeView::rebuild);
    connect(m_model, &ProjectModel::pointChanged, this, &FlumeView::rebuild);
    connect(m_model, &ProjectModel::pointRemoved, this, &FlumeView::rebuild);
    connect(m_model, &ProjectModel::modelReset, this, &FlumeView::rebuild);

    rebuild();
}

double FlumeView::flumeLength() const
{
    return m_lengthSpin->value();
}

double FlumeView::flumeWidth() const
{
    return m_widthSpin->value();
}

void FlumeView::setFlumeSize(double length, double width)
{
    m_lengthSpin->setValue(length);
    m_widthSpin->setValue(width);
}

void FlumeView::drawFlume()
{
    const double length = flumeLength();
    const double width = flumeWidth();
    const QRectF flumeRect(toScene(0.0, width / 2.0),
                           toScene(length, -width / 2.0));

    // light blue water surface
    m_scene->addRect(flumeRect, QPen(QColor(70, 100, 140), 2.0),
                     QBrush(QColor(190, 222, 240)));

    // flow direction arrow (background, translucent)
    const double ay = 0.0;
    const double x0 = 0.12 * length, x1 = 0.88 * length;
    const double head = 0.05 * length;
    const double shaft = width * 0.06;
    QPolygonF arrow;
    arrow << toScene(x0, ay + shaft) << toScene(x1 - head, ay + shaft)
          << toScene(x1 - head, ay + 2.5 * shaft) << toScene(x1, ay)
          << toScene(x1 - head, ay - 2.5 * shaft) << toScene(x1 - head, ay - shaft)
          << toScene(x0, ay - shaft);
    auto *arrowItem = m_scene->addPolygon(arrow, QPen(Qt::NoPen),
                                          QBrush(QColor(255, 255, 255, 130)));
    arrowItem->setZValue(1);
    auto *flowLabel = m_scene->addSimpleText(tr("flow direction"));
    flowLabel->setBrush(QColor(70, 100, 140));
    QFont labelFont = flowLabel->font();
    labelFont.setPointSizeF(width * kScale * 0.045);
    flowLabel->setFont(labelFont);
    flowLabel->setPos(toScene(length * 0.42, -width * 0.05));
    flowLabel->setZValue(1);

    // coordinate origin at the center of the inlet (x=0, y=0)
    const QPen axisPen(QColor(160, 40, 40), 1.5);
    const double tick = width * 0.08;
    m_scene->addLine(QLineF(toScene(-tick, 0.0), toScene(3.0 * tick, 0.0)), axisPen)->setZValue(2);
    m_scene->addLine(QLineF(toScene(0.0, -tick), toScene(0.0, 3.0 * tick)), axisPen)->setZValue(2);
    auto *xLabel = m_scene->addSimpleText(QStringLiteral("x"));
    xLabel->setBrush(axisPen.color());
    xLabel->setFont(labelFont);
    xLabel->setPos(toScene(3.2 * tick, 0.0));
    xLabel->setZValue(2);
    auto *yLabel = m_scene->addSimpleText(QStringLiteral("y"));
    yLabel->setBrush(axisPen.color());
    yLabel->setFont(labelFont);
    yLabel->setPos(toScene(0.01 * length, 3.2 * tick + 0.06 * width));
    yLabel->setZValue(2);

    // bank annotations: the scene shows y pointing up, so the orographic
    // left bank (y < 0) appears at the bottom, the right bank (y > 0) on top
    auto *leftBank = m_scene->addSimpleText(tr("left bank (y < 0)"));
    auto *rightBank = m_scene->addSimpleText(tr("right bank (y > 0)"));
    for (QGraphicsSimpleTextItem *item : {leftBank, rightBank}) {
        item->setBrush(QColor(70, 100, 140));
        item->setFont(labelFont);
        item->setZValue(1);
    }
    leftBank->setPos(toScene(0.02 * length, -width / 2.0 + 0.02 * width)
                     - QPointF(0, leftBank->boundingRect().height()));
    rightBank->setPos(toScene(0.02 * length, width / 2.0 - 0.02 * width));
}

void FlumeView::rebuild()
{
    m_scene->clear();
    drawFlume();

    // one marker per z position; same-xy markers get a small horizontal offset
    QHash<QString, int> xyCount;
    for (const MeasurementPoint &point : m_model->points()) {
        const int index = xyCount[point.xyKey()]++;
        const QPointF center = toScene(point.x, point.y)
                               + QPointF(index * 2.5 * kMarkerRadius, 0.0);
        auto *marker = m_scene->addEllipse(
            QRectF(center - QPointF(kMarkerRadius, kMarkerRadius),
                   QSizeF(2.0 * kMarkerRadius, 2.0 * kMarkerRadius)),
            QPen(Qt::black, 1.0),
            QBrush(point.hasWaterDepth() ? QColor(20, 50, 120)      // dark blue
                                         : QColor(90, 90, 90)));    // dark gray
        marker->setZValue(3);
        marker->setData(0, point.id.toString());
        marker->setToolTip(QStringLiteral("%1\n%2%3")
                               .arg(point.label(), point.data.sourceFileName(),
                                    point.hasWaterDepth()
                                        ? QStringLiteral("\nh=%1 m").arg(point.waterDepth)
                                        : QString()));
        marker->setCursor(Qt::PointingHandCursor);
    }

    fitView();
}

void FlumeView::fitView()
{
    const double margin = 0.06 * flumeLength() * kScale;
    QRectF rect(toScene(0.0, flumeWidth() / 2.0), toScene(flumeLength(), -flumeWidth() / 2.0));
    m_view->setSceneRect(rect.adjusted(-margin, -margin, margin, margin));
    m_view->fitInView(m_view->sceneRect(), Qt::KeepAspectRatio);
}

bool FlumeView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_view->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                const QPointF scenePos = m_view->mapToScene(mouseEvent->pos());
                // clicked on a marker?
                for (QGraphicsItem *item : m_scene->items(scenePos)) {
                    const QString id = item->data(0).toString();
                    if (!id.isEmpty()) {
                        emit editPointRequested(QUuid::fromString(id));
                        return true;
                    }
                }
                // clicked into the flume?
                const QPointF flumePos = fromScene(scenePos);
                if (flumePos.x() >= 0.0 && flumePos.x() <= flumeLength()
                    && std::fabs(flumePos.y()) <= flumeWidth() / 2.0) {
                    emit newPointRequested(flumePos.x(), flumePos.y());
                    return true;
                }
            }
        } else if (event->type() == QEvent::Resize) {
            fitView();
        }
    }
    return QWidget::eventFilter(watched, event);
}
