/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "core/MeasurementPoint.h"

#include <QDialog>
#include <QList>

class QTableWidget;

/// Bulk import wizard: select multiple ADV files at once, then assign x, y, z
/// coordinates (and optional water depth) per file in an overview table.
/// Coordinates are pre-filled from XX_YY_ZZ_*.vna style file names.
class ImportWizard : public QDialog
{
    Q_OBJECT
public:
    explicit ImportWizard(QWidget *parent = nullptr);

    /// The imported points (valid after accept()).
    QList<adv::MeasurementPoint> results() const { return m_results; }

private slots:
    void addFiles();
    void tryAccept();

private:
    QTableWidget *m_table;
    QList<adv::MeasurementPoint> m_results;
};
