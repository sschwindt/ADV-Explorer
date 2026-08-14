/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "core/FlowTrackerReader.h"

#include <QByteArray>
#include <QString>

namespace adv {

/// Fallback reader for the CSV export of a FlowTracker2 measurement, for users
/// who kept only the exported tables and not the original `.ft` file.
///
/// The export splits one measurement across `<survey>.ft.dat.csv` (the raw
/// samples) and `<survey>.ft.sum.csv` (one row per point measurement). Both are
/// semicolon separated with decimal commas and a UTF-8 byte order mark, and
/// **their column headers are translated to the language of the FlowTracker2
/// user interface** (the reference files are German: "Jahr", "Tiefe",
/// "Winkel"). The column order is fixed, so this reader keys strictly on
/// position and never reads the header text.
///
/// Two things the export cannot provide, both of which the `.ft` file does:
///
/// * **No correlation columns.** The CorrX/CorrY/CorrZ1 roles are absent, so the
///   correlation despiking filter has nothing to work on and quietly does
///   nothing.
/// * **No spike indices.** The summary reports only how many samples the
///   instrument rejected, not which ones, so the series cannot be blanked and
///   the statistics are those of the raw record. They therefore differ slightly
///   from both the `.ft` result and the instrument's own summary values.
///
/// Bank and edge stations do not appear in the export either, so a cross section
/// read this way has no endpoints and the import wizard has to ask for them.
///
/// Prefer FlowTrackerReader whenever the `.ft` file is available.
class FlowTrackerCsvReader
{
public:
    /// Parse the raw-sample and summary exports into the same survey structure
    /// the `.ft` reader produces.
    static bool read(const QByteArray &datBytes, const QByteArray &sumBytes,
                     FtSurvey *survey, QString *errorString = nullptr);

    /// Read a `*.ft.dat.csv` together with its `*.ft.sum.csv` sibling.
    static bool readFile(const QString &datPath, FtSurvey *survey,
                         QString *errorString = nullptr);

    /// Path of the summary file belonging to a raw-sample export, whichever of
    /// the two the user picked. Empty when the name does not follow the export
    /// convention.
    static QString summaryPathFor(const QString &path);
};

} // namespace adv
