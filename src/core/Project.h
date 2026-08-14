/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include <QString>

namespace adv {

class ProjectModel;

/// Save/load of standalone .advProj project files.
///
/// The file is a JSON document that embeds the raw bytes of every measurement
/// data file (zlib-compressed, base64-encoded) together with all point, filter,
/// correction and plot settings, so a project opens on any computer without
/// the original data paths.
namespace project {

bool save(const ProjectModel &model, const QString &filePath, QString *errorString = nullptr);

/// Load a project. `warning` receives a message for problems that were
/// recovered from rather than fatal, such as a stored coordinate system this
/// build cannot handle; the project still opens in that case.
bool load(ProjectModel *model, const QString &filePath, QString *errorString = nullptr,
          QString *warning = nullptr);

QString fileExtension();   // "advProj"
QString fileFilter();      // for QFileDialog

} // namespace project

} // namespace adv
