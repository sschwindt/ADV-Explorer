/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include <QColor>
#include <QDialog>
#include <QJsonObject>

class QCheckBox;
class QComboBox;
class QCustomPlot;
class QDoubleSpinBox;
class QFontComboBox;
class QPushButton;

/// Axis and grid appearance of one plot frame. Applied live, so PNG exports
/// render exactly as styled.
struct PlotOptions {
    QString fontFamily;             ///< empty = application default font
    double labelFontSize = 11.0;    ///< axis label (title) size in points
    double tickFontSize = 9.0;      ///< tick number size in points
    bool ticksVisible = true;       ///< draw tick marks on the axes
    bool gridVisible = true;
    Qt::PenStyle gridStyle = Qt::DotLine;
    double gridWidth = 1.0;
    QColor gridColor = QColor(200, 200, 200);

    void applyTo(QCustomPlot *plot) const;
    QJsonObject toJson() const;
    static PlotOptions fromJson(const QJsonObject &json);
};

/// Dialog editing axis label/tick fonts (size and system-available families)
/// and the tick/grid style of a plot frame.
class PlotOptionsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PlotOptionsDialog(const PlotOptions &options, QWidget *parent = nullptr);

    PlotOptions options() const;

private:
    PlotOptions m_options;
    QFontComboBox *m_fontCombo;
    QDoubleSpinBox *m_labelSizeSpin;
    QDoubleSpinBox *m_tickSizeSpin;
    QCheckBox *m_ticksCheck;
    QCheckBox *m_gridCheck;
    QComboBox *m_gridStyleCombo;
    QDoubleSpinBox *m_gridWidthSpin;
    QPushButton *m_gridColorButton;
};
