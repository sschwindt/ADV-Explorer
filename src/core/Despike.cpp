/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "Despike.h"
#include "AdvData.h"
#include "FlowStats.h"

#include <cmath>

namespace adv {
namespace despike {

namespace {

/// Remove samples of one component where |x - mean| > k * std (rmspike.py
/// velocity thresholding). Returns removed count.
int velocityThreshold(QVector<double> *values, double k)
{
    const SeriesStats s = flowstats::seriesStats(*values);
    if (s.n == 0 || !std::isfinite(s.std))
        return 0;
    const double lower = s.mean - k * s.std;
    const double upper = s.mean + k * s.std;
    int removed = 0;
    for (double &v : *values) {
        if (std::isfinite(v) && (v <= lower || v >= upper)) {
            v = nan();
            ++removed;
        }
    }
    return removed;
}

/// Remove samples whose acceleration exceeds lambdaA * g (rmspike.py
/// acceleration thresholding). a_i = (x_i - x_{i-1}) * freq.
int accelerationThreshold(QVector<double> *values, double lambdaA, double freq)
{
    const double aCr = 9.81 * lambdaA;
    const QVector<double> &x = *values;
    QVector<bool> flag(x.size(), false);
    for (int i = 1; i < x.size(); ++i) {
        if (!std::isfinite(x[i]) || !std::isfinite(x[i - 1]))
            continue;
        const double a = (x[i] - x[i - 1]) * freq;
        if (std::fabs(a) >= aCr)
            flag[i] = true;
    }
    int removed = 0;
    for (int i = 0; i < x.size(); ++i) {
        if (flag[i]) {
            (*values)[i] = nan();
            ++removed;
        }
    }
    return removed;
}

/// Remove rows (all velocity components) where the quality series falls below
/// a threshold. Returns removed sample count (per component sum).
int qualityThreshold(const QVector<double> &quality, double threshold,
                     QVector<QVector<double> *> components)
{
    int removed = 0;
    for (int i = 0; i < quality.size(); ++i) {
        if (!std::isfinite(quality[i]) || quality[i] >= threshold)
            continue;
        for (QVector<double> *comp : components) {
            if (i < comp->size() && std::isfinite((*comp)[i])) {
                (*comp)[i] = nan();
                ++removed;
            }
        }
    }
    return removed;
}

/// Central differences; NaN where a neighbor is NaN.
QVector<double> centralDiff(const QVector<double> &x)
{
    QVector<double> d(x.size(), nan());
    for (int i = 1; i + 1 < x.size(); ++i) {
        if (std::isfinite(x[i + 1]) && std::isfinite(x[i - 1]))
            d[i] = (x[i + 1] - x[i - 1]) / 2.0;
    }
    return d;
}

/// One PST iteration; returns newly flagged sample count.
int pstIteration(QVector<double> *values)
{
    QVector<double> &x = *values;
    const double mean = flowstats::nanMean(x);
    if (!std::isfinite(mean))
        return 0;

    // fluctuations and derivatives
    QVector<double> f(x.size());
    for (int i = 0; i < x.size(); ++i)
        f[i] = x[i] - mean;
    const QVector<double> df = centralDiff(f);
    const QVector<double> d2f = centralDiff(df);

    const SeriesStats sf = flowstats::seriesStats(f);
    const SeriesStats sdf = flowstats::seriesStats(df);
    const SeriesStats sd2f = flowstats::seriesStats(d2f);
    if (sf.n < 16 || sf.std <= 0.0 || sdf.std <= 0.0 || sd2f.std <= 0.0)
        return 0;

    // universal threshold
    const double lambdaU = std::sqrt(2.0 * std::log(double(sf.n)));
    const double a1 = lambdaU * sf.std;   // f axis
    const double b1 = lambdaU * sdf.std;  // df axis
    const double b2 = lambdaU * sd2f.std; // d2f axis

    // rotation angle of the f-d2f ellipse principal axis
    double sumFD2 = 0.0, sumFF = 0.0;
    for (int i = 0; i < f.size(); ++i) {
        if (std::isfinite(f[i]) && std::isfinite(d2f[i])) {
            sumFD2 += f[i] * d2f[i];
            sumFF += f[i] * f[i];
        }
    }
    const double theta = (sumFF > 0.0) ? std::atan2(sumFD2, sumFF) : 0.0;
    const double ct = std::cos(theta), st = std::sin(theta);
    // ellipse semi-axes in the rotated f-d2f plane
    const double at2 = (a1 * a1 * ct * ct - b2 * b2 * st * st) / (ct * ct * ct * ct - st * st * st * st);
    const double bt2 = (b2 * b2 * ct * ct - a1 * a1 * st * st) / (ct * ct * ct * ct - st * st * st * st);

    auto outsideEllipse = [](double px, double py, double a, double b) {
        if (!(a > 0.0) || !(b > 0.0))
            return false;
        return (px * px) / (a * a) + (py * py) / (b * b) > 1.0;
    };

    int removed = 0;
    for (int i = 0; i < x.size(); ++i) {
        if (!std::isfinite(x[i]))
            continue;
        bool spike = false;
        if (std::isfinite(df[i]) && outsideEllipse(f[i], df[i], a1, b1))
            spike = true;
        if (!spike && std::isfinite(df[i]) && std::isfinite(d2f[i])
            && outsideEllipse(df[i], d2f[i], b1, b2))
            spike = true;
        if (!spike && std::isfinite(d2f[i]) && at2 > 0.0 && bt2 > 0.0) {
            const double xr = f[i] * ct + d2f[i] * st;
            const double yr = -f[i] * st + d2f[i] * ct;
            if (outsideEllipse(xr, yr, std::sqrt(at2), std::sqrt(bt2)))
                spike = true;
        }
        if (spike) {
            x[i] = nan();
            ++removed;
        }
    }
    return removed;
}

} // namespace

int phaseSpaceThreshold(QVector<double> *values)
{
    int total = 0;
    for (int iteration = 0; iteration < 10; ++iteration) {
        const int removed = pstIteration(values);
        total += removed;
        if (removed == 0)
            break;
    }
    return total;
}

int fillGapsLinear(QVector<double> *values)
{
    QVector<double> &x = *values;
    int filled = 0;
    int lastFinite = -1;
    for (int i = 0; i < x.size(); ++i) {
        if (std::isfinite(x[i])) {
            if (lastFinite >= 0 && lastFinite < i - 1) {
                // interior gap: interpolate linearly
                const double x0 = x[lastFinite];
                const double x1 = x[i];
                const int span = i - lastFinite;
                for (int j = lastFinite + 1; j < i; ++j) {
                    x[j] = x0 + (x1 - x0) * double(j - lastFinite) / span;
                    ++filled;
                }
            } else if (lastFinite < 0 && i > 0) {
                // leading gap: back-fill with first finite value
                for (int j = 0; j < i; ++j) {
                    x[j] = x[i];
                    ++filled;
                }
            }
            lastFinite = i;
        }
    }
    // trailing gap: forward-fill
    if (lastFinite >= 0) {
        for (int j = lastFinite + 1; j < x.size(); ++j) {
            x[j] = x[lastFinite];
            ++filled;
        }
    }
    return filled;
}

DespikeResult apply(const DespikeInput &input, const DespikeConfig &config)
{
    DespikeResult result;
    result.u = input.u;
    result.v = input.v;
    result.w1 = input.w1;
    result.w2 = input.w2;

    QVector<QVector<double> *> components = {&result.u, &result.v, &result.w1};
    if (!result.w2.isEmpty())
        components.append(&result.w2);
    const QStringList componentNames = {QStringLiteral("u"), QStringLiteral("v"),
                                        QStringLiteral("w1"), QStringLiteral("w2")};

    if (config.corrEnabled && !input.corrAvg.isEmpty()) {
        const int n = qualityThreshold(input.corrAvg, config.corrThreshold, components);
        result.spikeCounts.insert(QStringLiteral("correlation"), n);
        result.totalRemoved += n;
    }
    if (config.snrEnabled && !input.snrAvg.isEmpty()) {
        const int n = qualityThreshold(input.snrAvg, config.snrThreshold, components);
        result.spikeCounts.insert(QStringLiteral("SNR"), n);
        result.totalRemoved += n;
    }
    if (config.velEnabled) {
        for (int c = 0; c < components.size(); ++c) {
            const int n = velocityThreshold(components[c], config.velK);
            result.spikeCounts.insert(QStringLiteral("velocity %1").arg(componentNames[c]), n);
            result.totalRemoved += n;
        }
    }
    if (config.gnMethod == DespikeConfig::GnMethod::Velocity) {
        for (int c = 0; c < components.size(); ++c) {
            const int n = velocityThreshold(components[c], config.gnK);
            result.spikeCounts.insert(QStringLiteral("GN velocity %1").arg(componentNames[c]), n);
            result.totalRemoved += n;
        }
    } else if (config.gnMethod == DespikeConfig::GnMethod::Acceleration) {
        for (int c = 0; c < components.size(); ++c) {
            const int n = accelerationThreshold(components[c], config.gnLambdaA,
                                                input.samplingFrequency);
            result.spikeCounts.insert(QStringLiteral("GN acceleration %1").arg(componentNames[c]), n);
            result.totalRemoved += n;
        }
    }
    if (config.pstEnabled) {
        for (int c = 0; c < components.size(); ++c) {
            const int n = phaseSpaceThreshold(components[c]);
            result.spikeCounts.insert(QStringLiteral("PST %1").arg(componentNames[c]), n);
            result.totalRemoved += n;
        }
    }

    if (config.replace == DespikeConfig::Replace::LinearInterpolation) {
        for (QVector<double> *comp : components)
            fillGapsLinear(comp);
    }

    return result;
}

} // namespace despike
} // namespace adv
