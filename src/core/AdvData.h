/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <limits>

namespace adv {

/// Semantic role of a data column, independent of the source file format.
enum class Role {
    Time,        ///< time in seconds
    Sample,      ///< sample number
    U,           ///< streamwise velocity (m/s)
    V,           ///< transverse velocity (m/s)
    W1,          ///< vertical velocity, beam 1 (m/s)
    W2,          ///< vertical velocity, beam 2 (m/s)
    AmpX, AmpY, AmpZ1, AmpZ2,     ///< signal amplitudes (dB)
    SnrX, SnrY, SnrZ1, SnrZ2,     ///< signal-to-noise ratios
    CorrX, CorrY, CorrZ1, CorrZ2, ///< correlation scores (%)
    Other        ///< any additional column
};

inline double nan() { return std::numeric_limits<double>::quiet_NaN(); }

/// Column-oriented container for one raw ADV measurement file.
class AdvData
{
public:
    AdvData() = default;

    int rowCount() const { return m_columns.isEmpty() ? 0 : m_columns.first().size(); }
    int columnCount() const { return m_columns.size(); }
    bool isEmpty() const { return rowCount() == 0; }

    const QStringList &columnNames() const { return m_columnNames; }
    const QVector<double> &column(int index) const { return m_columns.at(index); }
    QVector<double> &column(int index) { return m_columns[index]; }

    /// Index of the column holding the given role; -1 if absent.
    int columnOfRole(Role role) const { return m_roleMap.value(role, -1); }
    bool hasRole(Role role) const { return m_roleMap.contains(role); }
    /// Column data for a role; empty vector if the role is absent.
    const QVector<double> &columnByRole(Role role) const;

    void addColumn(const QString &name, QVector<double> values, Role role = Role::Other);

    /// Sampling frequency in Hz (estimated from the time column when read).
    double samplingFrequency() const { return m_samplingFrequency; }
    void setSamplingFrequency(double f) { m_samplingFrequency = f; }
    /// Estimate the sampling frequency from the median time step.
    void estimateSamplingFrequency();

    /// True when the file had no time column and time was synthesized from
    /// the sample index (the user then provides the sampling frequency).
    bool timeSynthesized() const { return m_timeSynthesized; }
    void setTimeSynthesized(bool synthesized) { m_timeSynthesized = synthesized; }
    /// Regenerate the synthetic time column as index / frequency.
    /// No-op when the time column came from the file.
    void synthesizeTime(double frequency);

    /// Base name of the source file (display / project bookkeeping).
    QString sourceFileName() const { return m_sourceFileName; }
    void setSourceFileName(const QString &name) { m_sourceFileName = name; }

    /// Raw bytes of the source file, kept for standalone .advProj embedding.
    QByteArray rawBytes() const { return m_rawBytes; }
    void setRawBytes(const QByteArray &bytes) { m_rawBytes = bytes; }

    /// "vna" or "csv"; determines which reader restores the data from rawBytes.
    QString format() const { return m_format; }
    void setFormat(const QString &fmt) { m_format = fmt; }

    /// Column mapping used by the CSV reader (role -> column index), for
    /// reproducible restoring from a project file.
    QHash<Role, int> roleMap() const { return m_roleMap; }

private:
    QStringList m_columnNames;
    QVector<QVector<double>> m_columns;
    QHash<Role, int> m_roleMap;
    double m_samplingFrequency = 200.0;
    bool m_timeSynthesized = false;
    QString m_sourceFileName;
    QByteArray m_rawBytes;
    QString m_format = QStringLiteral("vna");
};

/// Human-readable names for roles (used in mapping UI and exports).
QString roleName(Role role);
Role roleFromName(const QString &name);

} // namespace adv
