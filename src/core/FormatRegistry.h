/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "core/AdvData.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace adv {

/// One supported measurement file format.
///
/// The registry exists because the file suffix alone cannot decide which reader
/// to use: a FlowTracker2 raw export is named `<survey>.ft.dat.csv`, whose
/// QFileInfo::suffix() is "csv", so plain suffix dispatch would hand it to the
/// generic CSV reader and quietly produce nonsense. Matching therefore runs over
/// full file-name endings, most specific first.
struct FileFormat {
    /// Identifier persisted in project files. Never change an existing value.
    QString id;
    QString displayName;
    /// File-name endings, most specific first, e.g. {".ft.dat.csv"}.
    QStringList nameEndings;
    /// True when the user must map columns to roles before reading (generic CSV).
    bool needsColumnMapping = false;
    /// True when one file holds many measurement points and must be opened with
    /// a survey reader rather than with read().
    bool multiPoint = false;
    /// Reconstruct the series of a single point from stored or imported bytes.
    /// This is the path project loading uses; multi-point formats store their
    /// already extracted series, so it is valid for every format.
    std::function<AdvData(const QByteArray &, const QHash<Role, int> &, QString *)> read;
};

namespace formats {

const QVector<FileFormat> &all();

/// Look up by persisted identifier; nullptr when unknown.
const FileFormat *byId(const QString &id);

/// Look up by file name or path; nullptr when no format matches.
const FileFormat *byFilePath(const QString &path);

/// Filter string for QFileDialog covering every readable format.
QString openFileFilter();

} // namespace formats
} // namespace adv
