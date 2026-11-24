#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QString>
#include <QVector>

#include <functional>

namespace mcpacker {

/// 极简 ZIP 写入器（支持 store / deflate、Zip64、UTF-8 文件名）。
/// 设计目标：对 Minecraft 实例（大量小文件、少量大 jar）可靠且可报告进度。
class ZipWriter
{
public:
    explicit ZipWriter(const QString &filePath);
    ~ZipWriter();

    bool open(QString *error = nullptr);
    bool addFile(const QString &entryName, const QString &sourcePath, QString *error = nullptr);
    bool addData(const QString &entryName, const QByteArray &data, QString *error = nullptr);
    bool addEmptyDirectory(const QString &entryName, QString *error = nullptr);
    bool close(QString *error = nullptr);

    bool isOpen() const { return m_open; }
    QString errorString() const { return m_error; }
    qint64 bytesWritten() const { return m_written; }
    int entryCount() const { return int(m_entries.size()); }

    /// 每条数据处理完成后的回调：entry 名称 + 原始字节数
    std::function<void(const QString &, qint64)> onEntryWritten;

    /// 超过该大小的文件不再压缩（避免单文件占用过多内存）
    static constexpr qint64 kMaxCompressSize = 64LL * 1024 * 1024;

private:
    struct Entry
    {
        QString name;
        quint16 method = 0;
        quint16 flags = 0;
        quint16 modTime = 0;
        quint16 modDate = 0;
        quint32 crc = 0;
        quint32 diskStart = 0;
        quint64 compressedSize = 0;
        quint64 uncompressedSize = 0;
        quint64 localOffset = 0;
        bool isDirectory = false;
    };

    bool writeRaw(const QByteArray &bytes);
    bool writeEntry(const QString &entryName, const QByteArray &raw, const QDateTime &modTime,
                    QString *error);
    void setError(const QString &message);

    QString m_filePath;
    QFile m_file;
    QString m_error;
    bool m_open = false;
    qint64 m_written = 0;
    QVector<Entry> m_entries;
};

} // namespace mcpacker
