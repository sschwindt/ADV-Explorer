/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "PointWizard.h"

#include "core/CsvReader.h"
#include "core/VnaReader.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

using namespace adv;

namespace {

bool isCsvFile(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return suffix != QStringLiteral("vna");
}

QDoubleSpinBox *makeCoordinateSpin(QWidget *parent)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setRange(-1000.0, 1000.0);
    spin->setDecimals(4); // 4 digits precision in meters
    spin->setSingleStep(0.01);
    spin->setSuffix(QStringLiteral(" m"));
    return spin;
}

} // namespace

PointWizard::PointWizard(double x, double y, QWidget *parent)
    : QDialog(parent)
{
    m_point.x = x;
    m_point.y = y;
    buildUi(false);
    loadFromPoint();
}

PointWizard::PointWizard(const MeasurementPoint &point, QWidget *parent)
    : QDialog(parent)
    , m_point(point)
{
    buildUi(true);
    loadFromPoint();
}

void PointWizard::buildUi(bool editMode)
{
    setWindowTitle(editMode ? tr("Edit measurement point") : tr("New measurement point"));
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);

    // --- position -----------------------------------------------------------
    auto *positionGroup = new QGroupBox(tr("Position (flume coordinate system)"), this);
    auto *positionForm = new QFormLayout(positionGroup);
    m_xSpin = makeCoordinateSpin(this);
    m_ySpin = makeCoordinateSpin(this);
    m_zSpin = makeCoordinateSpin(this);
    positionForm->addRow(tr("x (streamwise):"), m_xSpin);
    positionForm->addRow(tr("y (transverse, left bank negative):"), m_ySpin);
    positionForm->addRow(tr("z (above bottom):"), m_zSpin);

    m_depthCheck = new QCheckBox(tr("Total water depth h at this x-y position "
                                    "(applies to all points at the same x-y):"), this);
    m_depthSpin = makeCoordinateSpin(this);
    m_depthSpin->setRange(0.0001, 1000.0);
    m_depthSpin->setValue(0.2);
    m_depthSpin->setEnabled(false);
    connect(m_depthCheck, &QCheckBox::toggled, m_depthSpin, &QWidget::setEnabled);
    positionForm->addRow(m_depthCheck, m_depthSpin);
    layout->addWidget(positionGroup);

    // --- data file ------------------------------------------------------------
    auto *fileGroup = new QGroupBox(tr("Measurement data file"), this);
    auto *fileLayout = new QVBoxLayout(fileGroup);
    auto *fileRow = new QHBoxLayout;
    m_fileEdit = new QLineEdit(this);
    m_fileEdit->setPlaceholderText(tr("Select a .vna or delimited text file (.csv, .txt, .dat)"));
    auto *browseButton = new QPushButton(tr("Browse..."), this);
    connect(browseButton, &QPushButton::clicked, this, &PointWizard::browseFile);
    fileRow->addWidget(m_fileEdit, 1);
    fileRow->addWidget(browseButton);
    fileLayout->addLayout(fileRow);

    auto *freqRow = new QHBoxLayout;
    freqRow->addWidget(new QLabel(tr("Sampling frequency (used when the file "
                                     "has no time column):"), this));
    m_freqSpin = new QDoubleSpinBox(this);
    m_freqSpin->setRange(0.01, 100000.0);
    m_freqSpin->setDecimals(2);
    m_freqSpin->setValue(200.0);
    m_freqSpin->setSuffix(QStringLiteral(" Hz"));
    freqRow->addWidget(m_freqSpin);
    freqRow->addStretch();
    fileLayout->addLayout(freqRow);

    m_mappingTable = new QTableWidget(this);
    m_mappingTable->setColumnCount(2);
    m_mappingTable->setHorizontalHeaderLabels({tr("Data series"), tr("File column")});
    m_mappingTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_mappingTable->verticalHeader()->setVisible(false);
    m_mappingTable->setMaximumHeight(180);
    m_mappingTable->setVisible(false);
    fileLayout->addWidget(m_mappingTable);
    layout->addWidget(fileGroup);

    // --- time window ------------------------------------------------------------
    auto *windowGroup = new QGroupBox(tr("Analysis time window"), this);
    auto *windowLayout = new QHBoxLayout(windowGroup);
    m_tStartCheck = new QCheckBox(tr("Start (s):"), this);
    m_tStartSpin = new QDoubleSpinBox(this);
    m_tStartSpin->setRange(0.0, 1e7);
    m_tStartSpin->setDecimals(3);
    m_tStartSpin->setEnabled(false);
    connect(m_tStartCheck, &QCheckBox::toggled, m_tStartSpin, &QWidget::setEnabled);
    m_tEndCheck = new QCheckBox(tr("End (s):"), this);
    m_tEndSpin = new QDoubleSpinBox(this);
    m_tEndSpin->setRange(0.0, 1e7);
    m_tEndSpin->setDecimals(3);
    m_tEndSpin->setEnabled(false);
    connect(m_tEndCheck, &QCheckBox::toggled, m_tEndSpin, &QWidget::setEnabled);
    windowLayout->addWidget(m_tStartCheck);
    windowLayout->addWidget(m_tStartSpin);
    windowLayout->addSpacing(16);
    windowLayout->addWidget(m_tEndCheck);
    windowLayout->addWidget(m_tEndSpin);
    windowLayout->addStretch();
    layout->addWidget(windowGroup);

    // --- despiking ------------------------------------------------------------
    auto *despikeGroup = new QGroupBox(tr("Despiking filters"), this);
    auto *despikeForm = new QFormLayout(despikeGroup);

    m_corrCheck = new QCheckBox(tr("Correlation score threshold (remove rows below):"), this);
    m_corrSpin = new QDoubleSpinBox(this);
    m_corrSpin->setRange(0.0, 100.0);
    m_corrSpin->setValue(70.0);
    m_corrSpin->setEnabled(false);
    connect(m_corrCheck, &QCheckBox::toggled, m_corrSpin, &QWidget::setEnabled);
    despikeForm->addRow(m_corrCheck, m_corrSpin);

    m_snrCheck = new QCheckBox(tr("SNR threshold (remove rows below):"), this);
    m_snrSpin = new QDoubleSpinBox(this);
    m_snrSpin->setRange(0.0, 100.0);
    m_snrSpin->setValue(20.0);
    m_snrSpin->setEnabled(false);
    connect(m_snrCheck, &QCheckBox::toggled, m_snrSpin, &QWidget::setEnabled);
    despikeForm->addRow(m_snrCheck, m_snrSpin);

    m_velCheck = new QCheckBox(tr("Velocity threshold: |u,v,w| > k std with k ="), this);
    m_velKSpin = new QDoubleSpinBox(this);
    m_velKSpin->setRange(0.5, 20.0);
    m_velKSpin->setValue(3.0);
    m_velKSpin->setEnabled(false);
    connect(m_velCheck, &QCheckBox::toggled, m_velKSpin, &QWidget::setEnabled);
    despikeForm->addRow(m_velCheck, m_velKSpin);

    m_gnCombo = new QComboBox(this);
    m_gnCombo->addItems({tr("off"), tr("velocity thresholding"), tr("acceleration thresholding")});
    auto *gnRow = new QHBoxLayout;
    gnRow->addWidget(m_gnCombo);
    gnRow->addWidget(new QLabel(tr("lambda a:"), this));
    m_gnLambdaSpin = new QDoubleSpinBox(this);
    m_gnLambdaSpin->setRange(0.1, 10.0);
    m_gnLambdaSpin->setSingleStep(0.1);
    m_gnLambdaSpin->setValue(1.0);
    gnRow->addWidget(m_gnLambdaSpin);
    gnRow->addWidget(new QLabel(tr("k:"), this));
    m_gnKSpin = new QDoubleSpinBox(this);
    m_gnKSpin->setRange(0.5, 20.0);
    m_gnKSpin->setValue(3.0);
    gnRow->addWidget(m_gnKSpin);
    gnRow->addStretch();
    // literal ampersand: this label has no buddy widget, so QFormLayout does
    // not run mnemonic processing and "&&" would render as a double ampersand
    despikeForm->addRow(tr("Goring & Nikora (2002):"), gnRow);

    m_pstCheck = new QCheckBox(tr("Phase-space thresholding (iterative)"), this);
    despikeForm->addRow(m_pstCheck);

    m_replaceCombo = new QComboBox(this);
    m_replaceCombo->addItems({tr("leave gaps (NaN)"), tr("linear interpolation")});
    m_replaceCombo->setCurrentIndex(1);
    despikeForm->addRow(tr("Replace removed samples by:"), m_replaceCombo);
    layout->addWidget(despikeGroup);

    // --- buttons ------------------------------------------------------------
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (editMode) {
        auto *deleteButton = buttons->addButton(tr("Delete point"), QDialogButtonBox::DestructiveRole);
        connect(deleteButton, &QPushButton::clicked, this, &PointWizard::requestDelete);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &PointWizard::tryAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void PointWizard::loadFromPoint()
{
    m_xSpin->setValue(m_point.x);
    m_ySpin->setValue(m_point.y);
    m_zSpin->setValue(m_point.z);
    if (m_point.hasWaterDepth()) {
        m_depthCheck->setChecked(true);
        m_depthSpin->setValue(m_point.waterDepth);
    }
    if (std::isfinite(m_point.tStart)) {
        m_tStartCheck->setChecked(true);
        m_tStartSpin->setValue(m_point.tStart);
    }
    if (std::isfinite(m_point.tEnd)) {
        m_tEndCheck->setChecked(true);
        m_tEndSpin->setValue(m_point.tEnd);
    }
    if (!m_point.data.sourceFileName().isEmpty()) {
        m_fileEdit->setText(m_point.data.sourceFileName());
        m_freqSpin->setValue(m_point.data.samplingFrequency());
    }

    const DespikeConfig &d = m_point.despike;
    m_corrCheck->setChecked(d.corrEnabled);
    m_corrSpin->setValue(d.corrThreshold);
    m_snrCheck->setChecked(d.snrEnabled);
    m_snrSpin->setValue(d.snrThreshold);
    m_velCheck->setChecked(d.velEnabled);
    m_velKSpin->setValue(d.velK);
    m_gnCombo->setCurrentIndex(int(d.gnMethod));
    m_gnLambdaSpin->setValue(d.gnLambdaA);
    m_gnKSpin->setValue(d.gnK);
    m_pstCheck->setChecked(d.pstEnabled);
    m_replaceCombo->setCurrentIndex(int(d.replace));
}

void PointWizard::browseFile()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("Select ADV measurement file"), QString(),
        tr("ADV data (*.vna *.csv *.txt *.dat);;All files (*)"));
    if (filePath.isEmpty())
        return;
    m_fileEdit->setText(filePath);
    updateMappingTable();
}

void PointWizard::updateMappingTable()
{
    const QString filePath = m_fileEdit->text();
    if (filePath.isEmpty() || !isCsvFile(filePath) || !QFile::exists(filePath)) {
        m_mappingTable->setVisible(false);
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const CsvReader::Preview preview = CsvReader::preview(file.readAll());
    if (preview.columnCount == 0)
        return;
    const QHash<Role, int> guessed = CsvReader::guessMapping(preview.columnNames);

    const QList<Role> mappableRoles = {Role::Time, Role::U, Role::V, Role::W1, Role::W2};
    m_mappingTable->setRowCount(mappableRoles.size());
    for (int r = 0; r < mappableRoles.size(); ++r) {
        const Role role = mappableRoles.at(r);
        auto *nameItem = new QTableWidgetItem(roleName(role));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_mappingTable->setItem(r, 0, nameItem);
        auto *combo = new QComboBox(m_mappingTable);
        combo->addItem(tr("(not present)"), -1);
        for (int c = 0; c < preview.columnNames.size(); ++c)
            combo->addItem(preview.columnNames.at(c), c);
        if (guessed.contains(role))
            combo->setCurrentIndex(guessed.value(role) + 1);
        m_mappingTable->setCellWidget(r, 1, combo);
    }
    m_mappingTable->setVisible(true);
}

QHash<Role, int> PointWizard::mappingFromTable() const
{
    QHash<Role, int> mapping;
    const QList<Role> mappableRoles = {Role::Time, Role::U, Role::V, Role::W1, Role::W2};
    for (int r = 0; r < m_mappingTable->rowCount() && r < mappableRoles.size(); ++r) {
        auto *combo = qobject_cast<QComboBox *>(m_mappingTable->cellWidget(r, 1));
        if (!combo)
            continue;
        const int column = combo->currentData().toInt();
        if (column >= 0)
            mapping.insert(mappableRoles.at(r), column);
    }
    return mapping;
}

bool PointWizard::loadDataFile(QString *errorString)
{
    const QString filePath = m_fileEdit->text();
    // unchanged file (edit mode shows only the base name)
    if (filePath == m_point.data.sourceFileName() && !m_point.data.isEmpty())
        return true;
    if (filePath.isEmpty()) {
        *errorString = tr("Please select a measurement data file.");
        return false;
    }
    if (!QFile::exists(filePath)) {
        *errorString = tr("File %1 does not exist.").arg(filePath);
        return false;
    }
    AdvData data;
    if (isCsvFile(filePath)) {
        QHash<Role, int> mapping = mappingFromTable();
        if (mapping.isEmpty()) {
            updateMappingTable();
            mapping = mappingFromTable();
        }
        if (!mapping.contains(Role::U)) {
            *errorString = tr("Please assign at least the u velocity column.");
            return false;
        }
        data = CsvReader::readFile(filePath, mapping, errorString);
    } else {
        data = VnaReader::readFile(filePath, errorString);
    }
    if (data.isEmpty())
        return false;
    m_point.data = data;
    return true;
}

void PointWizard::tryAccept()
{
    QString error;
    if (!loadDataFile(&error)) {
        QMessageBox::warning(this, tr("Measurement point"), error);
        return;
    }
    // files without a time column get their time base from the user frequency
    if (m_point.data.timeSynthesized())
        m_point.data.synthesizeTime(m_freqSpin->value());
    m_point.x = m_xSpin->value();
    m_point.y = m_ySpin->value();
    m_point.z = m_zSpin->value();
    m_point.waterDepth = m_depthCheck->isChecked() ? m_depthSpin->value() : nan();
    m_point.tStart = m_tStartCheck->isChecked() ? m_tStartSpin->value() : nan();
    m_point.tEnd = m_tEndCheck->isChecked() ? m_tEndSpin->value() : nan();

    DespikeConfig &d = m_point.despike;
    d.corrEnabled = m_corrCheck->isChecked();
    d.corrThreshold = m_corrSpin->value();
    d.snrEnabled = m_snrCheck->isChecked();
    d.snrThreshold = m_snrSpin->value();
    d.velEnabled = m_velCheck->isChecked();
    d.velK = m_velKSpin->value();
    d.gnMethod = DespikeConfig::GnMethod(m_gnCombo->currentIndex());
    d.gnLambdaA = m_gnLambdaSpin->value();
    d.gnK = m_gnKSpin->value();
    d.pstEnabled = m_pstCheck->isChecked();
    d.replace = DespikeConfig::Replace(m_replaceCombo->currentIndex());

    accept();
}

void PointWizard::requestDelete()
{
    if (QMessageBox::question(this, tr("Delete point"),
                              tr("Remove this measurement point from the project?"))
        == QMessageBox::Yes) {
        m_deleteRequested = true;
        accept();
    }
}
