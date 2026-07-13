/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "Rotation.h"

#include <cmath>

namespace adv {
namespace rotation {

RotationAngles propose(double meanU, double meanV, double meanW,
                       double vw, double vVar, double wVar)
{
    RotationAngles angles;
    if (std::isfinite(meanU) && std::isfinite(meanV))
        angles.heading = std::atan2(meanV, meanU);
    // mean streamwise velocity magnitude after the heading rotation
    const double uAfterHeading = std::hypot(meanU, meanV);
    if (std::isfinite(uAfterHeading) && std::isfinite(meanW))
        angles.pitch = std::atan2(meanW, uAfterHeading);
    if (std::isfinite(vw) && std::isfinite(vVar) && std::isfinite(wVar))
        angles.roll = 0.5 * std::atan2(2.0 * vw, vVar - wVar);
    return angles;
}

void apply(const RotationAngles &angles,
           QVector<double> *u, QVector<double> *v, QVector<double> *w)
{
    if (angles.isIdentity())
        return;
    const int n = std::min({u->size(), v->size(), w->size()});
    const double ch = std::cos(angles.heading), sh = std::sin(angles.heading);
    const double cp = std::cos(angles.pitch), sp = std::sin(angles.pitch);
    const double cr = std::cos(angles.roll), sr = std::sin(angles.roll);
    for (int i = 0; i < n; ++i) {
        double ui = (*u)[i], vi = (*v)[i], wi = (*w)[i];
        // heading (about z)
        double u1 = ui * ch + vi * sh;
        double v1 = -ui * sh + vi * ch;
        // pitch (about y)
        double u2 = u1 * cp + wi * sp;
        double w2 = -u1 * sp + wi * cp;
        // roll (about x)
        double v3 = v1 * cr + w2 * sr;
        double w3 = -v1 * sr + w2 * cr;
        (*u)[i] = u2;
        (*v)[i] = v3;
        (*w)[i] = w3;
    }
}

} // namespace rotation
} // namespace adv
