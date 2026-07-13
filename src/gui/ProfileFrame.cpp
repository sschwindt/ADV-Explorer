/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "ProfileFrame.h"

#include "core/ProjectModel.h"

#include <qcustomplot.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>

using namespace adv;

namespace {

/// Dialog proposing/applying heading, pitch and roll corrections (degrees).
class CorrectionDialog : public QDialog
{
public:
    CorrectionDialog(const RotationAngles &current, const RotationAngles &proposed,
                     QWidget *parent)
        : QDialog(parent)
        , m_proposed(proposed)
    {
        setWindowTitle(tr("Probe alignment correction"));
        auto *form = new QFormLayout(this);
        form->addRow(new QLabel(
            tr("Rotate the velocity coordinate system of this x-y profile to "
               "compensate probe misalignment. The proposed angles zero the "
               "mean transverse (V) and vertical (W) velocities and the "
               "residual v'w' coupling. Adjust manually if needed.")));

        auto makeSpin = [this](double value) {
            auto *spin = new QDoubleSpinBox(this);
            spin->setRange(-180.0, 180.0);
            spin->setDecimals(3);
            spin->setSuffix(QStringLiteral(" deg"));
            spin->setValue(value);
            return spin;
        };
        m_headingSpin = makeSpin(qRadiansToDegrees(current.heading));
        m_pitchSpin = makeSpin(qRadiansToDegrees(current.pitch));
        m_rollSpin = makeSpin(qRadiansToDegrees(current.roll));
        form->addRow(tr("Heading (about z, zeroes mean V):"), m_headingSpin);
        form->addRow(tr("Pitch (about y, zeroes mean W):"), m_pitchSpin);
        form->addRow(tr("Roll (about x, zeroes v'w'):"), m_rollSpin);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        auto *proposeButton = buttons->addButton(tr("Propose"), QDialogButtonBox::ActionRole);
        auto *resetButton = buttons->addButton(tr("Set to zero"), QDialogButtonBox::ResetRole);
        connect(proposeButton, &QPushButton::clicked, this, [this]() {
            m_headingSpin->setValue(qRadiansToDegrees(m_proposed.heading));
            m_pitchSpin->setValue(qRadiansToDegrees(m_proposed.pitch));
            m_rollSpin->setValue(qRadiansToDegrees(m_proposed.roll));
        });
        connect(resetButton, &QPushButton::clicked, this, [this]() {
            m_headingSpin->setValue(0.0);
            m_pitchSpin->setValue(0.0);
            m_rollSpin->setValue(0.0);
        });
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    RotationAngles angles() const
    {
        RotationAngles a;
        a.heading = qDegreesToRadians(m_headingSpin->value());
        a.pitch = qDegreesToRadians(m_pitchSpin->value());
        a.roll = qDegreesToRadians(m_rollSpin->value());
        return a;
    }

private:
    RotationAngles m_proposed;
    QDoubleSpinBox *m_headingSpin;
    QDoubleSpinBox *m_pitchSpin;
    QDoubleSpinBox *m_rollSpin;
};

QString formatNumber(double value, int precision = 5)
{
    return std::isfinite(value) ? QString::number(value, 'g', precision)
                                : QStringLiteral("nan");
}

} // namespace

ProfileFrame::ProfileFrame(ProjectModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
    , m_plot(new QCustomPlot(this))
{
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(4, 4, 4, 0);
    toolbar->addWidget(new QLabel(tr("Profile (x|y):")));
    m_profileCombo = new QComboBox(this);
    m_profileCombo->setMinimumWidth(160);
    toolbar->addWidget(m_profileCombo);

    m_uCheck = new QCheckBox(QStringLiteral("U"), this);
    m_uCheck->setChecked(true);
    m_vCheck = new QCheckBox(QStringLiteral("V"), this);
    m_vCheck->setChecked(true);
    m_wCheck = new QCheckBox(QStringLiteral("W"), this);
    m_wCheck->setChecked(true);
    toolbar->addWidget(m_uCheck);
    toolbar->addWidget(m_vCheck);
    toolbar->addWidget(m_wCheck);

    toolbar->addSpacing(12);
    m_zRadio = new QRadioButton(tr("z (m)"), this);
    m_zRadio->setChecked(true);
    m_zhRadio = new QRadioButton(tr("z/h (-)"), this);
    toolbar->addWidget(m_zRadio);
    toolbar->addWidget(m_zhRadio);

    toolbar->addSpacing(12);
    auto *correctionButton = new QPushButton(tr("Probe alignment..."), this);
    toolbar->addWidget(correctionButton);
    toolbar->addStretch();

    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->legend->setVisible(true);
    m_plot->xAxis->setLabel(tr("velocity (m/s)"));
    m_plot->yAxis->setLabel(tr("z (m)"));

    // top-right statistics legend panel
    m_statsPanel = new QPlainTextEdit(this);
    m_statsPanel->setReadOnly(true);
    QFont mono = m_statsPanel->font();
    mono.setFamily(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::TypeWriter);
    m_statsPanel->setFont(mono);
    m_statsPanel->setMinimumWidth(320);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_plot);
    splitter->addWidget(m_statsPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(toolbar);
    layout->addWidget(splitter, 1);

    connect(m_profileCombo, &QComboBox::currentIndexChanged, this, &ProfileFrame::rebuildPlot);
    for (QCheckBox *check : {m_uCheck, m_vCheck, m_wCheck})
        connect(check, &QCheckBox::toggled, this, &ProfileFrame::rebuildPlot);
    connect(m_zRadio, &QRadioButton::toggled, this, &ProfileFrame::rebuildPlot);
    connect(correctionButton, &QPushButton::clicked, this, &ProfileFrame::openCorrectionDialog);

    connect(m_model, &ProjectModel::pointAdded, this, &ProfileFrame::refresh);
    connect(m_model, &ProjectModel::pointChanged, this, &ProfileFrame::refresh);
    connect(m_model, &ProjectModel::pointRemoved, this, &ProfileFrame::refresh);
    connect(m_model, &ProjectModel::correctionChanged, this, &ProfileFrame::refresh);
    connect(m_model, &ProjectModel::modelReset, this, &ProfileFrame::refresh);

    refresh();
}

QString ProfileFrame::currentProfileKey() const
{
    return m_profileCombo->currentText();
}

void ProfileFrame::refresh()
{
    const QString current = currentProfileKey();
    m_profileCombo->blockSignals(true);
    m_profileCombo->clear();
    m_profileCombo->addItems(m_model->profileKeys());
    const int index = m_profileCombo->findText(current);
    m_profileCombo->setCurrentIndex(index >= 0 ? index : 0);
    m_profileCombo->blockSignals(false);
    rebuildPlot();
}

void ProfileFrame::rebuildPlot()
{
    m_plot->clearGraphs();
    m_statsPanel->clear();

    const QString key = currentProfileKey();
    const QList<QUuid> ids = m_model->profilePoints(key);
    if (ids.isEmpty()) {
        m_plot->replot();
        return;
    }

    const bool relative = m_zhRadio->isChecked();
    m_plot->yAxis->setLabel(relative ? tr("z/h (-)") : tr("z (m)"));

    struct Component {
        QCheckBox *check;
        QColor color;
        QString name;
    };
    // Okabe-Ito colors
    const QList<Component> components = {
        {m_uCheck, QColor("#0072B2"), QStringLiteral("U")},
        {m_vCheck, QColor("#E69F00"), QStringLiteral("V")},
        {m_wCheck, QColor("#009E73"), QStringLiteral("W")},
    };

    QVector<double> zValues;
    QVector<QVector<double>> means(3);
    QString statsText;
    statsText += tr("Profile %1\n").arg(key);
    const RotationAngles correction = m_model->correction(key);
    if (!correction.isIdentity()) {
        statsText += tr("correction: heading=%1 pitch=%2 roll=%3 deg\n")
                         .arg(qRadiansToDegrees(correction.heading), 0, 'f', 3)
                         .arg(qRadiansToDegrees(correction.pitch), 0, 'f', 3)
                         .arg(qRadiansToDegrees(correction.roll), 0, 'f', 3);
    }
    statsText += QStringLiteral("\n");

    for (const QUuid &id : ids) {
        const MeasurementPoint *point = m_model->point(id);
        const auto series = m_model->processed(id);
        if (!point || !series || !series->isValid())
            continue;
        double zPlot = point->z;
        if (relative) {
            if (!point->hasWaterDepth())
                continue; // z/h requires a water depth
            zPlot = point->z / point->waterDepth;
        }
        zValues.append(zPlot);
        const PointStats &s = series->stats;
        means[0].append(s.u.mean);
        means[1].append(s.v.mean);
        means[2].append(s.w.mean);

        statsText += tr("z=%1 m%2\n")
                         .arg(point->z, 0, 'f', 4)
                         .arg(point->hasWaterDepth()
                                  ? tr("  (z/h=%1)").arg(point->z / point->waterDepth, 0, 'f', 3)
                                  : QString());
        statsText += tr("  mean  u=%1 v=%2 w=%3 m/s\n")
                         .arg(formatNumber(s.u.mean), formatNumber(s.v.mean), formatNumber(s.w.mean));
        statsText += tr("  std   u=%1 v=%2 w=%3 m/s\n")
                         .arg(formatNumber(s.u.std), formatNumber(s.v.std), formatNumber(s.w.std));
        statsText += tr("  skew  u=%1 v=%2 w=%3\n")
                         .arg(formatNumber(s.u.skewness), formatNumber(s.v.skewness),
                              formatNumber(s.w.skewness));
        statsText += tr("  kurt  u=%1 v=%2 w=%3\n")
                         .arg(formatNumber(s.u.kurtosis), formatNumber(s.v.kurtosis),
                              formatNumber(s.w.kurtosis));
        statsText += tr("  u'v'=%1 u'w'=%2 v'w'=%3 m^2/s^2\n")
                         .arg(formatNumber(s.uv), formatNumber(s.uw), formatNumber(s.vw));
        statsText += tr("  TKE=%1 m^2/s^2  eps=%2 m^2/s^3\n\n")
                         .arg(formatNumber(s.tke), formatNumber(s.eps));
    }

    for (int c = 0; c < components.size(); ++c) {
        if (!components.at(c).check->isChecked() || means.at(c).isEmpty())
            continue;
        QCPGraph *graph = m_plot->addGraph(m_plot->xAxis, m_plot->yAxis);
        graph->setData(means.at(c), zValues);
        graph->setName(components.at(c).name);
        QPen pen(components.at(c).color);
        pen.setWidthF(1.8);
        graph->setPen(pen);
        graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle,
                                               components.at(c).color,
                                               components.at(c).color, 7.0));
    }

    m_statsPanel->setPlainText(statsText);
    m_plot->rescaleAxes();
    m_plot->replot();
}

void ProfileFrame::openCorrectionDialog()
{
    const QString key = currentProfileKey();
    const QList<QUuid> ids = m_model->profilePoints(key);
    if (ids.isEmpty())
        return;

    // propose angles from depth-averaged means of the uncorrected profile
    RotationAngles identity;
    double sumU = 0.0, sumV = 0.0, sumW = 0.0, sumVw = 0.0, sumVVar = 0.0, sumWVar = 0.0;
    int n = 0;
    for (const QUuid &id : ids) {
        const MeasurementPoint *point = m_model->point(id);
        if (!point)
            continue;
        // process without the current correction to propose absolute angles
        const ProcessedSeries series = processPoint(*point, identity, m_model->wRole());
        if (!series.isValid())
            continue;
        sumU += series.stats.u.mean;
        sumV += series.stats.v.mean;
        sumW += series.stats.w.mean;
        sumVw += series.stats.vw;
        sumVVar += series.stats.v.std * series.stats.v.std;
        sumWVar += series.stats.w.std * series.stats.w.std;
        ++n;
    }
    if (n == 0)
        return;
    const RotationAngles proposed = rotation::propose(
        sumU / n, sumV / n, sumW / n, sumVw / n, sumVVar / n, sumWVar / n);

    CorrectionDialog dialog(m_model->correction(key), proposed, this);
    if (dialog.exec() == QDialog::Accepted)
        m_model->setCorrection(key, dialog.angles());
}

bool ProfileFrame::exportPng(const QString &filePath, int dpi)
{
    const double scale = dpi / 96.0;
    return m_plot->savePng(filePath, m_plot->width(), m_plot->height(), scale, -1, dpi);
}

QJsonObject ProfileFrame::saveState() const
{
    QJsonObject state;
    state[QStringLiteral("profile")] = currentProfileKey();
    state[QStringLiteral("u")] = m_uCheck->isChecked();
    state[QStringLiteral("v")] = m_vCheck->isChecked();
    state[QStringLiteral("w")] = m_wCheck->isChecked();
    state[QStringLiteral("relative")] = m_zhRadio->isChecked();
    return state;
}

void ProfileFrame::restoreState(const QJsonObject &state)
{
    const int index = m_profileCombo->findText(state[QStringLiteral("profile")].toString());
    if (index >= 0)
        m_profileCombo->setCurrentIndex(index);
    m_uCheck->setChecked(state[QStringLiteral("u")].toBool(true));
    m_vCheck->setChecked(state[QStringLiteral("v")].toBool(true));
    m_wCheck->setChecked(state[QStringLiteral("w")].toBool(true));
    m_zhRadio->setChecked(state[QStringLiteral("relative")].toBool(false));
    rebuildPlot();
}
