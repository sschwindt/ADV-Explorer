/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "PlotOptionsDialog.h"

#include <qcustomplot.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QPushButton>

void PlotOptions::applyTo(QCustomPlot *plot) const
{
    QFont labelFont = QGuiApplication::font();
    if (!fontFamily.isEmpty())
        labelFont.setFamily(fontFamily);
    QFont tickFont = labelFont;
    labelFont.setPointSizeF(labelFontSize);
    tickFont.setPointSizeF(tickFontSize);

    const QPen gridPen(gridColor, gridWidth, gridStyle);
    for (QCPAxis *axis : {plot->xAxis, plot->yAxis}) {
        axis->setLabelFont(labelFont);
        axis->setTickLabelFont(tickFont);
        axis->setTicks(ticksVisible);
        axis->grid()->setVisible(gridVisible);
        axis->grid()->setPen(gridPen);
    }
    if (plot->legend)
        plot->legend->setFont(tickFont);
}

QJsonObject PlotOptions::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("fontFamily")] = fontFamily;
    o[QStringLiteral("labelFontSize")] = labelFontSize;
    o[QStringLiteral("tickFontSize")] = tickFontSize;
    o[QStringLiteral("ticksVisible")] = ticksVisible;
    o[QStringLiteral("gridVisible")] = gridVisible;
    o[QStringLiteral("gridStyle")] = int(gridStyle);
    o[QStringLiteral("gridWidth")] = gridWidth;
    o[QStringLiteral("gridColor")] = gridColor.name();
    return o;
}

PlotOptions PlotOptions::fromJson(const QJsonObject &o)
{
    PlotOptions p;
    p.fontFamily = o[QStringLiteral("fontFamily")].toString();
    p.labelFontSize = o[QStringLiteral("labelFontSize")].toDouble(11.0);
    p.tickFontSize = o[QStringLiteral("tickFontSize")].toDouble(9.0);
    p.ticksVisible = o[QStringLiteral("ticksVisible")].toBool(true);
    p.gridVisible = o[QStringLiteral("gridVisible")].toBool(true);
    p.gridStyle = Qt::PenStyle(o[QStringLiteral("gridStyle")].toInt(int(Qt::DotLine)));
    p.gridWidth = o[QStringLiteral("gridWidth")].toDouble(1.0);
    p.gridColor = QColor(o[QStringLiteral("gridColor")].toString(p.gridColor.name()));
    return p;
}

PlotOptionsDialog::PlotOptionsDialog(const PlotOptions &options, QWidget *parent)
    : QDialog(parent)
    , m_options(options)
{
    setWindowTitle(tr("Plot options"));
    auto *form = new QFormLayout(this);

    m_fontCombo = new QFontComboBox(this);
    if (!m_options.fontFamily.isEmpty())
        m_fontCombo->setCurrentFont(QFont(m_options.fontFamily));
    else
        m_fontCombo->setCurrentFont(QGuiApplication::font());
    form->addRow(tr("Axis font:"), m_fontCombo);

    m_labelSizeSpin = new QDoubleSpinBox(this);
    m_labelSizeSpin->setRange(4.0, 48.0);
    m_labelSizeSpin->setSuffix(QStringLiteral(" pt"));
    m_labelSizeSpin->setValue(m_options.labelFontSize);
    form->addRow(tr("Axis label size:"), m_labelSizeSpin);

    m_tickSizeSpin = new QDoubleSpinBox(this);
    m_tickSizeSpin->setRange(4.0, 48.0);
    m_tickSizeSpin->setSuffix(QStringLiteral(" pt"));
    m_tickSizeSpin->setValue(m_options.tickFontSize);
    form->addRow(tr("Tick label size:"), m_tickSizeSpin);

    m_ticksCheck = new QCheckBox(tr("show tick marks"), this);
    m_ticksCheck->setChecked(m_options.ticksVisible);
    form->addRow(tr("Ticks:"), m_ticksCheck);

    m_gridCheck = new QCheckBox(tr("show grid"), this);
    m_gridCheck->setChecked(m_options.gridVisible);
    form->addRow(tr("Grid:"), m_gridCheck);

    m_gridStyleCombo = new QComboBox(this);
    m_gridStyleCombo->addItem(tr("solid"), int(Qt::SolidLine));
    m_gridStyleCombo->addItem(tr("dotted"), int(Qt::DotLine));
    m_gridStyleCombo->addItem(tr("dashed"), int(Qt::DashLine));
    m_gridStyleCombo->addItem(tr("dash-dot"), int(Qt::DashDotLine));
    m_gridStyleCombo->setCurrentIndex(
        m_gridStyleCombo->findData(int(m_options.gridStyle)));
    form->addRow(tr("Grid style:"), m_gridStyleCombo);

    m_gridWidthSpin = new QDoubleSpinBox(this);
    m_gridWidthSpin->setRange(0.5, 10.0);
    m_gridWidthSpin->setSingleStep(0.5);
    m_gridWidthSpin->setValue(m_options.gridWidth);
    form->addRow(tr("Grid line width:"), m_gridWidthSpin);

    m_gridColorButton = new QPushButton(m_options.gridColor.name(), this);
    m_gridColorButton->setStyleSheet(
        QStringLiteral("background-color: %1").arg(m_options.gridColor.name()));
    connect(m_gridColorButton, &QPushButton::clicked, this, [this]() {
        const QColor chosen = QColorDialog::getColor(m_options.gridColor, this);
        if (chosen.isValid()) {
            m_options.gridColor = chosen;
            m_gridColorButton->setText(chosen.name());
            m_gridColorButton->setStyleSheet(
                QStringLiteral("background-color: %1").arg(chosen.name()));
        }
    });
    form->addRow(tr("Grid color:"), m_gridColorButton);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

PlotOptions PlotOptionsDialog::options() const
{
    PlotOptions o = m_options;
    o.fontFamily = m_fontCombo->currentFont().family();
    o.labelFontSize = m_labelSizeSpin->value();
    o.tickFontSize = m_tickSizeSpin->value();
    o.ticksVisible = m_ticksCheck->isChecked();
    o.gridVisible = m_gridCheck->isChecked();
    o.gridStyle = Qt::PenStyle(m_gridStyleCombo->currentData().toInt());
    o.gridWidth = m_gridWidthSpin->value();
    return o;
}
