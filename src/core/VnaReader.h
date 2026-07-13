/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "AdvData.h"

#include <QByteArray>
#include <QString>

namespace adv {

/// Reader for Nortek Vectrino ASCII export files (.vna).
///
/// The column layout follows tke-calculator's read_vna: 20 whitespace-
/// separated columns of which column 0 (status) and column 3 (error code)
/// are skipped:
///   [skip] time(s) sample [skip] u v w1 w2 amp_x amp_y amp_z1 amp_z2
///   snr_x snr_y snr_z1 snr_z2 corr_x corr_y corr_z1 corr_z2
class VnaReader
{
public:
    /// Parse the contents of a .vna file. Returns an empty AdvData and sets
    /// errorString on failure.
    static AdvData read(const QByteArray &bytes, QString *errorString = nullptr);
    /// Convenience overload reading from disk (also stores raw bytes).
    static AdvData readFile(const QString &filePath, QString *errorString = nullptr);

    /// Extract x, y, z in meters from a file name of the form XX_YY_ZZ_*.vna
    /// (coordinates in centimeters, leading "__" meaning negative).
    /// Non-parsable parts yield NaN.
    static void coordinatesFromFileName(const QString &fileName,
                                        double *x, double *y, double *z);
};

} // namespace adv
