/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "MeasurementPoint.h"

#include <cmath>

namespace adv {

QString MeasurementPoint::label() const
{
    return QStringLiteral("x=%1 y=%2 z=%3 m")
        .arg(x, 0, 'f', 4).arg(y, 0, 'f', 4).arg(z, 0, 'f', 4);
}

QString MeasurementPoint::xyKey() const
{
    return makeXyKey(x, y);
}

QString MeasurementPoint::makeXyKey(double x, double y)
{
    return QStringLiteral("%1|%2").arg(x, 0, 'f', 4).arg(y, 0, 'f', 4);
}

namespace {

/// Slice a column to the [first, last] row range.
QVector<double> slice(const QVector<double> &values, int first, int last)
{
    if (values.isEmpty())
        return {};
    return values.mid(first, last - first + 1);
}

/// Row-average of up to four quality columns (correlation or SNR).
QVector<double> rowAverage(const AdvData &data, const QList<Role> &roles)
{
    QList<const QVector<double> *> cols;
    for (Role role : roles) {
        if (data.hasRole(role))
            cols.append(&data.columnByRole(role));
    }
    if (cols.isEmpty())
        return {};
    QVector<double> avg(data.rowCount(), nan());
    for (int i = 0; i < avg.size(); ++i) {
        double sum = 0.0;
        int n = 0;
        for (const QVector<double> *col : cols) {
            if (i < col->size() && std::isfinite(col->at(i))) {
                sum += col->at(i);
                ++n;
            }
        }
        if (n > 0)
            avg[i] = sum / n;
    }
    return avg;
}

} // namespace

ProcessedSeries processPoint(const MeasurementPoint &point,
                             const RotationAngles &correction,
                             Role wRole)
{
    ProcessedSeries result;
    const AdvData &data = point.data;
    if (data.isEmpty())
        return result;

    // --- time window ------------------------------------------------------
    const QVector<double> &time = data.columnByRole(Role::Time);
    int first = 0;
    int last = data.rowCount() - 1;
    if (!time.isEmpty()) {
        if (std::isfinite(point.tStart)) {
            while (first < time.size() && time[first] < point.tStart)
                ++first;
        }
        if (std::isfinite(point.tEnd)) {
            while (last > first && time[last] > point.tEnd)
                --last;
        }
    }
    if (last < first)
        return result;

    result.time = slice(time, first, last);
    result.samplingFrequency = data.samplingFrequency();

    // --- despiking ----------------------------------------------------------
    DespikeInput input;
    input.u = slice(data.columnByRole(Role::U), first, last);
    input.v = slice(data.columnByRole(Role::V), first, last);
    input.w1 = slice(data.columnByRole(Role::W1), first, last);
    input.w2 = slice(data.columnByRole(Role::W2), first, last);
    input.corrAvg = slice(rowAverage(data, {Role::CorrX, Role::CorrY, Role::CorrZ1, Role::CorrZ2}),
                          first, last);
    input.snrAvg = slice(rowAverage(data, {Role::SnrX, Role::SnrY, Role::SnrZ1, Role::SnrZ2}),
                         first, last);
    input.samplingFrequency = data.samplingFrequency();

    if (point.despike.anyEnabled()) {
        const DespikeResult cleaned = despike::apply(input, point.despike);
        result.u = cleaned.u;
        result.v = cleaned.v;
        result.w1 = cleaned.w1;
        result.w2 = cleaned.w2;
        result.spikeCounts = cleaned.spikeCounts;
    } else {
        result.u = input.u;
        result.v = input.v;
        result.w1 = input.w1;
        result.w2 = input.w2;
    }

    // --- rotation correction (probe misalignment) --------------------------
    if (!correction.isIdentity()) {
        QVector<double> &w = (wRole == Role::W2 && !result.w2.isEmpty()) ? result.w2 : result.w1;
        rotation::apply(correction, &result.u, &result.v, &w);
    }

    // --- statistics ---------------------------------------------------------
    const QVector<double> &w = (wRole == Role::W2 && !result.w2.isEmpty()) ? result.w2 : result.w1;
    result.stats = flowstats::compute(result.u, result.v, w, data.samplingFrequency());
    result.tkeInst = flowstats::instantaneousTke(result.u, result.v, w);

    return result;
}

} // namespace adv
