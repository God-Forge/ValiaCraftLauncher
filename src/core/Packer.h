#pragma once

#include "core/Common.h"
#include "core/Manifest.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace mcpacker {

/// 打包请求
struct PackRequest
{
    QString gameDir;                 // .minecraft 根目录
    VersionInfo version;             // 要打包的版本
    bool includeJava = true;         // 是否把 Java 运行时一起打包
    JavaInfo java;                   // includeJava 时使用的 Java
    QString javaRuntimeName;         // 包内 runtime 子目录名（空 = 自动生成）
    bool includeLibraries = false;
    bool includeAssets = false;
    bool includeInstanceFiles = true; // 非隔离版本：额外打包根目录下的实例文件
    bool includePclConfig = true;
    QString outputPath;              // 输出 .mcpk（ZIP）路径
};

/// 打包结果
struct PackResult
{
    bool ok = false;
    QString outputPath;
    qint64 totalBytes = 0;
    int fileCount = 0;
    QStringList warnings;
    QString error;
};

/// 把某个版本（以及可选的 Java 运行时、libraries、assets）打包成 .mcpk
class Packer
{
public:
    Packer() = default;
    Packer(ProgressFn progress, LogFn log) : m_progress(std::move(progress)), m_log(std::move(log)) {}

    PackResult run(const PackRequest &request);

private:
    struct SourceFile
    {
        QString entryName;
        QString absolutePath;
        qint64 size = 0;
    };

    void collectVersionFiles(const QString &versionDir, const QString &versionId,
                             const QString &entryPrefix, bool includePclConfig,
                             QVector<SourceFile> &out);
    void collectTree(const QString &rootDir, const QString &entryPrefix, QVector<SourceFile> &out);
    void collectInstanceFiles(const QString &gameDir, const QVector<SourceFile> &versionFiles,
                              QVector<SourceFile> &out);
    void report(int percent, const QString &message) const;
    void log(const QString &message) const;

    static QByteArray buildGlobalPclTemplate(const QString &gameDir, QStringList *warnings);
    static QByteArray buildInstancePclTemplate(const VersionInfo &version, bool includeJava,
                                               QStringList *warnings);

    ProgressFn m_progress;
    LogFn m_log;
    int m_excludedCount = 0;
};

} // namespace mcpacker
