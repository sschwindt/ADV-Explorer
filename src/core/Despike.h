/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include <QMap>
#include <QString>
#include <QVector>

namespace adv {

/// Configuration of the chainable despiking filters of one measurement point.
struct DespikeConfig {
    // correlation score threshold: remove rows whose average beam correlation
    // falls below the threshold (SonTek/Nortek recommendation: 70)
    bool corrEnabled = false;
    double corrThreshold = 70.0;

    // signal-to-noise ratio threshold: remove rows with average SNR below it
    bool snrEnabled = false;
    double snrThreshold = 20.0;

    // velocity threshold: remove samples with |x - mean| > k * std per component
    bool velEnabled = false;
    double velK = 3.0;

    // Goring & Nikora (2002) thresholding as ported from tke-calculator rmspike.py
    enum class GnMethod { Off, Velocity, Acceleration };
    GnMethod gnMethod = GnMethod::Off;
    double gnLambdaA = 1.0; ///< acceleration threshold multiplier of g
    double gnK = 3.0;       ///< velocity threshold multiplier of std

    // phase-space thresholding (Goring & Nikora 2002; cf. proadv), iterative
    bool pstEnabled = false;

    // replacement of removed samples
    enum class Replace { NaN, LinearInterpolation };
    Replace replace = Replace::LinearInterpolation;

    bool anyEnabled() const
    {
        return corrEnabled || snrEnabled || velEnabled
               || gnMethod != GnMethod::Off || pstEnabled;
    }
};

/// Input series for despiking; w2, corrAvg and snrAvg may be empty.
struct DespikeInput {
    QVector<double> u, v, w1, w2;
    QVector<double> corrAvg; ///< row-average correlation score
    QVector<double> snrAvg;  ///< row-average SNR
    double samplingFrequency = 200.0;
};

/// Cleaned series plus per-filter spike counts.
struct DespikeResult {
    QVector<double> u, v, w1, w2;
    QMap<QString, int> spikeCounts; ///< filter name -> removed samples
    int totalRemoved = 0;
};

namespace despike {

DespikeResult apply(const DespikeInput &input, const DespikeConfig &config);

/// Replace NaN runs by linear interpolation between finite neighbors
/// (edges take the nearest finite value). Returns number of filled samples.
int fillGapsLinear(QVector<double> *values);

/// Phase-space thresholding of one series; flagged samples become NaN.
/// Returns the number of newly flagged samples.
int phaseSpaceThreshold(QVector<double> *values);

} // namespace despike

} // namespace adv
