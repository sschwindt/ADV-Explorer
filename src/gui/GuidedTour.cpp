/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "GuidedTour.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// TourHighlight
// ---------------------------------------------------------------------------

TourHighlight::TourHighlight(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    hide();
}

void TourHighlight::follow(QWidget *target)
{
    m_target = target;
    reposition();
}

void TourHighlight::reposition()
{
    QWidget *target = m_target.data();
    if (!target || !target->isVisible() || !parentWidget()) {
        hide();
        return;
    }

    // map through the shared window, so the highlight lands correctly no matter
    // how deeply the target is nested in splitters and tab widgets
    const QPoint topLeft =
        parentWidget()->mapFromGlobal(target->mapToGlobal(QPoint(0, 0)));
    QRect area(topLeft, target->size());
    area = area.intersected(parentWidget()->rect());
    if (!area.isValid() || area.isEmpty()) {
        hide();
        return;
    }

    // a couple of pixels of margin so the frame reads as around the widget
    setGeometry(area.adjusted(-3, -3, 3, 3).intersected(parentWidget()->rect()));
    raise();
    show();
    update();
}

void TourHighlight::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF frame = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
    painter.setPen(QPen(QColor(0, 114, 178), 3));
    painter.setBrush(QColor(0, 114, 178, 28));
    painter.drawRoundedRect(frame, 5, 5);
}

// ---------------------------------------------------------------------------
// GuidedTour
// ---------------------------------------------------------------------------

GuidedTour::GuidedTour(QWidget *parent)
    : QDockWidget(tr("Guided tour"), parent)
{
    setObjectName(QStringLiteral("guidedTour"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);

    auto *panel = new QWidget(this);
    // the main window paints a background image, which would otherwise show
    // through the dock and make the text hard to read
    panel->setAutoFillBackground(true);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);

    m_counter = new QLabel(panel);
    QFont counterFont = m_counter->font();
    counterFont.setPointSizeF(qMax(7.0, counterFont.pointSizeF() - 1.0));
    m_counter->setFont(counterFont);
    m_counter->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(m_counter);

    m_title = new QLabel(panel);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 1.0);
    m_title->setFont(titleFont);
    m_title->setWordWrap(true);
    layout->addWidget(m_title);

    m_body = new QLabel(panel);
    m_body->setWordWrap(true);
    m_body->setTextFormat(Qt::RichText);
    m_body->setOpenExternalLinks(true);
    m_body->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_body, 1);

    auto *buttons = new QHBoxLayout;
    m_back = new QPushButton(tr("< Back"), panel);
    m_next = new QPushButton(tr("Next >"), panel);
    m_next->setDefault(true);
    buttons->addWidget(m_back);
    buttons->addStretch();
    buttons->addWidget(m_next);
    layout->addLayout(buttons);

    connect(m_back, &QPushButton::clicked, this, [this] { showStep(m_current - 1); });
    connect(m_next, &QPushButton::clicked, this, [this] {
        if (m_current + 1 >= m_steps.size())
            close();
        else
            showStep(m_current + 1);
    });

    setWidget(panel);
    setMinimumWidth(260);

    // the highlight belongs to the window, not to the dock, so it can be drawn
    // over the views on the other side of the splitter
    if (parent) {
        m_highlight = new TourHighlight(parent);
        // the target moves whenever the window is resized or a splitter is
        // dragged, and the dock itself takes space away from the views
        parent->installEventFilter(this);
    }

    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (!visible && m_highlight)
            m_highlight->hide();
        else if (visible && m_highlight && m_current >= 0
                 && m_current < m_steps.size())
            m_highlight->follow(m_steps.at(m_current).target.data());
    });
}

void GuidedTour::setSteps(const QList<Step> &steps)
{
    m_steps = steps;
    m_current = -1;
}

void GuidedTour::start()
{
    if (m_steps.isEmpty())
        return;
    show();
    raise();
    showStep(0);
}

void GuidedTour::showStep(int index)
{
    if (index < 0 || index >= m_steps.size())
        return;
    m_current = index;
    const Step &step = m_steps.at(index);

    if (step.tabIndex >= 0)
        emit tabRequested(step.tabIndex);

    m_counter->setText(tr("Step %1 of %2").arg(index + 1).arg(m_steps.size()));
    m_title->setText(step.title);
    m_body->setText(step.body);
    updateButtons();

    if (m_highlight) {
        m_highlight->follow(step.target.data());
        // raising a tab relayouts its page, so the geometry read above can be
        // the pre-layout one; settle it once the event loop has caught up
        QTimer::singleShot(0, m_highlight, &TourHighlight::reposition);
    }
}

bool GuidedTour::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parent() && m_highlight && m_highlight->isVisible()
        && (event->type() == QEvent::Resize || event->type() == QEvent::Move
            || event->type() == QEvent::LayoutRequest)) {
        QTimer::singleShot(0, m_highlight, &TourHighlight::reposition);
    }
    return QDockWidget::eventFilter(watched, event);
}

void GuidedTour::updateButtons()
{
    m_back->setEnabled(m_current > 0);
    m_next->setText(m_current + 1 >= m_steps.size() ? tr("Finish") : tr("Next >"));
}

void GuidedTour::resizeEvent(QResizeEvent *event)
{
    QDockWidget::resizeEvent(event);
    // the dock taking space reflows the views it points at
    if (m_highlight)
        m_highlight->reposition();
}
