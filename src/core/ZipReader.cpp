#include "core/ZipReader.h"

#include "core/Crc32.h"
#include "core/Deflate.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

namespace mcpacker {
namespace {

constexpr quint32 kCentralHeaderSig = 0x02014b50;
constexpr quint32 kEndOfCentralDirSig = 0x06054b50;
constexpr quint32 kZip64EndSig = 0x06064b50;
constexpr quint32 kZip64LocatorSig = 0x07064b50;
constexpr quint32 kLocalHeaderSig = 0x04034b50;

quint16 readU16(const QByteArray &b, qsizetype off)
{
    return quint16(quint8(b.at(off)) | (quint16(quint8(b.at(off + 1))) << 8));
}

quint32 readU32(const QByteArray &b, qsizetype off)
{
    return quint32(quint8(b.at(off))) | (quint32(quint8(b.at(off + 1))) << 8)
           | (quint32(quint8(b.at(off + 2))) << 16) | (quint32(quint8(b.at(off + 3))) << 24);
}

quint64 readU64(const QByteArray &b, qsizetype off)
{
    quint64 v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | quint64(quint8(b.at(off + i)));
    return v;
}

} // namespace

ZipReader::~ZipReader()
{
    close();
}

void ZipReader::close()
{
    if (m_file.isOpen())
        m_file.close();
    m_open = false;
    m_entries.clear();
    m_totalBytes = 0;
}

bool ZipReader::open(const QString &filePath, QString *error)
{
    close();
    m_filePath = filePath;
    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("无法打开压缩包：%1").arg(m_file.errorString());
        if (error)
            *error = m_error;
        return false;
    }
    m_archiveSize = m_file.size();
    if (!loadCentralDirectory(error)) {
        m_file.close();
        return false;
    }
    m_open = true;
    return true;
}

bool ZipReader::loadCentralDirectory(QString *error)
{
    if (m_archiveSize < 22) {
        m_error = QStringLiteral("文件太小，不是有效的 ZIP 压缩包");
        if (error)
            *error = m_error;
        return false;
    }

    // 从尾部向前搜索 EOCD（最多 65557 字节）
    const qint64 searchLen = qMin<qint64>(m_archiveSize, 22 + 65535);
    if (!m_file.seek(m_archiveSize - searchLen)) {
        m_error = QStringLiteral("无法读取压缩包尾部");
        if (error)
            *error = m_error;
        return false;
    }
    const QByteArray tail = m_file.read(searchLen);
    if (tail.size() != searchLen) {
        m_error = QStringLiteral("读取压缩包尾部失败");
        if (error)
            *error = m_error;
        return false;
    }

    qsizetype eocdPos = -1;
    for (qsizetype i = tail.size() - 22; i >= 0; --i) {
        if (readU32(tail, i) == kEndOfCentralDirSig) {
            eocdPos = i;
            break;
        }
    }
    if (eocdPos < 0) {
        m_error = QStringLiteral("未找到 ZIP 目录结束标记（EOCD），文件可能已损坏");
        if (error)
            *error = m_error;
        return false;
    }

    quint64 totalEntries = readU16(tail, eocdPos + 10);
    quint64 cdSize = readU32(tail, eocdPos + 12);
    quint64 cdOffset = readU32(tail, eocdPos + 16);
    const bool needZip64 =
        totalEntries == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu;

    if (needZip64) {
        // locator 位于 EOCD 之前 20 字节
        const qint64 locatorFilePos = m_archiveSize - searchLen + eocdPos - 20;
        if (locatorFilePos >= 0 && m_file.seek(locatorFilePos)) {
            const QByteArray loc = m_file.read(20);
            if (loc.size() == 20 && readU32(loc, 0) == kZip64LocatorSig) {
                const quint64 z64Offset = readU64(loc, 8);
                if (m_file.seek(qint64(z64Offset))) {
                    const QByteArray z64 = m_file.read(56);
                    if (z64.size() >= 56 && readU32(z64, 0) == kZip64EndSig) {
                        totalEntries = readU64(z64, 32);
                        cdSize = readU64(z64, 40);
                        cdOffset = readU64(z64, 48);
                    } else {
                        m_error = QStringLiteral("Zip64 目录结束记录损坏");
                        if (error)
                            *error = m_error;
                        return false;
                    }
                }
            }
        }
    }

    if (cdOffset > quint64(m_archiveSize)) {
        m_error = QStringLiteral("ZIP 中央目录偏移越界，文件可能已损坏");
        if (error)
            *error = m_error;
        return false;
    }

    if (!m_file.seek(qint64(cdOffset))) {
        m_error = QStringLiteral("无法定位 ZIP 中央目录");
        if (error)
            *error = m_error;
        return false;
    }
    const QByteArray cd = m_file.read(qint64(cdSize));
    if (quint64(cd.size()) != cdSize) {
        m_error = QStringLiteral("读取 ZIP 中央目录失败");
        if (error)
            *error = m_error;
        return false;
    }

    m_entries.clear();
    m_totalBytes = 0;
    qsizetype pos = 0;
    for (quint64 i = 0; i < totalEntries; ++i) {
        if (pos + 46 > cd.size() || readU32(cd, pos) != kCentralHeaderSig)
            break;

        ZipEntry e;
        e.flags = readU16(cd, pos + 8);
        e.method = readU16(cd, pos + 10);
        e.crc = readU32(cd, pos + 16);
        e.compressedSize = readU32(cd, pos + 20);
        e.uncompressedSize = readU32(cd, pos + 24);
        const quint16 nameLen = readU16(cd, pos + 28);
        const quint16 extraLen = readU16(cd, pos + 30);
        const quint16 commentLen = readU16(cd, pos + 32);
        e.localOffset = readU32(cd, pos + 42);

        const qsizetype nameOff = pos + 46;
        const qsizetype extraOff = nameOff + nameLen;
        const qsizetype commentOff = extraOff + extraLen;
        if (commentOff + commentLen > cd.size())
            break;

        e.name = QString::fromUtf8(cd.constData() + nameOff, nameLen);

        // 解析 Zip64 扩展字段
        qsizetype p = extraOff;
        const qsizetype extraEnd = extraOff + extraLen;
        while (p + 4 <= extraEnd) {
            const quint16 id = readU16(cd, p);
            const quint16 len = readU16(cd, p + 2);
            if (p + 4 + len > extraEnd)
                break;
            if (id == 0x0001) {
                qsizetype q = p + 4;
                if (e.uncompressedSize == 0xFFFFFFFFu && q + 8 <= p + 4 + len) {
                    e.uncompressedSize = readU64(cd, q);
                    q += 8;
                }
                if (e.compressedSize == 0xFFFFFFFFu && q + 8 <= p + 4 + len) {
                    e.compressedSize = readU64(cd, q);
                    q += 8;
                }
                if (e.localOffset == 0xFFFFFFFFu && q + 8 <= p + 4 + len) {
                    e.localOffset = readU64(cd, q);
                }
            }
            p += 4 + len;
        }

        m_entries.append(e);
        m_totalBytes += qint64(e.uncompressedSize);
        pos = commentOff + commentLen;
    }

    if (m_entries.isEmpty() && totalEntries > 0) {
        m_error = QStringLiteral("ZIP 中央目录解析失败，未读取到任何条目");
        if (error)
            *error = m_error;
        return false;
    }
    return true;
}

const ZipEntry *ZipReader::find(const QString &entryName) const
{
    for (const ZipEntry &e : m_entries) {
        if (e.name == entryName)
            return &e;
    }
    return nullptr;
}

QByteArray ZipReader::read(const ZipEntry &entry, QString *error) const
{
    if (!m_open) {
        if (error)
            *error = QStringLiteral("压缩包未打开");
        return {};
    }
    if (!m_file.seek(qint64(entry.localOffset))) {
        if (error)
            *error = QStringLiteral("无法定位条目 %1").arg(entry.name);
        return {};
    }
    const QByteArray header = m_file.read(30);
    if (header.size() != 30 || readU32(header, 0) != kLocalHeaderSig) {
        if (error)
            *error = QStringLiteral("条目 %1 的本地文件头损坏").arg(entry.name);
        return {};
    }
    const quint16 nameLen = readU16(header, 26);
    const quint16 extraLen = readU16(header, 28);
    if (!m_file.seek(qint64(entry.localOffset) + 30 + nameLen + extraLen)) {
        if (error)
            *error = QStringLiteral("条目 %1 数据偏移非法").arg(entry.name);
        return {};
    }
    const QByteArray compressed = m_file.read(qint64(entry.compressedSize));
    if (quint64(compressed.size()) != entry.compressedSize) {
        if (error)
            *error = QStringLiteral("条目 %1 数据不完整").arg(entry.name);
        return {};
    }

    QByteArray out;
    if (entry.method == 0) {
        out = compressed;
    } else if (entry.method == 8) {
        QString inflateError;
        const quint32 expectedSize =
            entry.uncompressedSize <= 0xFFFFFFFEu ? quint32(entry.uncompressedSize) : 0u;
        out = inflateRaw(compressed, expectedSize, &inflateError);
        if (out.isEmpty() && entry.uncompressedSize != 0) {
            if (error)
                *error = QStringLiteral("条目 %1 解压失败：%2").arg(entry.name, inflateError);
            return {};
        }
    } else {
        if (error)
            *error = QStringLiteral("条目 %1 使用了不支持的压缩方法 %2")
                         .arg(entry.name)
                         .arg(entry.method);
        return {};
    }

    if (quint64(out.size()) != entry.uncompressedSize) {
        if (error)
            *error = QStringLiteral("条目 %1 解压长度不符：期望 %2，实际 %3")
                         .arg(entry.name)
                         .arg(entry.uncompressedSize)
                         .arg(out.size());
        return {};
    }

    const quint32 actualCrc = out.isEmpty() ? 0u : crc32(out.constData(), out.size());
    if (actualCrc != entry.crc) {
        if (error)
            *error = QStringLiteral("条目 %1 CRC 校验失败（数据损坏）").arg(entry.name);
        return {};
    }
    return out;
}

QString ZipReader::safeTargetPath(const QString &destDir, const QString &entryName)
{
    QString name = entryName;
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (name.startsWith(QLatin1Char('/')))
        name.remove(0, 1);

    const QStringList parts = name.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == QStringLiteral("..") || part == QStringLiteral("."))
            return {};
        if (part.contains(QLatin1Char(':')))
            return {};
    }
    if (parts.isEmpty())
        return {};

    const QString base = QDir(destDir).absolutePath();
    const QString target = QDir(base).absoluteFilePath(parts.join(QLatin1Char('/')));
    const QString canonicalBase = QDir::cleanPath(base);
    const QString canonicalTarget = QDir::cleanPath(target);
    if (!canonicalTarget.startsWith(canonicalBase + QLatin1Char('/')))
        return {};
    return canonicalTarget;
}

bool ZipReader::extract(const QString &destDir, ConflictPolicy policy,
                        const ZipReader::ExtractProgress &progress, QStringList *written,
                        QStringList *skipped, QStringList *failed, QString *error)
{
    if (!m_open) {
        if (error)
            *error = QStringLiteral("压缩包未打开");
        return false;
    }
    QDir dir(destDir);
    if (!dir.exists() && !QDir().mkpath(destDir)) {
        if (error)
            *error = QStringLiteral("无法创建目标目录：%1").arg(destDir);
        return false;
    }

    const qint64 totalBytes = qMax<qint64>(m_totalBytes, 1);
    qint64 doneBytes = 0;
    int counter = 0;

    for (const ZipEntry &entry : m_entries) {
        ++counter;
        if (entry.isDirectory()) {
            const QString target = safeTargetPath(destDir, entry.name);
            if (!target.isEmpty())
                QDir().mkpath(target);
            if (progress)
                progress(doneBytes, totalBytes, entry.name);
            continue;
        }

        const QString target = safeTargetPath(destDir, entry.name);
        if (target.isEmpty()) {
            if (failed)
                failed->append(entry.name + QStringLiteral("（非法路径，已跳过）"));
            continue;
        }

        QString finalPath = target;
        if (QFileInfo::exists(finalPath)) {
            if (policy == ConflictPolicy::Skip) {
                if (skipped)
                    skipped->append(entry.name);
                doneBytes += qint64(entry.uncompressedSize);
                if (progress)
                    progress(doneBytes, totalBytes, entry.name);
                continue;
            }
            if (policy == ConflictPolicy::Rename) {
                const QFileInfo fi(finalPath);
                const QString dirPath = fi.absolutePath();
                const QString base = fi.completeBaseName();
                const QString suffix = fi.suffix();
                for (int n = 2; n < 10000; ++n) {
                    const QString candidate =
                        suffix.isEmpty()
                            ? QStringLiteral("%1/%2 (%3)").arg(dirPath, base).arg(n)
                            : QStringLiteral("%1/%2 (%3).%4").arg(dirPath, base).arg(n).arg(suffix);
                    if (!QFileInfo::exists(candidate)) {
                        finalPath = candidate;
                        break;
                    }
                }
            }
        }

        QDir().mkpath(QFileInfo(finalPath).absolutePath());

        QString readError;
        const QByteArray data = read(entry, &readError);
        if (data.isNull() && !readError.isEmpty()) {
            if (failed)
                failed->append(entry.name + QStringLiteral("（%1）").arg(readError));
            continue;
        }

        QSaveFile out(finalPath);
        if (!out.open(QIODevice::WriteOnly)) {
            if (failed)
                failed->append(entry.name
                               + QStringLiteral("（无法写入：%1）").arg(out.errorString()));
            continue;
        }
        if (out.write(data) != data.size()) {
            out.cancelWriting();
            if (failed)
                failed->append(entry.name
                               + QStringLiteral("（写入不完整：%1）").arg(out.errorString()));
            continue;
        }
        if (!out.commit()) {
            if (failed)
                failed->append(entry.name + QStringLiteral("（提交失败）"));
            continue;
        }
        if (written)
            written->append(entry.name);

        doneBytes += qint64(entry.uncompressedSize);
        if (progress)
            progress(doneBytes, totalBytes, entry.name);
    }

    return true;
}

} // namespace mcpacker
