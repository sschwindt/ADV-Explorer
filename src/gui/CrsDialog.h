/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;

/// Chooser for the project coordinate reference system used in field mode.
///
/// Only the systems adv::crs implements are offered, so the application needs
/// no PROJ or GDAL dependency. The list is filterable and an EPSG code can be
/// typed directly; unsupported codes are rejected with the supported ranges
/// spelled out rather than silently accepted.
class CrsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CrsDialog(int currentEpsg, QWidget *parent = nullptr);

    int selectedEpsg() const { return m_epsg; }

private:
    void applyFilter(const QString &text);
    void selectEpsg(int epsg);
    void updateDetails();

    QLineEdit *m_filterEdit;
    QListWidget *m_list;
    QLabel *m_details;
    int m_epsg = 0;
};
