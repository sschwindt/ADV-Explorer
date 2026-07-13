/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "VnaReader.h"

#include <QFile>
#include <QFileInfo>

namespace adv {

namespace {

// role and name for each of the 18 kept columns (of 20 in the file)
struct ColumnSpec {
    int fileColumn;
    Role role;
};

const ColumnSpec kColumns[] = {
    {1, Role::Time},  {2, Role::Sample},
    {4, Role::U},     {5, Role::V},     {6, Role::W1},    {7, Role::W2},
    {8, Role::AmpX},  {9, Role::AmpY},  {10, Role::AmpZ1}, {11, Role::AmpZ2},
    {12, Role::SnrX}, {13, Role::SnrY}, {14, Role::SnrZ1}, {15, Role::SnrZ2},
    {16, Role::CorrX},{17, Role::CorrY},{18, Role::CorrZ1},{19, Role::CorrZ2},
};
constexpr int kKeptColumns = int(sizeof(kColumns) / sizeof(kColumns[0]));
constexpr int kFileColumns = 20;

} // namespace

AdvData VnaReader::read(const QByteArray &bytes, QString *errorString)
{
    AdvData data;
    QVector<QVector<double>> cols(kKeptColumns);

    int lineNo = 0;
    for (const QByteArray &rawLine : bytes.split('\n')) {
        ++lineNo;
        const QByteArray line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        const QList<QByteArray> tokens = line.simplified().split(' ');
        if (tokens.size() < kFileColumns) {
            if (errorString)
                *errorString = QStringLiteral("Line %1: expected %2 columns, found %3.")
                                   .arg(lineNo).arg(kFileColumns).arg(tokens.size());
            return AdvData();
        }
        for (int c = 0; c < kKeptColumns; ++c) {
            bool ok = false;
            const double value = tokens.at(kColumns[c].fileColumn).toDouble(&ok);
            cols[c].append(ok ? value : nan());
        }
    }

    if (cols.first().isEmpty()) {
        if (errorString)
            *errorString = QStringLiteral("No data rows found.");
        return AdvData();
    }

    for (int c = 0; c < kKeptColumns; ++c)
        data.addColumn(roleName(kColumns[c].role), std::move(cols[c]), kColumns[c].role);

    data.setFormat(QStringLiteral("vna"));
    data.setRawBytes(bytes);
    data.estimateSamplingFrequency();
    return data;
}

AdvData VnaReader::readFile(const QString &filePath, QString *errorString)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorString)
            *errorString = QStringLiteral("Cannot open %1: %2").arg(filePath, file.errorString());
        return AdvData();
    }
    AdvData data = read(file.readAll(), errorString);
    data.setSourceFileName(QFileInfo(filePath).fileName());
    return data;
}

void VnaReader::coordinatesFromFileName(const QString &fileName,
                                        double *x, double *y, double *z)
{
    // replicate tke-calculator's vna_file_name2coordinates:
    // "__" encodes a minus sign, coordinates are cm separated by "_"
    QString base = QFileInfo(fileName).completeBaseName();
    base.replace(QStringLiteral("__"), QStringLiteral("-"));
    const QStringList parts = base.split('_');
    double *out[3] = {x, y, z};
    for (int i = 0; i < 3; ++i) {
        if (!out[i])
            continue;
        bool ok = false;
        const double cm = (i < parts.size()) ? parts.at(i).toDouble(&ok) : 0.0;
        *out[i] = ok ? cm / 100.0 : nan();
    }
}

} // namespace adv
