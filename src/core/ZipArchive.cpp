/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "core/ZipArchive.h"

#include <miniz.h>

#include <memory>

namespace adv {
namespace {
constexpr qint64 kMaxEntrySize = 64LL * 1024 * 1024;

QString lastError(mz_zip_archive *zip)
{
    return QString::fromLatin1(mz_zip_get_error_string(mz_zip_get_last_error(zip)));
}
} // namespace

struct ZipArchive::Private {
    QByteArray bytes;      ///< miniz reads straight out of this buffer
    mz_zip_archive zip{};
    bool open = false;

    ~Private()
    {
        if (open)
            mz_zip_reader_end(&zip);
    }
};

ZipArchive::ZipArchive()
    : d(std::make_unique<Private>())
{
}

ZipArchive::~ZipArchive() = default;

qint64 ZipArchive::maxEntrySize()
{
    return kMaxEntrySize;
}

bool ZipArchive::open(const QByteArray &bytes, QString *errorString)
{
    if (d->open) {
        mz_zip_reader_end(&d->zip);
        d->open = false;
    }
    d->bytes = bytes;
    mz_zip_zero_struct(&d->zip);

    if (d->bytes.isEmpty()) {
        if (errorString)
            *errorString = QStringLiteral("The archive is empty.");
        return false;
    }

    if (!mz_zip_reader_init_mem(&d->zip, d->bytes.constData(),
                                static_cast<size_t>(d->bytes.size()), 0)) {
        if (errorString)
            *errorString = QStringLiteral("Not a readable ZIP archive: %1").arg(lastError(&d->zip));
        return false;
    }
    d->open = true;
    return true;
}

bool ZipArchive::isOpen() const
{
    return d->open;
}

QStringList ZipArchive::entryNames() const
{
    QStringList names;
    if (!d->open)
        return names;

    const mz_uint count = mz_zip_reader_get_num_files(&d->zip);
    names.reserve(static_cast<int>(count));
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&d->zip, i, &stat))
            continue;
        if (stat.m_is_directory)
            continue;
        names.append(QString::fromUtf8(stat.m_filename));
    }
    return names;
}

bool ZipArchive::contains(const QString &name) const
{
    if (!d->open)
        return false;
    return mz_zip_reader_locate_file(&d->zip, name.toUtf8().constData(), nullptr, 0) >= 0;
}

QByteArray ZipArchive::read(const QString &name, QString *errorString) const
{
    if (!d->open) {
        if (errorString)
            *errorString = QStringLiteral("No archive is open.");
        return {};
    }

    const int index = mz_zip_reader_locate_file(&d->zip, name.toUtf8().constData(), nullptr, 0);
    if (index < 0) {
        if (errorString)
            *errorString = QStringLiteral("The archive has no entry \"%1\".").arg(name);
        return {};
    }

    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&d->zip, static_cast<mz_uint>(index), &stat)) {
        if (errorString)
            *errorString = QStringLiteral("Cannot read the directory entry of \"%1\": %2")
                               .arg(name, lastError(&d->zip));
        return {};
    }
    // guard against a decompression bomb before allocating anything
    if (static_cast<qint64>(stat.m_uncomp_size) > kMaxEntrySize) {
        if (errorString)
            *errorString = QStringLiteral("Entry \"%1\" is %2 MB, which exceeds the %3 MB limit.")
                               .arg(name)
                               .arg(static_cast<qint64>(stat.m_uncomp_size) / (1024 * 1024))
                               .arg(kMaxEntrySize / (1024 * 1024));
        return {};
    }

    size_t size = 0;
    void *data = mz_zip_reader_extract_to_heap(&d->zip, static_cast<mz_uint>(index), &size, 0);
    if (!data) {
        if (errorString)
            *errorString = QStringLiteral("Cannot decompress \"%1\": %2")
                               .arg(name, lastError(&d->zip));
        return {};
    }

    const QByteArray result(static_cast<const char *>(data), static_cast<qsizetype>(size));
    mz_free(data);
    return result;
}

} // namespace adv
