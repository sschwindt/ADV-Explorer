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

    /// Cover `target`, or hide the highlight when it is null or invisible.
    void follow(QWidget *target);
    /// Re-read the target geometry, for instance after a resize.
    void reposition();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPointer<QWidget> m_target;
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
    /// One step: what to say, which widget to point at, and which tab to show.
    struct Step {
        QString title;
        QString body;
        QPointer<QWidget> target;
        int tabIndex = -1; ///< tab to raise before showing the step, -1 to leave it
    };

    explicit GuidedTour(QWidget *parent = nullptr);

    void setSteps(const QList<Step> &steps);
    void start();

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
