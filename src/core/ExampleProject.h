/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include <QString>

namespace adv {
class ProjectModel;
}

/// Ready-made demonstration projects built from the sample measurements
/// embedded in the application resources (see src/resources.qrc).
///
/// These are the projects behind Help > Load example. They are also what the
/// documentation screenshots are taken from, which is why they live here rather
/// than inside MainWindow: reading from the resources keeps them working in the
/// AppImage and the Windows zip, where there is no input-data/ directory and no
/// predictable working directory.
namespace examples {

/// Laboratory campaign: one five-point vertical plus a second location, from
/// the Vectrino tables in input-data/. Replaces the contents of `model`.
bool loadLab(adv::ProjectModel *model, QString *errorString = nullptr);

/// Field campaign: a FlowTracker2 cross section placed on a real river site in
/// ETRS89 / UTM zone 32N. Replaces the contents of `model`.
bool loadField(adv::ProjectModel *model, QString *errorString = nullptr);

} // namespace examples
