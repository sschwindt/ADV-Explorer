/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "core/AdvData.h"

#include <QDateTime>
#include <QString>
#include <QVector>

namespace adv {

/// One SonTek FlowTracker2 point measurement: a station sampled at one
/// fractional depth for the configured averaging time.
struct FtPoint {
    QString id;                        ///< instrument UUID of the point measurement
    int stationIndex = -1;
    int pointIndex = -1;
    double fractionalDepth = nan();    ///< 0.2, 0.6 or 0.8 of the depth below the surface
    double distanceFromBottom = nan(); ///< height above the bed (m), i.e. the z coordinate
    QDateTime startTime;

    /// Sample indices the instrument itself flagged as spikes. They are written
    /// as NaN into the velocity columns, which is what makes the statistics
    /// reproduce the instrument's own despiked output.
    QVector<int> instrumentSpikes;

    /// The instrument's own despiked reference statistics of the streamwise
    /// component, used by the tests to prove the extraction is faithful.
    ///
    /// Careful: the FlowTracker2 firmware reports a *sample* standard deviation
    /// (ddof = 1), whereas SeriesStats::std is the population value (ddof = 0,
    /// matching numpy nanstd and the tke-calculator predecessor). The two differ
    /// by a factor sqrt(n / (n - 1)), which at n = 58 is 0.9%. The application
    /// deliberately keeps its own convention; this field stores the instrument
    /// value unchanged.
    double referenceMeanU = nan();
    double referenceSampleStdU = nan();
    int referenceCount = 0;

    AdvData data;
};

/// A vertical of a FlowTracker2 cross section, at one position along the tape.
struct FtStation {
    QString id;
    int index = -1;
    double location = nan();  ///< chainage along the tape (m)
    double depth = nan();     ///< total water depth (m)
    QString stationType;      ///< "LeftBank", "OpenWater" or "RightBank"
    QString velocityMethod;   ///< "None", "SixTenths", "TwoTenthsSixTenthsEightTenths", ...
    bool hasGps = false;
    double latitude = nan();
    double longitude = nan();
    QVector<FtPoint> points;

    /// Bank and edge stations record a depth and a chainage but no velocity.
    /// They cannot become measurement points; they define the cross-section ends.
    bool isBank() const { return points.isEmpty() || stationType != QLatin1String("OpenWater"); }
};

/// One FlowTracker2 discharge measurement, i.e. one `.ft` file.
struct FtSurvey {
    QString siteName;
    QString siteNumber;
    QString operatorName;
    QString sourceFileName;
    QDateTime startTime;
    QDateTime endTime;
    double samplingFrequency = 2.0; ///< Hz, typically 1 to 10
    double averagingSeconds = nan();
    double snrThresholdDb = nan();  ///< the instrument's own quality-control threshold
    QVector<FtStation> stations;

    int pointCount() const;
    /// Chainage of the first and last station; NaN when there are none.
    double firstChainage() const;
    double lastChainage() const;
};

/// Reader for the SonTek/Xylem FlowTracker2 `.ft` file, a ZIP archive holding a
/// `DataFile.json` survey description and one JSON document per point
/// measurement.
///
/// Beam quantities are mapped onto the x/y/z1 role slots (SnrX/SnrY/SnrZ1 and
/// CorrX/CorrY/CorrZ1) because processPoint() row-averages over whichever of
/// those roles exist. A FlowTracker2 probe has three beams and a single vertical
/// component, so the W2, SnrZ2 and CorrZ2 roles are never produced.
///
/// Correlation is stored on a 0 to 100 scale for consistency with the Vectrino
/// reader, but the instrument's score is not comparable: observed values run
/// from about 5 to 72 with a median near 35, so the Vectrino default threshold
/// of 70 would reject nearly every sample.
class FlowTrackerReader
{
public:
    static bool read(const QByteArray &ftBytes, FtSurvey *survey, QString *errorString = nullptr);
    static bool readFile(const QString &filePath, FtSurvey *survey, QString *errorString = nullptr);

    /// Serialise an extracted point series into the self-contained form stored
    /// in a project file: a header line of role names, then comma-separated
    /// rows. It is deliberately readable by CsvReader, so restoring a project
    /// needs no FlowTracker-specific load path.
    static QByteArray canonicalBytes(const AdvData &data);

    /// Role-to-column mapping of the canonical form, for CsvReader::read().
    static QHash<Role, int> canonicalMapping(const AdvData &data);
};

} // namespace adv
