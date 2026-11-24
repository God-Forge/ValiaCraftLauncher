#include "core/ZipWriter.h"

#include "core/Crc32.h"
#include "core/Deflate.h"

#include <QFileInfo>

namespace mcpacker {
namespace {

constexpr quint32 kLocalHeaderSig = 0x04034b50;
constexpr quint32 kCentralHeaderSig = 0x02014b50;
constexpr quint32 kEndOfCentralDirSig = 0x06054b50;
constexpr quint32 kZip64EndSig = 0x06064b50;
constexpr quint32 kZip64LocatorSig = 0x07064b50;

constexpr quint16 kFlagUtf8 = 0x0800;
constexpr quint64 kZip64Magic = 0xFFFFFFFFu;

/// 追加小端整数
void putU16(QByteArray &buf, quint16 v)
{
    buf.append(char(v & 0xFF));
    buf.append(char((v >> 8) & 0xFF));
}

void putU32(QByteArray &buf, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        buf.append(char((v >> (8 * i)) & 0xFF));
}

void putU64(QByteArray &buf, quint64 v)
{
    for (int i = 0; i < 8; ++i)
        buf.append(char((v >> (8 * i)) & 0xFF));
}

void dosDateTime(const QDateTime &dt, quint16 &dosTime, quint16 &dosDate)
{
    const QDateTime local = dt.isValid() ? dt.toLocalTime() : QDateTime::currentDateTime();
    const QDate d = local.date();
    const QTime t = local.time();
    int year = qBound(1980, d.year(), 2107);
    dosDate = quint16(((year - 1980) << 9) | (d.month() << 5) | d.day());
    dosTime = quint16((t.hour() << 11) | (t.minute() << 5) | (t.second() / 2));
}

} // namespace

ZipWriter::ZipWriter(const QString &filePath) : m_filePath(filePath) {}

ZipWriter::~ZipWriter()
{
    if (m_open) {
        // 出错时尽量别留下损坏文件
        m_file.close();
        m_file.remove();
    }
}

void ZipWriter::setError(const QString &message)
{
    m_error = message;
}

bool ZipWriter::open(QString *error)
{
    if (m_open)
        return true;
    m_file.setFileName(m_filePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(QStringLiteral("无法创建压缩包：%1").arg(m_file.errorString()));
        if (error)
            *error = m_error;
        return false;
    }
    m_open = true;
    m_written = 0;
    m_entries.clear();
    return true;
}

bool ZipWriter::writeRaw(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return true;
    const qint64 n = m_file.write(bytes);
    if (n != bytes.size()) {
        setError(QStringLiteral("写入压缩包失败：%1").arg(m_file.errorString()));
        return false;
    }
    m_written += n;
    return true;
}

bool ZipWriter::addFile(const QString &entryName, const QString &sourcePath, QString *error)
{
    QFileInfo fi(sourcePath);
    if (!fi.exists() || !fi.isFile()) {
        setError(QStringLiteral("文件不存在：%1").arg(sourcePath));
        if (error)
            *error = m_error;
        return false;
    }
    QFile f(sourcePath);
    if (!f.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("无法读取文件：%1（%2）").arg(sourcePath, f.errorString()));
        if (error)
            *error = m_error;
        return false;
    }
    const QByteArray data = f.readAll();
    f.close();
    return writeEntry(entryName, data, fi.lastModified(), error);
}

bool ZipWriter::addData(const QString &entryName, const QByteArray &data, QString *error)
{
    return writeEntry(entryName, data, QDateTime::currentDateTime(), error);
}

bool ZipWriter::addEmptyDirectory(const QString &entryName, QString *error)
{
    QString name = entryName;
    if (!name.endsWith(QLatin1Char('/')))
        name += QLatin1Char('/');

    Entry e;
    e.name = name;
    e.method = 0;
    e.flags = kFlagUtf8;
    dosDateTime(QDateTime::currentDateTime(), e.modTime, e.modDate);
    e.isDirectory = true;
    e.localOffset = quint64(m_file.pos());

    QByteArray local;
    putU32(local, kLocalHeaderSig);
    putU16(local, 20);
    putU16(local, e.flags);
    putU16(local, e.method);
    putU16(local, e.modTime);
    putU16(local, e.modDate);
    putU32(local, 0);
    putU32(local, 0);
    putU32(local, 0);
    const QByteArray nameBytes = name.toUtf8();
    putU16(local, quint16(nameBytes.size()));
    putU16(local, 0);
    local.append(nameBytes);

    if (!writeRaw(local)) {
        if (error)
            *error = m_error;
        return false;
    }
    m_entries.append(e);
    return true;
}

bool ZipWriter::writeEntry(const QString &entryName, const QByteArray &raw, const QDateTime &modTime,
                           QString *error)
{
    if (!m_open) {
        setError(QStringLiteral("压缩包尚未打开"));
        if (error)
            *error = m_error;
        return false;
    }

    QString name = entryName;
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (name.startsWith(QLatin1Char('/')))
        name.remove(0, 1);

    const QByteArray data = raw;
    QByteArray payload;
    quint16 method = 0;
    if (!data.isEmpty() && data.size() <= kMaxCompressSize) {
        const QByteArray compressed = deflateRaw(data);
        if (!compressed.isEmpty() && compressed.size() < data.size()) {
            payload = compressed;
            method = 8;
        }
    }
    if (payload.isEmpty())
        payload = data;

    Entry e;
    e.name = name;
    e.method = method;
    e.flags = kFlagUtf8;
    dosDateTime(modTime, e.modTime, e.modDate);
    e.crc = data.isEmpty() ? 0u : crc32(data.constData(), data.size());
    e.uncompressedSize = quint64(data.size());
    e.compressedSize = quint64(payload.size());
    e.localOffset = quint64(m_file.pos());

    const QByteArray nameBytes = name.toUtf8();

    // 是否需要 Zip64 扩展字段
    const bool needZip64Local =
        e.uncompressedSize >= kZip64Magic || e.compressedSize >= kZip64Magic;

    QByteArray local;
    putU32(local, kLocalHeaderSig);
    putU16(local, needZip64Local ? 45 : 20);
    putU16(local, e.flags);
    putU16(local, e.method);
    putU16(local, e.modTime);
    putU16(local, e.modDate);
    putU32(local, e.crc);
    putU32(local, needZip64Local ? quint32(0xFFFFFFFFu) : quint32(e.compressedSize));
    putU32(local, needZip64Local ? quint32(0xFFFFFFFFu) : quint32(e.uncompressedSize));
    putU16(local, quint16(nameBytes.size()));

    QByteArray extra;
    if (needZip64Local) {
        putU16(extra, 0x0001);
        putU16(extra, quint16(16));
        putU64(extra, e.uncompressedSize);
        putU64(extra, e.compressedSize);
    }
    putU16(local, quint16(extra.size()));
    local.append(nameBytes);
    local.append(extra);

    if (!writeRaw(local) || !writeRaw(payload)) {
        if (error)
            *error = m_error;
        return false;
    }

    m_entries.append(e);
    if (onEntryWritten)
        onEntryWritten(name, qint64(data.size()));
    return true;
}

bool ZipWriter::close(QString *error)
{
    if (!m_open)
        return true;

    const quint64 centralOffset = quint64(m_file.pos());
    QByteArray central;
    bool needZip64Eocd = false;

    for (const Entry &e : std::as_const(m_entries)) {
        const QByteArray nameBytes = e.name.toUtf8();

        QByteArray extra;
        bool needOffset64 = e.localOffset >= kZip64Magic;
        bool needSize64 =
            e.uncompressedSize >= kZip64Magic || e.compressedSize >= kZip64Magic;
        if (needOffset64 || needSize64) {
            QByteArray z64;
            putU64(z64, e.uncompressedSize);
            putU64(z64, e.compressedSize);
            putU64(z64, e.localOffset);
            putU16(extra, 0x0001);
            putU16(extra, quint16(z64.size()));
            extra.append(z64);
            needZip64Eocd = true;
        }

        putU32(central, kCentralHeaderSig);
        putU16(central, 20); // version made by（MS-DOS，2.0）
        putU16(central, needSize64 ? 45 : 20);
        putU16(central, e.flags);
        putU16(central, e.method);
        putU16(central, e.modTime);
        putU16(central, e.modDate);
        putU32(central, e.crc);
        putU32(central, needSize64 ? quint32(0xFFFFFFFFu) : quint32(e.compressedSize));
        putU32(central, needSize64 ? quint32(0xFFFFFFFFu) : quint32(e.uncompressedSize));
        putU16(central, quint16(nameBytes.size()));
        putU16(central, quint16(extra.size()));
        putU16(central, 0); // comment length
        putU16(central, 0); // disk start
        putU16(central, 0); // internal attrs
        putU32(central, e.isDirectory ? 0x10u : 0u); // external attrs
        putU32(central, needOffset64 ? quint32(0xFFFFFFFFu) : quint32(e.localOffset));
        central.append(nameBytes);
        central.append(extra);
    }

    const quint64 centralSize = quint64(central.size());
    if (!writeRaw(central)) {
        if (error)
            *error = m_error;
        return false;
    }

    if (quint64(m_entries.size()) > 0xFFFF || centralOffset >= kZip64Magic
        || centralSize >= kZip64Magic) {
        needZip64Eocd = true;
    }

    if (needZip64Eocd) {
        const quint64 zip64EndOffset = quint64(m_file.pos());
        QByteArray z64;
        putU32(z64, kZip64EndSig);
        putU64(z64, 44); // record size
        putU16(z64, 45);
        putU16(z64, 45);
        putU32(z64, 0);
        putU32(z64, 0);
        putU64(z64, quint64(m_entries.size()));
        putU64(z64, quint64(m_entries.size()));
        putU64(z64, centralSize);
        putU64(z64, centralOffset);

        QByteArray locator;
        putU32(locator, kZip64LocatorSig);
        putU32(locator, 0);
        putU64(locator, zip64EndOffset);
        putU32(locator, 1);

        if (!writeRaw(z64) || !writeRaw(locator)) {
            if (error)
                *error = m_error;
            return false;
        }
    }

    QByteArray eocd;
    putU32(eocd, kEndOfCentralDirSig);
    putU16(eocd, 0);
    putU16(eocd, 0);
    const quint64 totalEntries = quint64(m_entries.size());
    if (needZip64Eocd) {
        putU16(eocd, 0xFFFF);
        putU16(eocd, 0xFFFF);
        putU32(eocd, 0xFFFFFFFFu);
        putU32(eocd, 0xFFFFFFFFu);
    } else {
        putU16(eocd, quint16(totalEntries));
        putU16(eocd, quint16(totalEntries));
        putU32(eocd, quint32(centralSize));
        putU32(eocd, quint32(centralOffset));
    }
    putU16(eocd, 0);

    if (!writeRaw(eocd)) {
        if (error)
            *error = m_error;
        return false;
    }

    m_file.flush();
    m_file.close();
    m_open = false;
    return true;
}

} // namespace mcpacker
