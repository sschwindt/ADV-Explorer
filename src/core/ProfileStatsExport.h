/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include <QString>

namespace adv {

class ProjectModel;

/// Statistics exports (xlsx workbooks and CSV).
namespace statsexport {

/// Per x-y-z point statistics workbook (one row per point):
/// coordinates, depth, mean/std/stderr/skewness/kurtosis per component,
/// stresses, TKE, dissipation, magnitude and direction.
bool writePointStats(const ProjectModel &model, const QString &filePath,
                     QString *errorString = nullptr);

/// Fill the ADV-profiles template workbook with per-profile vertical stats
/// (one row per z-point, grouped by x-y profile) including absolute z and
/// relative z/h depth columns plus magnitude/direction formulas.
bool fillProfileTemplate(const ProjectModel &model, const QString &templatePath,
                         const QString &outputPath, QString *errorString = nullptr);

/// The column order shared with tools/make_template.py.
QStringList profileTemplateColumns();

} // namespace statsexport

} // namespace adv
