/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "AdvData.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace adv {

/// Reader for generic delimited text files (UBERTONE exports, CSV, TSV).
///
/// Reading is a two-step process so a wizard can present a preview and let
/// the user assign semantic roles (time, u, v, w1, ...) to columns:
///   1. preview() sniffs the delimiter and returns header + first rows.
///   2. read() parses everything, applying a role -> column-index mapping.
class CsvReader
{
public:
    struct Preview {
        QChar delimiter;
        bool hasHeader = false;
        QStringList columnNames;         ///< header or "column 1", "column 2", ...
        QList<QStringList> sampleRows;   ///< up to maxRows parsed rows
        int columnCount = 0;
    };

    static Preview preview(const QByteArray &bytes, int maxRows = 10);

    /// Parse the file with the given role mapping (role -> column index).
    /// Unmapped columns are kept with their file header as Role::Other.
    static AdvData read(const QByteArray &bytes, const QHash<Role, int> &mapping,
                        QString *errorString = nullptr);
    static AdvData readFile(const QString &filePath, const QHash<Role, int> &mapping,
                            QString *errorString = nullptr);

    /// Guess a role mapping from header names (case-insensitive substring match).
    static QHash<Role, int> guessMapping(const QStringList &columnNames);
};

} // namespace adv
