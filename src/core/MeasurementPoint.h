/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "AdvData.h"
#include "Despike.h"
#include "FlowStats.h"
#include "Rotation.h"

#include <QString>
#include <QUuid>

namespace adv {

/// One user-defined measurement point: position, water depth, time window,
/// raw data and despiking configuration.
struct MeasurementPoint {
    QUuid id = QUuid::createUuid();
    double x = 0.0; ///< streamwise position (m), origin at center inlet
    double y = 0.0; ///< transverse position (m), left bank negative
    double z = 0.0; ///< height above the flume bottom (m)
    double waterDepth = nan(); ///< total water depth h at this x-y (m); NaN = unset
    double tStart = nan();     ///< analysis window start (s); NaN = record start
    double tEnd = nan();       ///< analysis window end (s); NaN = record end

    AdvData data;
    DespikeConfig despike;

    bool hasWaterDepth() const { return std::isfinite(waterDepth); }
    QString label() const;
    /// Key identifying the vertical profile this point belongs to.
    QString xyKey() const;
    static QString makeXyKey(double x, double y);
};

/// Fully processed series of one point: time window, despiking and rotation
/// correction applied.
struct ProcessedSeries {
    QVector<double> time;
    QVector<double> u, v, w1, w2;
    QVector<double> tkeInst;      ///< instantaneous 0.5 (u'^2+v'^2+w'^2)
    PointStats stats;             ///< turbulence statistics (chosen w component)
    QMap<QString, int> spikeCounts;
    double samplingFrequency = 200.0;

    bool isValid() const { return !time.isEmpty(); }
};

/// Run the processing pipeline of one point:
/// slice time window -> despike -> rotate -> statistics.
/// wRole selects which vertical beam (W1 or W2) enters w-based statistics.
ProcessedSeries processPoint(const MeasurementPoint &point,
                             const RotationAngles &correction,
                             Role wRole = Role::W1);

} // namespace adv
