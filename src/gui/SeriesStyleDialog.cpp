/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "SeriesStyleDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>

QJsonObject SeriesStyle::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("lineWidth")] = lineWidth;
    o[QStringLiteral("lineColor")] = lineColor.name();
    o[QStringLiteral("lineStyle")] = int(lineStyle);
    o[QStringLiteral("markerShape")] = markerShape;
    o[QStringLiteral("markerFilled")] = markerFilled;
    o[QStringLiteral("markerSize")] = markerSize;
    o[QStringLiteral("markerLineWidth")] = markerLineWidth;
    o[QStringLiteral("markerColor")] = markerColor.name();
    return o;
}

SeriesStyle SeriesStyle::fromJson(const QJsonObject &o)
{
    SeriesStyle s;
    s.lineWidth = o[QStringLiteral("lineWidth")].toDouble(1.5);
    s.lineColor = QColor(o[QStringLiteral("lineColor")].toString(s.lineColor.name()));
    s.lineStyle = Qt::PenStyle(o[QStringLiteral("lineStyle")].toInt(int(Qt::SolidLine)));
    s.markerShape = o[QStringLiteral("markerShape")].toInt(0);
    s.markerFilled = o[QStringLiteral("markerFilled")].toBool();
    s.markerSize = o[QStringLiteral("markerSize")].toDouble(6.0);
    s.markerLineWidth = o[QStringLiteral("markerLineWidth")].toDouble(1.0);
    s.markerColor = QColor(o[QStringLiteral("markerColor")].toString(s.markerColor.name()));
    return s;
}

SeriesStyleDialog::SeriesStyleDialog(const SeriesStyle &style, QWidget *parent)
    : QDialog(parent)
    , m_style(style)
{
    setWindowTitle(tr("Series style"));
    auto *form = new QFormLayout(this);

    m_lineWidthSpin = new QDoubleSpinBox(this);
    m_lineWidthSpin->setRange(0.0, 20.0);
    m_lineWidthSpin->setSingleStep(0.5);
    m_lineWidthSpin->setValue(m_style.lineWidth);
    form->addRow(tr("Line width:"), m_lineWidthSpin);

    m_lineStyleCombo = new QComboBox(this);
    m_lineStyleCombo->addItem(tr("solid"), int(Qt::SolidLine));
    m_lineStyleCombo->addItem(tr("dotted"), int(Qt::DotLine));
    m_lineStyleCombo->addItem(tr("dashed"), int(Qt::DashLine));
    m_lineStyleCombo->addItem(tr("dash-dot"), int(Qt::DashDotLine));
    m_lineStyleCombo->setCurrentIndex(
        m_lineStyleCombo->findData(int(m_style.lineStyle)));
    form->addRow(tr("Line style:"), m_lineStyleCombo);

    form->addRow(tr("Line color:"), makeColorButton(&m_style.lineColor));

    m_markerShapeCombo = new QComboBox(this);
    m_markerShapeCombo->addItems({tr("off"), tr("rectangular"), tr("circular"), tr("triangular")});
    m_markerShapeCombo->setCurrentIndex(m_style.markerShape);
    form->addRow(tr("Markers:"), m_markerShapeCombo);

    m_markerFilledCheck = new QCheckBox(tr("filled"), this);
    m_markerFilledCheck->setChecked(m_style.markerFilled);
    form->addRow(QString(), m_markerFilledCheck);

    m_markerSizeSpin = new QDoubleSpinBox(this);
    m_markerSizeSpin->setRange(1.0, 30.0);
    m_markerSizeSpin->setValue(m_style.markerSize);
    form->addRow(tr("Marker size:"), m_markerSizeSpin);

    m_markerLineWidthSpin = new QDoubleSpinBox(this);
    m_markerLineWidthSpin->setRange(0.5, 10.0);
    m_markerLineWidthSpin->setSingleStep(0.5);
    m_markerLineWidthSpin->setValue(m_style.markerLineWidth);
    form->addRow(tr("Marker line width:"), m_markerLineWidthSpin);

    form->addRow(tr("Marker color:"), makeColorButton(&m_style.markerColor));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

QPushButton *SeriesStyleDialog::makeColorButton(QColor *color)
{
    auto *button = new QPushButton(color->name(), this);
    button->setStyleSheet(QStringLiteral("background-color: %1").arg(color->name()));
    connect(button, &QPushButton::clicked, this, [this, button, color]() {
        const QColor chosen = QColorDialog::getColor(*color, this);
        if (chosen.isValid()) {
            *color = chosen;
            button->setText(chosen.name());
            button->setStyleSheet(QStringLiteral("background-color: %1").arg(chosen.name()));
        }
    });
    return button;
}

SeriesStyle SeriesStyleDialog::style() const
{
    SeriesStyle s = m_style;
    s.lineWidth = m_lineWidthSpin->value();
    s.lineStyle = Qt::PenStyle(m_lineStyleCombo->currentData().toInt());
    s.markerShape = m_markerShapeCombo->currentIndex();
    s.markerFilled = m_markerFilledCheck->isChecked();
    s.markerSize = m_markerSizeSpin->value();
    s.markerLineWidth = m_markerLineWidthSpin->value();
    return s;
}
