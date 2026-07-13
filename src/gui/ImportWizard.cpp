/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "ImportWizard.h"

#include "core/CsvReader.h"
#include "core/VnaReader.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

using namespace adv;

namespace {
enum Column { FileColumn, XColumn, YColumn, ZColumn, DepthColumn, ColumnCount };
}

ImportWizard::ImportWizard(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Import ADV measurement files"));
    setMinimumSize(720, 420);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        tr("Add measurement files and assign the x-y-z position of each probe "
           "location (m). Coordinates are pre-filled from XX_YY_ZZ_*.vna file "
           "names (cm). The water depth column is optional."), this));

    m_table = new QTableWidget(0, ColumnCount, this);
    m_table->setHorizontalHeaderLabels(
        {tr("File"), tr("x (m)"), tr("y (m)"), tr("z (m)"), tr("water depth h (m)")});
    m_table->horizontalHeader()->setSectionResizeMode(FileColumn, QHeaderView::Stretch);
    layout->addWidget(m_table, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto *addButton = buttons->addButton(tr("Add files..."), QDialogButtonBox::ActionRole);
    connect(addButton, &QPushButton::clicked, this, &ImportWizard::addFiles);
    connect(buttons, &QDialogButtonBox::accepted, this, &ImportWizard::tryAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    addFiles();
}

void ImportWizard::addFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Select ADV measurement files"), QString(),
        tr("ADV data (*.vna *.csv *.txt *.dat);;All files (*)"));
    for (const QString &filePath : files) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *fileItem = new QTableWidgetItem(QFileInfo(filePath).fileName());
        fileItem->setData(Qt::UserRole, filePath);
        fileItem->setFlags(fileItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, FileColumn, fileItem);

        double x, y, z;
        VnaReader::coordinatesFromFileName(filePath, &x, &y, &z);
        const double coords[3] = {x, y, z};
        for (int c = 0; c < 3; ++c) {
            m_table->setItem(row, XColumn + c,
                             new QTableWidgetItem(std::isfinite(coords[c])
                                                      ? QString::number(coords[c], 'f', 4)
                                                      : QString()));
        }
        m_table->setItem(row, DepthColumn, new QTableWidgetItem(QString()));
    }
}

void ImportWizard::tryAccept()
{
    m_results.clear();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QString filePath = m_table->item(row, FileColumn)->data(Qt::UserRole).toString();
        MeasurementPoint point;

        bool okX = false, okY = false, okZ = false;
        point.x = m_table->item(row, XColumn)->text().toDouble(&okX);
        point.y = m_table->item(row, YColumn)->text().toDouble(&okY);
        point.z = m_table->item(row, ZColumn)->text().toDouble(&okZ);
        if (!okX || !okY || !okZ) {
            QMessageBox::warning(this, windowTitle(),
                                 tr("Row %1: please provide numeric x, y and z coordinates.")
                                     .arg(row + 1));
            return;
        }
        bool okDepth = false;
        const double depth = m_table->item(row, DepthColumn)->text().toDouble(&okDepth);
        point.waterDepth = okDepth ? depth : nan();

        QString error;
        const QString suffix = QFileInfo(filePath).suffix().toLower();
        if (suffix == QStringLiteral("vna")) {
            point.data = VnaReader::readFile(filePath, &error);
        } else {
            QFile file(filePath);
            QHash<Role, int> mapping;
            if (file.open(QIODevice::ReadOnly))
                mapping = CsvReader::guessMapping(
                    CsvReader::preview(file.readAll()).columnNames);
            point.data = CsvReader::readFile(filePath, mapping, &error);
        }
        if (point.data.isEmpty()) {
            QMessageBox::warning(this, windowTitle(),
                                 tr("Cannot read %1:\n%2").arg(filePath, error));
            return;
        }
        m_results.append(std::move(point));
    }

    if (m_results.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("No files to import."));
        return;
    }
    accept();
}
