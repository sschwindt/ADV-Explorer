/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "core/AdvData.h"

#include <QPointF>
#include <QString>
#include <QVector>

#include <cmath>

namespace adv {

/// Which kind of measurement campaign a project holds.
///
/// The two differ in more than presentation. A laboratory Vectrino record is
/// tens of thousands of samples at ~200 Hz in flume coordinates; a FlowTracker2
/// field record is 60 samples at 2 Hz at a georeferenced river station. The
/// mode therefore decides how the site is drawn, how x and y are interpreted,
/// and which turbulence quantities are meaningful enough to report as such.
///
/// In both modes z keeps the same meaning: height above the bed, in metres.
enum class Mode {
    Lab,   ///< Nortek Vectrino in a flume; the default
    Field, ///< SonTek FlowTracker2 on a river cross section
};

/// Identifier persisted in project files; never change an existing value.
inline QString modeId(Mode mode)
{
    return mode == Mode::Field ? QStringLiteral("field") : QStringLiteral("lab");
}

inline Mode modeFromId(const QString &id)
{
    return id == QStringLiteral("field") ? Mode::Field : Mode::Lab;
}

/// A surveyed river cross section: the line measurement stations sit on, plus
/// the bed profile along it.
///
/// FlowTracker2 records a chainage along a tape rather than a coordinate, so
/// the stations are placed by interpolating between the two bank positions.
/// Keeping the section means that placement stays reproducible after a project
/// is saved and reloaded, and gives the map something to draw besides markers.
struct CrossSection {
    QString name;
    double leftX = nan();
    double leftY = nan();
    double rightX = nan();
    double rightY = nan();
    double leftChainage = nan();
    double rightChainage = nan();
    /// (chainage, total water depth) pairs, bank stations included.
    QVector<QPointF> bed;

    bool isValid() const
    {
        return std::isfinite(leftX) && std::isfinite(leftY) && std::isfinite(rightX)
               && std::isfinite(rightY) && std::isfinite(leftChainage)
               && std::isfinite(rightChainage) && leftChainage != rightChainage;
    }

    /// Position of a station at the given chainage along the section.
    /// Returns false when the section is incomplete.
    bool positionAt(double chainage, double *x, double *y) const
    {
        if (!isValid())
            return false;
        const double t = (chainage - leftChainage) / (rightChainage - leftChainage);
        *x = leftX + t * (rightX - leftX);
        *y = leftY + t * (rightY - leftY);
        return true;
    }
};

} // namespace adv
