/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "core/StatsLabels.h"

#include <cmath>

namespace adv {
namespace labels {

QString tkeSeriesId()
{
    return QStringLiteral("@tke");
}

QString legacyTkeSeriesName()
{
    return QStringLiteral("TKE inst. (m^2/s^2)");
}

QString tke(Mode mode)
{
    return mode == Mode::Field ? QStringLiteral("TKE proxy") : QStringLiteral("TKE");
}

QString tkeColumn(Mode mode)
{
    return tke(mode) + QStringLiteral(" (m^2/s^2)");
}

QString tkeSeriesName(Mode mode)
{
    return tke(mode) + QStringLiteral(" inst. (m^2/s^2)");
}

bool epsEstimable(int sampleCount, double samplingFrequency)
{
    // mirrors what welchPsd() and dissipationRate() already enforce:
    // one Welch segment is 256 samples, and the fitted band runs from 1 Hz to
    // half the Nyquist frequency
    if (sampleCount < 256 || !std::isfinite(samplingFrequency))
        return false;
    return 0.25 * samplingFrequency > 1.0;
}

QString epsText(double eps, int sampleCount, double samplingFrequency)
{
    if (!epsEstimable(sampleCount, samplingFrequency)) {
        return samplingFrequency <= 4.0
                   ? QStringLiteral("n/a (sampling rate too low)")
                   : QStringLiteral("n/a (record too short)");
    }
    if (!std::isfinite(eps))
        return QStringLiteral("n/a");
    return QString::number(eps, 'g', 4);
}

QString xAxisName(Mode mode)
{
    return mode == Mode::Field ? QStringLiteral("easting (m)") : QStringLiteral("x (m)");
}

QString yAxisName(Mode mode)
{
    return mode == Mode::Field ? QStringLiteral("northing (m)") : QStringLiteral("y (m)");
}

} // namespace labels
} // namespace adv
