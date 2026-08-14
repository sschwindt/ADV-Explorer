/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <memory>

namespace adv {

/// Read-only access to the entries of a ZIP archive held in memory.
///
/// SonTek FlowTracker2 `.ft` files are ZIP containers of JSON documents, so the
/// application needs to unpack them. The vendored miniz library does the work;
/// this wrapper keeps it out of every other header and adds the size limits that
/// a general-purpose unpacker should not be run without.
///
/// Note that FlowTracker2 writes central-directory entries whose 32-bit size
/// fields are 0xFFFFFFFF with the real sizes in a ZIP64 extra field, but without
/// the matching end-of-central-directory-64 record. Qt's private QZipReader
/// ignores extra fields and would report 4 GiB entries, which is why the
/// vendored library is used instead.
class ZipArchive
{
public:
    ZipArchive();
    ~ZipArchive();

    ZipArchive(const ZipArchive &) = delete;
    ZipArchive &operator=(const ZipArchive &) = delete;

    /// Parse the central directory of the archive. The bytes are copied, so the
    /// caller does not have to keep the original buffer alive.
    bool open(const QByteArray &bytes, QString *errorString = nullptr);

    bool isOpen() const;

    /// Names of all file entries, directories excluded.
    QStringList entryNames() const;

    bool contains(const QString &name) const;

    /// Decompress one entry. Returns an empty array and sets errorString when the
    /// entry is missing, corrupt, or larger than the size limit.
    QByteArray read(const QString &name, QString *errorString = nullptr) const;

    /// Largest accepted size of a single decompressed entry (64 MiB).
    static qint64 maxEntrySize();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace adv
