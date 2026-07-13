/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include <QVector>

namespace adv {

/// NaN-aware descriptive statistics of one series.
struct SeriesStats {
    double mean = 0.0;
    double std = 0.0;      ///< population standard deviation (ddof = 0, as numpy nanstd)
    double stderror = 0.0; ///< std / sqrt(n)
    double skewness = 0.0; ///< Fisher-Pearson g1
    double kurtosis = 0.0; ///< excess kurtosis g2
    int n = 0;             ///< number of finite samples
};

/// Turbulence statistics of one measurement point (time window applied upstream).
struct PointStats {
    SeriesStats u, v, w;
    double uv = 0.0;   ///< mean of u'v' (m^2/s^2)
    double uw = 0.0;   ///< mean of u'w' (m^2/s^2)
    double vw = 0.0;   ///< mean of v'w' (m^2/s^2)
    double tke = 0.0;  ///< 0.5 (std_u^2 + std_v^2 + std_w^2) (m^2/s^2)
    double eps = 0.0;  ///< turbulent dissipation rate (m^2/s^3), NaN if not estimable
    double magnitude = 0.0; ///< |(mean u, mean v, mean w)| (m/s)
    double direction = 0.0; ///< horizontal flow direction vs +x axis (degrees)
};

namespace flowstats {

SeriesStats seriesStats(const QVector<double> &values);
double nanMean(const QVector<double> &values);
/// Mean of the product of fluctuations of two series (Reynolds stress term).
double crossStress(const QVector<double> &a, const QVector<double> &b);

/// Full turbulence statistics from velocity components.
PointStats compute(const QVector<double> &u, const QVector<double> &v,
                   const QVector<double> &w, double samplingFrequency);

/// Instantaneous TKE-like fluctuation series 0.5 (u'^2 + v'^2 + w'^2).
QVector<double> instantaneousTke(const QVector<double> &u, const QVector<double> &v,
                                 const QVector<double> &w);

/// Turbulent dissipation rate from an inertial-subrange (-5/3) fit of the
/// streamwise velocity spectrum using Taylor's frozen turbulence hypothesis.
/// Returns NaN when the record is too short or the mean velocity is ~0.
double dissipationRate(const QVector<double> &u, double samplingFrequency);

/// Welch power spectral density estimate (Hann window, 50% overlap).
/// Returns frequency/psd pairs; empty when the record is too short.
void welchPsd(const QVector<double> &values, double samplingFrequency,
              QVector<double> *frequencies, QVector<double> *psd);

} // namespace flowstats

} // namespace adv
