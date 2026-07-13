/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "core/MeasurementPoint.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QTableWidget;

/// Wizard dialog defining or editing one measurement point: x-y-z position
/// (meters, 4 decimals), data file, water depth, analysis time window and
/// despiking filters. For delimited text files a column-mapping table is
/// shown so velocity components can be assigned to file columns.
class PointWizard : public QDialog
{
    Q_OBJECT
public:
    /// New point at the clicked flume position.
    PointWizard(double x, double y, QWidget *parent = nullptr);
    /// Edit an existing point.
    explicit PointWizard(const adv::MeasurementPoint &point, QWidget *parent = nullptr);

    /// The configured point (valid after accept()).
    adv::MeasurementPoint result() const { return m_point; }
    /// True when the user pressed the Delete button (edit mode only).
    bool deleteRequested() const { return m_deleteRequested; }

private slots:
    void browseFile();
    void tryAccept();
    void requestDelete();

private:
    void buildUi(bool editMode);
    void loadFromPoint();
    void updateMappingTable();
    bool loadDataFile(QString *errorString);
    QHash<adv::Role, int> mappingFromTable() const;

    adv::MeasurementPoint m_point;
    bool m_deleteRequested = false;

    QDoubleSpinBox *m_xSpin = nullptr;
    QDoubleSpinBox *m_ySpin = nullptr;
    QDoubleSpinBox *m_zSpin = nullptr;
    QLineEdit *m_fileEdit = nullptr;
    QDoubleSpinBox *m_freqSpin = nullptr;
    QCheckBox *m_depthCheck = nullptr;
    QDoubleSpinBox *m_depthSpin = nullptr;
    QCheckBox *m_tStartCheck = nullptr;
    QDoubleSpinBox *m_tStartSpin = nullptr;
    QCheckBox *m_tEndCheck = nullptr;
    QDoubleSpinBox *m_tEndSpin = nullptr;
    QTableWidget *m_mappingTable = nullptr;

    // despiking controls
    QCheckBox *m_corrCheck = nullptr;
    QDoubleSpinBox *m_corrSpin = nullptr;
    QCheckBox *m_snrCheck = nullptr;
    QDoubleSpinBox *m_snrSpin = nullptr;
    QCheckBox *m_velCheck = nullptr;
    QDoubleSpinBox *m_velKSpin = nullptr;
    QComboBox *m_gnCombo = nullptr;
    QDoubleSpinBox *m_gnLambdaSpin = nullptr;
    QDoubleSpinBox *m_gnKSpin = nullptr;
    QCheckBox *m_pstCheck = nullptr;
    QComboBox *m_replaceCombo = nullptr;
};
