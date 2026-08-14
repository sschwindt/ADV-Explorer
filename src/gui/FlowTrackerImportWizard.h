/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "core/FlowTrackerReader.h"
#include "core/GeoPointImport.h"
#include "core/MeasurementPoint.h"
#include "core/ProjectSettings.h"

#include <QDialog>
#include <QList>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTableWidget;
class QTreeWidget;

namespace adv {
class ProjectModel;
}

/// Import of a whole FlowTracker2 cross section.
///
/// Unlike the other importers this one turns a single file into many
/// measurement points, because a FlowTracker2 survey is a row of verticals along
/// a tape, each sampled at one to three fractional depths. The instrument
/// records a chainage rather than a coordinate, so the wizard also has to work
/// out where those stations actually are, in the project coordinate system.
///
/// Two ways of doing that are offered:
///
///  * a cross-section line given by its two bank positions, with stations placed
///    by interpolating their chainage along it. The bank and edge stations in
///    the file supply the chainages of the two ends, which is what makes them
///    worth keeping even though they hold no velocity data.
///  * positions surveyed elsewhere, read from a GeoPackage or a delimited text
///    file and matched to the stations by order or by name.
///
/// The handheld GPS in the file is offered only for centring the map. Its
/// scatter is several metres, far more than the spacing between stations, so it
/// cannot place them.
class FlowTrackerImportWizard : public QDialog
{
    Q_OBJECT
public:
    explicit FlowTrackerImportWizard(adv::ProjectModel *model, QWidget *parent = nullptr);

    QList<adv::MeasurementPoint> results() const { return m_results; }
    QList<adv::CrossSection> crossSections() const { return m_sections; }
    int stationCount() const { return m_stationCount; }

private slots:
    void browseSurvey();
    void browsePositions();
    void tryAccept();

private:
    void buildUi();
    void loadSurvey(const QString &filePath);
    void rebuildStationTree();
    void updateGeoreferencing();
    bool buildResults(QString *errorString);

    adv::ProjectModel *m_model;
    adv::FtSurvey m_survey;
    QString m_surveyPath;

    QList<adv::MeasurementPoint> m_results;
    QList<adv::CrossSection> m_sections;
    int m_stationCount = 0;

    QLineEdit *m_fileEdit = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QTreeWidget *m_stationTree = nullptr;
    QCheckBox *m_applySpikesCheck = nullptr;

    QRadioButton *m_lineRadio = nullptr;
    QRadioButton *m_positionsRadio = nullptr;
    QDoubleSpinBox *m_leftXSpin = nullptr;
    QDoubleSpinBox *m_leftYSpin = nullptr;
    QDoubleSpinBox *m_rightXSpin = nullptr;
    QDoubleSpinBox *m_rightYSpin = nullptr;
    QDoubleSpinBox *m_leftChainageSpin = nullptr;
    QDoubleSpinBox *m_rightChainageSpin = nullptr;
    QLabel *m_lineHint = nullptr;

    QLineEdit *m_positionsEdit = nullptr;
    QLabel *m_positionsHint = nullptr;
    QVector<adv::GeoPoint> m_positions;

    QLabel *m_gpsLabel = nullptr;
};
