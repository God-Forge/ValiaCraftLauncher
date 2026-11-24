#pragma once

#include "core/Common.h"

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace mcpacker {

struct ZipEntry
{
    QString name;             // UTF-8 文件名，使用 '/' 分隔
    quint16 method = 0;       // 0=store, 8=deflate
    quint16 flags = 0;
    quint32 crc = 0;
    quint64 compressedSize = 0;
    quint64 uncompressedSize = 0;
    quint64 localOffset = 0;

    bool isDirectory() const { return name.endsWith(QLatin1Char('/')); }
};

/// 只读 ZIP 解析/解压器，支持 Zip64 与 UTF-8 文件名。
class ZipReader
{
public:
    using ExtractProgress = std::function<void(qint64 done, qint64 total, const QString &entry)>;

    ZipReader() = default;
    ~ZipReader();

    bool open(const QString &filePath, QString *error = nullptr);
    void close();

    bool isOpen() const { return m_open; }
    QString filePath() const { return m_filePath; }
    const QVector<ZipEntry> &entries() const { return m_entries; }
    qint64 totalUncompressedBytes() const { return m_totalBytes; }
    qint64 archiveSize() const { return m_archiveSize; }

    const ZipEntry *find(const QString &entryName) const;

    /// 读取单个条目内容（deflate 条目自动解压并校验 CRC）
    QByteArray read(const ZipEntry &entry, QString *error = nullptr) const;

    /// 解压全部条目到 destDir。返回 false 表示有致命错误。
    /// 单个文件失败会记录到 failed 后继续。
    bool extract(const QString &destDir, ConflictPolicy policy, const ExtractProgress &progress,
                 QStringList *written, QStringList *skipped, QStringList *failed,
                 QString *error = nullptr);

    /// 路径安全检查：entry 名不能穿越目标目录。返回规范化后的绝对路径，非法时为空。
    static QString safeTargetPath(const QString &destDir, const QString &entryName);

private:
    bool loadCentralDirectory(QString *error);

    QString m_filePath;
    mutable QFile m_file;
    QString m_error;
    bool m_open = false;
    qint64 m_totalBytes = 0;
    qint64 m_archiveSize = 0;
    QVector<ZipEntry> m_entries;
};

} // namespace mcpacker
