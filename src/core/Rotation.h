/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include <QVector>

namespace adv {

/// Probe-misalignment correction angles (radians) of one x-y vertical profile.
///
/// The experimenter may have mounted the probe slightly rotated; the
/// corrections rotate the velocity coordinate system so that the mean
/// transverse (V) and vertical (W) velocities vanish:
///  - heading: rotation about the z axis, zeroes mean V
///  - pitch:   rotation about the y axis, zeroes mean W
///  - roll:    rotation about the x axis, zeroes the residual v'w' coupling
struct RotationAngles {
    double heading = 0.0;
    double pitch = 0.0;
    double roll = 0.0;

    bool isIdentity() const { return heading == 0.0 && pitch == 0.0 && roll == 0.0; }
};

namespace rotation {

/// Propose correction angles from (profile-averaged) mean velocities and the
/// v'w' cross stress:
///   heading = atan2(meanV, meanU)
///   pitch   = atan2(meanW, meanU)   (applied after heading)
///   roll    = 0.5 atan2(2 v'w', v'^2 - w'^2)  (principal-axes rotation)
RotationAngles propose(double meanU, double meanV, double meanW,
                       double vw, double vVar, double wVar);

/// Apply heading, then pitch, then roll in place to the velocity series.
/// Heading:  u' =  u cos(t) + v sin(t);  v' = -u sin(t) + v cos(t)
/// Pitch:    u' =  u cos(p) + w sin(p);  w' = -u sin(p) + w cos(p)
/// Roll:     v' =  v cos(r) + w sin(r);  w' = -v sin(r) + w cos(r)
void apply(const RotationAngles &angles,
           QVector<double> *u, QVector<double> *v, QVector<double> *w);

} // namespace rotation

} // namespace adv
