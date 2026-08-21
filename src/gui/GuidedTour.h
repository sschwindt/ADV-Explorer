/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "core/ProjectSettings.h"

#include <QDockWidget>
#include <QList>
#include <QPointer>
#include <QRect>
#include <QString>

class QLabel;
class QPushButton;

/// Translucent frame drawn over the widget the current tour step talks about.
///
/// It is a sibling overlay rather than a stylesheet on the target, because the
/// targets include QCustomPlot canvases and custom-painted views whose own
/// painting a stylesheet would disturb. It never takes input.
class TourHighlight : public QWidget
{
    Q_OBJECT
public:
    explicit TourHighlight(QWidget *parent);

    /// Cover `target` (or `rect` inside it) together with `extras`, or hide the
    /// highlight when nothing of that is visible.
    void follow(QWidget *target, const QList<QPointer<QWidget>> &extras = {},
                const QRect &rect = QRect());
    /// Re-read the target geometry, for instance after a resize.
    void reposition();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QRect mappedRect(QWidget *widget, const QRect &area) const;

    QPointer<QWidget> m_target;
    QList<QPointer<QWidget>> m_extras;
    QRect m_rect; ///< sub-area of m_target in its own coordinates, may be null
};

/// A dockable, non-modal walkthrough of the main functions.
///
/// The steps are built for the campaign mode that is active when the tour
/// starts, because what matters differs: a flume and TKE in the laboratory, a
/// map, a coordinate system and a TKE proxy in the field. The user keeps full
/// control of the window throughout; nothing here blocks input or changes the
/// project.
class GuidedTour : public QDockWidget
{
    Q_OBJECT
public:
    /// One step: what to say, which widgets to point at, and which tab to show.
    ///
    /// A step of a workflow tour usually points at the control the user is meant
    /// to click next. `extras` covers the neighbouring controls that belong to
    /// the same action, and `rect` narrows the highlight to one part of the
    /// target, which is how a single menu title is framed inside the menu bar.
    struct Step {
        QString title;
        QString body;
        QPointer<QWidget> target;
        QList<QPointer<QWidget>> extras;
        QRect rect;
        int tabIndex = -1; ///< tab to raise before showing the step, -1 to leave it
    };

    explicit GuidedTour(QWidget *parent = nullptr);

    void setSteps(const QList<Step> &steps);
    /// Show the tour, beginning at `index` (clamped into the step range).
    void start(int index = 0);

signals:
    /// Raise this tab before the step is shown.
    void tabRequested(int index);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void showStep(int index);
    void updateButtons();

    QList<Step> m_steps;
    int m_current = -1;
    QLabel *m_counter;
    QLabel *m_title;
    QLabel *m_body;
    QPushButton *m_back;
    QPushButton *m_next;
    TourHighlight *m_highlight = nullptr;
};
