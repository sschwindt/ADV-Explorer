/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "CsvReader.h"

#include <QFile>
#include <QFileInfo>

namespace adv {

namespace {

QChar sniffDelimiter(const QList<QByteArray> &lines)
{
    const QList<QChar> candidates = {'\t', ';', ',', ' '};
    QChar best = ',';
    int bestScore = -1;
    for (const QChar delim : candidates) {
        // score: number of columns if consistent over the sampled lines
        int columns = -1;
        bool consistent = true;
        for (const QByteArray &line : lines) {
            if (line.trimmed().isEmpty())
                continue;
            int n;
            if (delim == ' ')
                n = QString::fromUtf8(line).simplified().split(' ').size();
            else
                n = line.count(delim.toLatin1()) + 1;
            if (columns < 0)
                columns = n;
            else if (n != columns)
                consistent = false;
        }
        if (columns > 1 && consistent && columns > bestScore) {
            bestScore = columns;
            best = delim;
        }
    }
    return best;
}

QStringList splitLine(const QString &line, QChar delimiter)
{
    if (delimiter == ' ')
        return line.simplified().split(' ');
    QStringList parts = line.split(delimiter);
    for (QString &p : parts)
        p = p.trimmed();
    return parts;
}

bool isNumericRow(const QStringList &tokens)
{
    if (tokens.isEmpty())
        return false;
    int numeric = 0;
    for (const QString &t : tokens) {
        bool ok = false;
        t.toDouble(&ok);
        if (!ok) {
            // tolerate decimal commas
            QString fixed = t;
            fixed.replace(',', '.');
            fixed.toDouble(&ok);
        }
        if (ok)
            ++numeric;
    }
    return numeric > tokens.size() / 2;
}

double toDoubleFlexible(const QString &token, bool *ok)
{
    double v = token.toDouble(ok);
    if (!*ok) {
        QString fixed = token;
        fixed.replace(',', '.');
        v = fixed.toDouble(ok);
    }
    return v;
}

} // namespace

CsvReader::Preview CsvReader::preview(const QByteArray &bytes, int maxRows)
{
    Preview result;
    QList<QByteArray> lines = bytes.split('\n');
    // drop trailing/blank lines for sniffing
    QList<QByteArray> sample;
    for (const QByteArray &l : lines) {
        if (!l.trimmed().isEmpty())
            sample.append(l.trimmed());
        if (sample.size() >= maxRows + 1)
            break;
    }
    if (sample.isEmpty())
        return result;

    result.delimiter = sniffDelimiter(sample);

    const QStringList firstRow = splitLine(QString::fromUtf8(sample.first()), result.delimiter);
    result.hasHeader = !isNumericRow(firstRow);
    result.columnCount = firstRow.size();

    if (result.hasHeader) {
        result.columnNames = firstRow;
    } else {
        for (int i = 0; i < result.columnCount; ++i)
            result.columnNames.append(QStringLiteral("column %1").arg(i + 1));
    }

    for (int i = result.hasHeader ? 1 : 0; i < sample.size() && result.sampleRows.size() < maxRows; ++i)
        result.sampleRows.append(splitLine(QString::fromUtf8(sample.at(i)), result.delimiter));

    return result;
}

AdvData CsvReader::read(const QByteArray &bytes, const QHash<Role, int> &mapping,
                        QString *errorString)
{
    const Preview info = preview(bytes, 3);
    if (info.columnCount == 0) {
        if (errorString)
            *errorString = QStringLiteral("No data found in file.");
        return AdvData();
    }

    QVector<QVector<double>> cols(info.columnCount);
    const QList<QByteArray> lines = bytes.split('\n');
    bool skipFirst = info.hasHeader;
    for (const QByteArray &rawLine : lines) {
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty())
            continue;
        if (skipFirst) {
            skipFirst = false;
            continue;
        }
        const QStringList tokens = splitLine(line, info.delimiter);
        for (int c = 0; c < info.columnCount; ++c) {
            if (c < tokens.size()) {
                bool ok = false;
                const double v = toDoubleFlexible(tokens.at(c), &ok);
                cols[c].append(ok ? v : nan());
            } else {
                cols[c].append(nan());
            }
        }
    }

    if (cols.first().isEmpty()) {
        if (errorString)
            *errorString = QStringLiteral("No data rows found.");
        return AdvData();
    }

    // invert mapping: column index -> role
    QHash<int, Role> byIndex;
    for (auto it = mapping.constBegin(); it != mapping.constEnd(); ++it)
        byIndex.insert(it.value(), it.key());

    AdvData data;
    for (int c = 0; c < info.columnCount; ++c) {
        const Role role = byIndex.value(c, Role::Other);
        const QString name = (role != Role::Other) ? roleName(role) : info.columnNames.at(c);
        data.addColumn(name, std::move(cols[c]), role);
    }

    // synthesize a time column from the sample index if none was mapped;
    // the point wizard lets the user supply the true sampling frequency
    if (!data.hasRole(Role::Time)) {
        QVector<double> t(data.rowCount());
        for (int i = 0; i < t.size(); ++i)
            t[i] = i; // seconds unknown; treated as sample index
        data.addColumn(roleName(Role::Time), std::move(t), Role::Time);
        data.setTimeSynthesized(true);
    }

    data.setFormat(QStringLiteral("csv"));
    data.setRawBytes(bytes);
    data.estimateSamplingFrequency();
    return data;
}

AdvData CsvReader::readFile(const QString &filePath, const QHash<Role, int> &mapping,
                            QString *errorString)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorString)
            *errorString = QStringLiteral("Cannot open %1: %2").arg(filePath, file.errorString());
        return AdvData();
    }
    AdvData data = read(file.readAll(), mapping, errorString);
    data.setSourceFileName(QFileInfo(filePath).fileName());
    return data;
}

QHash<Role, int> CsvReader::guessMapping(const QStringList &columnNames)
{
    QHash<Role, int> mapping;
    auto match = [&](Role role, const QStringList &needles) {
        if (mapping.contains(role))
            return;
        for (int i = 0; i < columnNames.size(); ++i) {
            const QString name = columnNames.at(i).toLower();
            for (const QString &needle : needles) {
                if (name.contains(needle)) {
                    if (!mapping.values().contains(i))
                        mapping.insert(role, i);
                    return;
                }
            }
        }
    };
    match(Role::Time, {QStringLiteral("time"), QStringLiteral("t (s)"), QStringLiteral("t(s)")});
    match(Role::U, {QStringLiteral("u ("), QStringLiteral("u("), QStringLiteral("vel_x"),
                    QStringLiteral("vx"), QStringLiteral("u_")});
    match(Role::V, {QStringLiteral("v ("), QStringLiteral("v("), QStringLiteral("vel_y"),
                    QStringLiteral("vy"), QStringLiteral("v_")});
    match(Role::W1, {QStringLiteral("w1"), QStringLiteral("w ("), QStringLiteral("w("),
                     QStringLiteral("vel_z"), QStringLiteral("vz")});
    match(Role::W2, {QStringLiteral("w2")});
    match(Role::SnrX, {QStringLiteral("snr")});
    match(Role::CorrX, {QStringLiteral("corr")});
    return mapping;
}

} // namespace adv
