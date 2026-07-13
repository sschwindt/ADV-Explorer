/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "PlotOptionsDialog.h"

#include <QJsonObject>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QCustomPlot;
class QPlainTextEdit;
class QRadioButton;

namespace adv {
class ProjectModel;
}

/// Vertical profile frame: plots mean U, V, W of all points of one x-y
/// profile against z or z/h, shows profile statistics (mean, std, skewness,
/// kurtosis, stresses, TKE, dissipation) in a legend panel and provides the
/// heading/pitch/roll probe-alignment correction.
class ProfileFrame : public QWidget
{
    Q_OBJECT
public:
    explicit ProfileFrame(adv::ProjectModel *model, QWidget *parent = nullptr);

    bool exportPng(const QString &filePath, int dpi = 300);

    QJsonObject saveState() const;
    void restoreState(const QJsonObject &state);

public slots:
    void refresh();

private slots:
    void rebuildPlot();
    void openCorrectionDialog();
    void editPlotOptions();

private:
    QString currentProfileKey() const;

    adv::ProjectModel *m_model;
    QCustomPlot *m_plot;
    QComboBox *m_profileCombo;
    QCheckBox *m_uCheck;
    QCheckBox *m_vCheck;
    QCheckBox *m_wCheck;
    QRadioButton *m_zRadio;
    QRadioButton *m_zhRadio;
    QPlainTextEdit *m_statsPanel;
    PlotOptions m_plotOptions;
};
