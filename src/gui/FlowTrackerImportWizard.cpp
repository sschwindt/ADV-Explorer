/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "FlowTrackerImportWizard.h"

#include "core/Crs.h"
#include "core/FlowTrackerCsvReader.h"
#include "core/FormatRegistry.h"
#include "core/ProjectModel.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

using namespace adv;

namespace {

QDoubleSpinBox *makeCoordinateSpin(QWidget *parent)
{
    auto *spin = new QDoubleSpinBox(parent);
    // projected coordinates reach seven digits before the decimal point
    spin->setRange(-1.0e8, 1.0e8);
    spin->setDecimals(3);
    spin->setSingleStep(1.0);
    spin->setSuffix(QStringLiteral(" m"));
    return spin;
}

} // namespace

FlowTrackerImportWizard::FlowTrackerImportWizard(ProjectModel *model, QWidget *parent)
    : QDialog(parent)
    , m_model(model)
{
    buildUi();
    updateGeoreferencing();
}

void FlowTrackerImportWizard::buildUi()
{
    setWindowTitle(tr("Import FlowTracker2 survey"));
    setMinimumSize(760, 680);

    auto *layout = new QVBoxLayout(this);

    // --- file -----------------------------------------------------------------
    auto *fileGroup = new QGroupBox(tr("Measurement file"), this);
    auto *fileLayout = new QVBoxLayout(fileGroup);
    auto *fileRow = new QHBoxLayout;
    m_fileEdit = new QLineEdit(this);
    m_fileEdit->setPlaceholderText(tr("Select a .ft file, or the .ft.dat.csv export"));
    m_fileEdit->setReadOnly(true);
    auto *browseButton = new QPushButton(tr("Browse..."), this);
    connect(browseButton, &QPushButton::clicked, this, &FlowTrackerImportWizard::browseSurvey);
    fileRow->addWidget(m_fileEdit, 1);
    fileRow->addWidget(browseButton);
    fileLayout->addLayout(fileRow);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    fileLayout->addWidget(m_summaryLabel);
    layout->addWidget(fileGroup);

    // --- stations -------------------------------------------------------------
    auto *stationGroup = new QGroupBox(tr("Stations and measurement points"), this);
    auto *stationLayout = new QVBoxLayout(stationGroup);
    m_stationTree = new QTreeWidget(this);
    m_stationTree->setColumnCount(5);
    m_stationTree->setHeaderLabels({tr("Station"), tr("Chainage (m)"), tr("Depth h (m)"),
                                    tr("z above bed (m)"), tr("mean u (m/s)")});
    m_stationTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    stationLayout->addWidget(m_stationTree);

    m_applySpikesCheck = new QCheckBox(
        tr("Use the instrument's own spike flags (recommended)"), this);
    m_applySpikesCheck->setChecked(true);
    m_applySpikesCheck->setToolTip(
        tr("FlowTracker2 marks individual samples as spikes and excludes them from the\n"
           "values it reports. Keeping the flags reproduces the instrument's own\n"
           "statistics exactly. The CSV export does not contain them."));
    stationLayout->addWidget(m_applySpikesCheck);
    layout->addWidget(stationGroup, 1);

    // --- georeferencing -------------------------------------------------------
    auto *geoGroup = new QGroupBox(tr("Positions in the project coordinate system"), this);
    auto *geoLayout = new QVBoxLayout(geoGroup);

    m_lineRadio = new QRadioButton(tr("Place stations along a cross-section line"), this);
    m_lineRadio->setChecked(true);
    geoLayout->addWidget(m_lineRadio);

    auto *lineForm = new QFormLayout;
    m_leftChainageSpin = new QDoubleSpinBox(this);
    m_leftChainageSpin->setRange(-10000.0, 10000.0);
    m_leftChainageSpin->setDecimals(3);
    m_leftChainageSpin->setSuffix(QStringLiteral(" m"));
    m_rightChainageSpin = new QDoubleSpinBox(this);
    m_rightChainageSpin->setRange(-10000.0, 10000.0);
    m_rightChainageSpin->setDecimals(3);
    m_rightChainageSpin->setSuffix(QStringLiteral(" m"));
    m_leftXSpin = makeCoordinateSpin(this);
    m_leftYSpin = makeCoordinateSpin(this);
    m_rightXSpin = makeCoordinateSpin(this);
    m_rightYSpin = makeCoordinateSpin(this);

    auto *leftRow = new QHBoxLayout;
    leftRow->addWidget(new QLabel(tr("at chainage"), this));
    leftRow->addWidget(m_leftChainageSpin);
    leftRow->addWidget(new QLabel(tr("E"), this));
    leftRow->addWidget(m_leftXSpin);
    leftRow->addWidget(new QLabel(tr("N"), this));
    leftRow->addWidget(m_leftYSpin);
    lineForm->addRow(tr("Start of the tape:"), leftRow);

    auto *rightRow = new QHBoxLayout;
    rightRow->addWidget(new QLabel(tr("at chainage"), this));
    rightRow->addWidget(m_rightChainageSpin);
    rightRow->addWidget(new QLabel(tr("E"), this));
    rightRow->addWidget(m_rightXSpin);
    rightRow->addWidget(new QLabel(tr("N"), this));
    rightRow->addWidget(m_rightYSpin);
    lineForm->addRow(tr("End of the tape:"), rightRow);
    geoLayout->addLayout(lineForm);

    m_lineHint = new QLabel(this);
    m_lineHint->setWordWrap(true);
    geoLayout->addWidget(m_lineHint);
    for (QDoubleSpinBox *spin : {m_leftXSpin, m_leftYSpin, m_rightXSpin, m_rightYSpin,
                                 m_leftChainageSpin, m_rightChainageSpin})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this]() { updateGeoreferencing(); });

    m_positionsRadio = new QRadioButton(
        tr("Take positions from a surveyed point file"), this);
    geoLayout->addWidget(m_positionsRadio);
    auto *positionsRow = new QHBoxLayout;
    m_positionsEdit = new QLineEdit(this);
    m_positionsEdit->setPlaceholderText(tr("GeoPackage (.gpkg) or easting/northing text file"));
    m_positionsEdit->setReadOnly(true);
    auto *positionsButton = new QPushButton(tr("Browse..."), this);
    connect(positionsButton, &QPushButton::clicked, this,
            &FlowTrackerImportWizard::browsePositions);
    positionsRow->addWidget(m_positionsEdit, 1);
    positionsRow->addWidget(positionsButton);
    geoLayout->addLayout(positionsRow);
    m_positionsHint = new QLabel(this);
    m_positionsHint->setWordWrap(true);
    geoLayout->addWidget(m_positionsHint);

    connect(m_lineRadio, &QRadioButton::toggled, this,
            [this]() { updateGeoreferencing(); });

    m_gpsLabel = new QLabel(this);
    m_gpsLabel->setWordWrap(true);
    geoLayout->addWidget(m_gpsLabel);
    layout->addWidget(geoGroup);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &FlowTrackerImportWizard::tryAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void FlowTrackerImportWizard::browseSurvey()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select a FlowTracker2 measurement"), QString(),
        tr("FlowTracker2 measurement (*.ft);;FlowTracker2 CSV export (*.ft.dat.csv);;"
           "All files (*)"));
    if (path.isEmpty())
        return;
    loadSurvey(path);
}

void FlowTrackerImportWizard::loadSurvey(const QString &filePath)
{
    QString error;
    FtSurvey survey;
    const FileFormat *format = formats::byFilePath(filePath);
    const bool isCsvExport = format && format->id == QStringLiteral("ftcsv");

    const bool ok = isCsvExport
                        ? FlowTrackerCsvReader::readFile(filePath, &survey, &error)
                        : FlowTrackerReader::readFile(filePath, &survey, &error);
    if (!ok) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Cannot read %1:\n%2")
                                 .arg(QFileInfo(filePath).fileName(), error));
        return;
    }

    m_survey = survey;
    m_surveyPath = filePath;
    m_fileEdit->setText(filePath);

    QString summary = tr("%1: %2 stations, %3 measurement points, %4 Hz")
                          .arg(survey.siteName.isEmpty() ? QFileInfo(filePath).fileName()
                                                         : survey.siteName)
                          .arg(survey.stations.size())
                          .arg(survey.pointCount())
                          .arg(survey.samplingFrequency);
    if (std::isfinite(survey.averagingSeconds))
        summary += tr(", %1 s per point").arg(survey.averagingSeconds);
    if (isCsvExport) {
        summary += tr("\nThe CSV export carries no correlation columns and no spike "
                      "indices, and it omits the bank stations. Import the .ft file "
                      "instead when you still have it.");
        m_applySpikesCheck->setChecked(false);
        m_applySpikesCheck->setEnabled(false);
    } else {
        m_applySpikesCheck->setEnabled(true);
    }
    m_summaryLabel->setText(summary);

    // the bank stations bracket the tape, which is exactly the chainage range
    // the cross-section line needs
    if (!survey.stations.isEmpty()) {
        m_leftChainageSpin->setValue(survey.stations.first().location);
        m_rightChainageSpin->setValue(survey.stations.last().location);
    }

    // the handheld fix locates the site but is far too noisy to place stations
    QString gpsText = tr("No GPS position is stored in this file.");
    for (const FtStation &station : survey.stations) {
        if (!station.hasGps)
            continue;
        gpsText = tr("Handheld GPS fix: %1, %2 (WGS 84). Used only to centre the map; "
                     "its scatter of several metres exceeds the station spacing.")
                      .arg(station.latitude, 0, 'f', 6)
                      .arg(station.longitude, 0, 'f', 6);
        break;
    }
    m_gpsLabel->setText(gpsText);

    rebuildStationTree();
    updateGeoreferencing();
}

void FlowTrackerImportWizard::rebuildStationTree()
{
    m_stationTree->clear();
    for (const FtStation &station : m_survey.stations) {
        auto *item = new QTreeWidgetItem(m_stationTree);
        item->setText(0, tr("Station %1 (%2)")
                             .arg(station.index + 1)
                             .arg(station.stationType.isEmpty() ? tr("open water")
                                                                : station.stationType));
        item->setText(1, QString::number(station.location, 'f', 2));
        item->setText(2, QString::number(station.depth, 'f', 3));

        if (station.isBank()) {
            // a bank has a depth and a chainage but no velocity record, so it
            // cannot become a measurement point; it defines the section end
            item->setText(0, item->text(0) + tr("  [bank, no velocity data]"));
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
            item->setDisabled(true);
            continue;
        }

        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
        for (const FtPoint &point : station.points) {
            auto *child = new QTreeWidgetItem(item);
            child->setText(0, tr("%1 of depth").arg(point.fractionalDepth));
            child->setText(3, QString::number(point.distanceFromBottom, 'f', 3));
            const SeriesStats stats =
                flowstats::seriesStats(point.data.columnByRole(Role::U));
            child->setText(4, QString::number(stats.mean, 'f', 3));
        }
        item->setExpanded(true);
    }
}

void FlowTrackerImportWizard::browsePositions()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select surveyed positions"), QString(),
        tr("GeoPackage (*.gpkg);;Delimited text (*.csv *.txt *.dat);;All files (*)"));
    if (path.isEmpty())
        return;

    QString error;
    QVector<GeoPoint> positions;
    if (path.endsWith(QStringLiteral(".gpkg"), Qt::CaseInsensitive)) {
        const QVector<GeoPackageLayer> layers = geoimport::geoPackageLayers(path, &error);
        if (layers.isEmpty()) {
            QMessageBox::warning(this, windowTitle(), error);
            return;
        }
        QString layerName = layers.first().name;
        if (layers.size() > 1) {
            QStringList names;
            for (const GeoPackageLayer &layer : layers)
                names.append(tr("%1 (%2 points, EPSG:%3)")
                                 .arg(layer.name).arg(layer.featureCount).arg(layer.epsg));
            bool ok = false;
            const QString chosen = QInputDialog::getItem(
                this, tr("Choose a layer"), tr("Point layer:"), names, 0, false, &ok);
            if (!ok)
                return;
            layerName = layers.at(names.indexOf(chosen)).name;
        }

        // warn rather than silently mixing coordinate systems
        for (const GeoPackageLayer &layer : layers) {
            if (layer.name != layerName)
                continue;
            if (m_model->epsg() != 0 && layer.epsg != 0 && layer.epsg != m_model->epsg()) {
                QMessageBox::warning(
                    this, windowTitle(),
                    tr("The layer is stored in EPSG:%1 but the project uses EPSG:%2.\n"
                       "Reproject the layer before importing it, or change the project "
                       "coordinate system.")
                        .arg(layer.epsg).arg(m_model->epsg()));
                return;
            }
        }
        positions = geoimport::readGeoPackage(path, layerName, &error);
    } else {
        const QStringList columns = geoimport::pointCsvColumns(path, &error);
        if (columns.size() < 2) {
            QMessageBox::warning(this, windowTitle(),
                                 error.isEmpty() ? tr("The file has fewer than two columns.")
                                                 : error);
            return;
        }
        bool ok = false;
        const QString xName = QInputDialog::getItem(
            this, tr("Easting column"), tr("Column holding the easting:"), columns, 0, false, &ok);
        if (!ok)
            return;
        const QString yName = QInputDialog::getItem(
            this, tr("Northing column"), tr("Column holding the northing:"), columns,
            qMin(1, columns.size() - 1), false, &ok);
        if (!ok)
            return;
        positions = geoimport::readPointCsv(path, columns.indexOf(xName),
                                            columns.indexOf(yName), -1, &error);
    }

    if (positions.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }

    m_positions = positions;
    m_positionsEdit->setText(path);
    m_positionsRadio->setChecked(true);
    updateGeoreferencing();
}

void FlowTrackerImportWizard::updateGeoreferencing()
{
    const bool byLine = m_lineRadio->isChecked();
    for (QDoubleSpinBox *spin : {m_leftXSpin, m_leftYSpin, m_rightXSpin, m_rightYSpin,
                                 m_leftChainageSpin, m_rightChainageSpin})
        spin->setEnabled(byLine);

    // count only the stations that will actually become points
    int openWater = 0;
    for (const FtStation &station : m_survey.stations) {
        if (!station.isBank())
            ++openWater;
    }

    if (byLine) {
        const double dx = m_rightXSpin->value() - m_leftXSpin->value();
        const double dy = m_rightYSpin->value() - m_leftYSpin->value();
        const double lineLength = std::hypot(dx, dy);
        const double tapeLength =
            std::fabs(m_rightChainageSpin->value() - m_leftChainageSpin->value());

        if (lineLength <= 0.0) {
            m_lineHint->setText(tr("Enter the two bank positions to place the stations."));
        } else {
            QString hint = tr("Line length %1 m for a tape length of %2 m.")
                               .arg(lineLength, 0, 'f', 2)
                               .arg(tapeLength, 0, 'f', 2);
            if (tapeLength > 0.0 && std::fabs(lineLength - tapeLength) > 0.02 * tapeLength) {
                hint += tr("  These differ by more than 2 percent; check the bank "
                           "coordinates or the chainages.");
            }
            m_lineHint->setText(hint);
        }
        m_positionsHint->clear();
    } else {
        m_lineHint->clear();
        if (m_positions.isEmpty()) {
            m_positionsHint->setText(tr("Select a file of surveyed positions."));
        } else {
            m_positionsHint->setText(
                tr("%1 positions read; they are matched to the %2 open-water stations "
                   "in order.")
                    .arg(m_positions.size()).arg(openWater));
        }
    }
}

bool FlowTrackerImportWizard::buildResults(QString *errorString)
{
    m_results.clear();
    m_sections.clear();
    m_stationCount = 0;

    if (m_survey.stations.isEmpty()) {
        *errorString = tr("Select a FlowTracker2 measurement first.");
        return false;
    }
    if (m_model->epsg() == 0) {
        *errorString = tr("Set the project coordinate system first "
                          "(Project > Coordinate system).");
        return false;
    }

    QVector<const FtStation *> selected;
    for (int row = 0; row < m_stationTree->topLevelItemCount(); ++row) {
        const QTreeWidgetItem *item = m_stationTree->topLevelItem(row);
        const FtStation &station = m_survey.stations.at(row);
        if (station.isBank())
            continue;
        if (item->checkState(0) == Qt::Checked)
            selected.append(&station);
    }
    if (selected.isEmpty()) {
        *errorString = tr("No station is selected for import.");
        return false;
    }

    CrossSection section;
    section.name = m_survey.siteName.isEmpty() ? QFileInfo(m_surveyPath).completeBaseName()
                                               : m_survey.siteName;
    for (const FtStation &station : m_survey.stations)
        section.bed.append(QPointF(station.location, station.depth));

    QVector<QPointF> stationPositions(selected.size());

    if (m_lineRadio->isChecked()) {
        section.leftChainage = m_leftChainageSpin->value();
        section.rightChainage = m_rightChainageSpin->value();
        section.leftX = m_leftXSpin->value();
        section.leftY = m_leftYSpin->value();
        section.rightX = m_rightXSpin->value();
        section.rightY = m_rightYSpin->value();
        if (!section.isValid()) {
            *errorString = tr("The cross-section line is incomplete: the two chainages "
                              "must differ and both bank positions must be given.");
            return false;
        }
        for (int i = 0; i < selected.size(); ++i) {
            double x = 0.0;
            double y = 0.0;
            section.positionAt(selected.at(i)->location, &x, &y);
            stationPositions[i] = QPointF(x, y);
        }
    } else {
        if (m_positions.size() != selected.size()) {
            *errorString = tr("%1 positions were read but %2 stations are selected. "
                              "Select the matching stations or use a file with one "
                              "position per station.")
                               .arg(m_positions.size()).arg(selected.size());
            return false;
        }
        for (int i = 0; i < selected.size(); ++i)
            stationPositions[i] = QPointF(m_positions.at(i).x, m_positions.at(i).y);

        section.leftChainage = selected.first()->location;
        section.rightChainage = selected.last()->location;
        section.leftX = stationPositions.first().x();
        section.leftY = stationPositions.first().y();
        section.rightX = stationPositions.last().x();
        section.rightY = stationPositions.last().y();
    }

    const DespikeConfig despike = fieldDespikeDefaults(m_survey.snrThresholdDb);

    for (int i = 0; i < selected.size(); ++i) {
        const FtStation &station = *selected.at(i);
        // the position is computed once per station: recomputing it per point
        // could differ in the last bits and split one vertical into several
        // profiles, because profiles are keyed on the formatted coordinates
        const double x = stationPositions.at(i).x();
        const double y = stationPositions.at(i).y();

        for (const FtPoint &ftPoint : station.points) {
            MeasurementPoint point;
            point.x = x;
            point.y = y;
            point.z = ftPoint.distanceFromBottom;
            point.waterDepth = station.depth;
            point.chainage = station.location;
            point.stationName = tr("%1 station %2")
                                    .arg(section.name).arg(station.index + 1);
            point.despike = despike;
            point.data = ftPoint.data;

            if (!m_applySpikesCheck->isChecked() && !ftPoint.instrumentSpikes.isEmpty()) {
                // the reader blanks the flagged samples; restoring them means
                // re-reading, which is not worth it, so tell the user instead
                point.stationName += tr(" (instrument spikes applied)");
            }
            m_results.append(point);
        }
        ++m_stationCount;
    }

    m_sections.append(section);
    return true;
}

void FlowTrackerImportWizard::tryAccept()
{
    QString error;
    if (!buildResults(&error)) {
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }
    accept();
}
