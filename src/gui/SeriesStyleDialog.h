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
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;

/// Visual style of one plotted data series (line and markers).
struct SeriesStyle {
    double lineWidth = 1.5;
    QColor lineColor = QColor(0, 114, 178); // Okabe-Ito blue
    Qt::PenStyle lineStyle = Qt::SolidLine; // solid, dotted, dashed, dash-dot
    int markerShape = 0;                    // 0 off, 1 square, 2 circle, 3 triangle
    bool markerFilled = false;
    double markerSize = 6.0;
    double markerLineWidth = 1.0;
    QColor markerColor = QColor(0, 114, 178);

    QJsonObject toJson() const;
    static SeriesStyle fromJson(const QJsonObject &json);
};

/// Dialog editing line width/color/style and marker shape/fill/size/color.
class SeriesStyleDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SeriesStyleDialog(const SeriesStyle &style, QWidget *parent = nullptr);

    SeriesStyle style() const;

private:
    QPushButton *makeColorButton(QColor *color);

    SeriesStyle m_style;
    QDoubleSpinBox *m_lineWidthSpin;
    QComboBox *m_lineStyleCombo;
    QComboBox *m_markerShapeCombo;
    QCheckBox *m_markerFilledCheck;
    QDoubleSpinBox *m_markerSizeSpin;
    QDoubleSpinBox *m_markerLineWidthSpin;
};
