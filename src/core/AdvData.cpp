/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "AdvData.h"

#include <algorithm>
#include <cmath>

namespace adv {

const QVector<double> &AdvData::columnByRole(Role role) const
{
    static const QVector<double> empty;
    const int idx = columnOfRole(role);
    return idx >= 0 ? m_columns.at(idx) : empty;
}

void AdvData::addColumn(const QString &name, QVector<double> values, Role role)
{
    m_columnNames.append(name);
    m_columns.append(std::move(values));
    if (role != Role::Other)
        m_roleMap.insert(role, m_columns.size() - 1);
}

void AdvData::estimateSamplingFrequency()
{
    const QVector<double> &t = columnByRole(Role::Time);
    if (t.size() < 2)
        return;
    QVector<double> dt;
    dt.reserve(t.size() - 1);
    for (int i = 1; i < t.size(); ++i) {
        const double d = t[i] - t[i - 1];
        if (std::isfinite(d) && d > 0.0)
            dt.append(d);
    }
    if (dt.isEmpty())
        return;
    std::nth_element(dt.begin(), dt.begin() + dt.size() / 2, dt.end());
    const double medianDt = dt[dt.size() / 2];
    if (medianDt > 0.0)
        m_samplingFrequency = 1.0 / medianDt;
}

void AdvData::synthesizeTime(double frequency)
{
    if (!m_timeSynthesized || frequency <= 0.0)
        return;
    const int idx = columnOfRole(Role::Time);
    if (idx < 0)
        return;
    QVector<double> &t = m_columns[idx];
    for (int i = 0; i < t.size(); ++i)
        t[i] = i / frequency;
    m_samplingFrequency = frequency;
}

QString roleName(Role role)
{
    switch (role) {
    case Role::Time: return QStringLiteral("time (s)");
    case Role::Sample: return QStringLiteral("sample no.");
    case Role::U: return QStringLiteral("u (m/s)");
    case Role::V: return QStringLiteral("v (m/s)");
    case Role::W1: return QStringLiteral("w1 (m/s)");
    case Role::W2: return QStringLiteral("w2 (m/s)");
    case Role::AmpX: return QStringLiteral("ampl. x (dB)");
    case Role::AmpY: return QStringLiteral("ampl. y (dB)");
    case Role::AmpZ1: return QStringLiteral("ampl. z1 (dB)");
    case Role::AmpZ2: return QStringLiteral("ampl. z2 (dB)");
    case Role::SnrX: return QStringLiteral("SNR x");
    case Role::SnrY: return QStringLiteral("SNR y");
    case Role::SnrZ1: return QStringLiteral("SNR z1");
    case Role::SnrZ2: return QStringLiteral("SNR z2");
    case Role::CorrX: return QStringLiteral("corr x");
    case Role::CorrY: return QStringLiteral("corr y");
    case Role::CorrZ1: return QStringLiteral("corr z1");
    case Role::CorrZ2: return QStringLiteral("corr z2");
    case Role::Other: break;
    }
    return QStringLiteral("other");
}

Role roleFromName(const QString &name)
{
    static const QList<Role> all = {
        Role::Time, Role::Sample, Role::U, Role::V, Role::W1, Role::W2,
        Role::AmpX, Role::AmpY, Role::AmpZ1, Role::AmpZ2,
        Role::SnrX, Role::SnrY, Role::SnrZ1, Role::SnrZ2,
        Role::CorrX, Role::CorrY, Role::CorrZ1, Role::CorrZ2,
    };
    for (Role r : all) {
        if (roleName(r) == name)
            return r;
    }
    return Role::Other;
}

} // namespace adv
