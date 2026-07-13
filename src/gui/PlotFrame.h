/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "PlotOptionsDialog.h"
#include "SeriesStyleDialog.h"

#include <QJsonObject>
#include <QUuid>
#include <QWidget>

class QComboBox;
class QCustomPlot;

namespace adv {
class ProjectModel;
}

/// Time-series plot frame: superposes any data columns of any measurement
/// points, with color-blind friendly palettes and per-series line/marker
/// styling. Multiple frames can be stacked in the main window.
class PlotFrame : public QWidget
{
    Q_OBJECT
public:
    explicit PlotFrame(adv::ProjectModel *model, QWidget *parent = nullptr);

    /// Export the frame as PNG exactly as displayed, at the given resolution.
    bool exportPng(const QString &filePath, int dpi = 300);

    /// Currently plotted series as (label, time, values) triples for CSV export.
    struct ExportSeries {
        QString label;
        QVector<double> time;
        QVector<double> values;
    };
    QList<ExportSeries> currentSeries() const;

    QJsonObject saveState() const;
    void restoreState(const QJsonObject &state);

public slots:
    /// Re-read model data (called when points change).
    void refresh();

private slots:
    void addSeries();
    void removeSelectedSeries();
    void editSelectedStyle();
    void editPlotOptions();
    void pointSelectionChanged();
    void applyPalette();

private:
    struct Series {
        QUuid pointId;
        QString column; ///< column name or the derived TKE series
        SeriesStyle style;
    };

    void rebuildPlot();
    void rebuildPointCombo();
    void rebuildColumnCombo();
    void rebuildSeriesCombo();
    bool seriesData(const Series &series, QVector<double> *t, QVector<double> *y) const;
    QString seriesLabel(const Series &series) const;
    QList<QColor> paletteColors() const;

    adv::ProjectModel *m_model;
    QCustomPlot *m_plot;
    QComboBox *m_pointCombo;
    QComboBox *m_columnCombo;
    QComboBox *m_seriesCombo;
    QComboBox *m_paletteCombo;
    QList<Series> m_series;
    PlotOptions m_plotOptions;
};
