/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "core/ProjectSettings.h"

#include <QString>

namespace adv {
namespace labels {

/// Stable identifier of the derived instantaneous-TKE series.
///
/// It is what gets written into a project file, because the *displayed* name
/// depends on the mode: a project saved in field mode and reopened in lab mode
/// would otherwise fail to restore its TKE curve.
QString tkeSeriesId();

/// Legacy display string that older project files stored instead of the id.
QString legacyTkeSeriesName();

/// "TKE" in lab mode, "TKE proxy" in field mode.
///
/// A FlowTracker2 point is 60 samples at 2 Hz, so it resolves nothing above
/// 1 Hz and averages over 30 s. The variance it yields is a useful relative
/// indicator between stations, but it is not the turbulent kinetic energy a
/// laboratory record measures, and the two must never be compared as if they
/// were the same quantity.
QString tke(Mode mode);

/// Column heading with units, e.g. "TKE proxy (m^2/s^2)".
QString tkeColumn(Mode mode);

/// Name of the derived per-sample series shown in the time-series plot.
QString tkeSeriesName(Mode mode);

/// Whether a dissipation rate can be estimated at all from a record of this
/// length and sampling rate.
///
/// The spectral estimator needs at least 256 samples for one Welch segment, and
/// an inertial subrange between 1 Hz and the quarter of the sampling rate. A
/// FlowTracker2 point satisfies neither, which is why eps currently comes out
/// as a silent NaN rather than as an explicit "not available".
bool epsEstimable(int sampleCount, double samplingFrequency);

/// Formatted dissipation rate, or the reason it is unavailable.
QString epsText(double eps, int sampleCount, double samplingFrequency);

/// Axis and coordinate names: "x (m)" in the flume, "easting (m)" in the field.
QString xAxisName(Mode mode);
QString yAxisName(Mode mode);

} // namespace labels
} // namespace adv
