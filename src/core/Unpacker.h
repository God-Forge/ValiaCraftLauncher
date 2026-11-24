#pragma once

#include "core/Common.h"
#include "core/Manifest.h"

#include <QString>
#include <QStringList>

namespace mcpacker {

/// 解包请求
struct UnpackRequest
{
    QString packagePath;      // .mcpk（ZIP）路径
    QString targetGameDir;    // 目标 .minecraft 根目录
    ConflictPolicy policy = ConflictPolicy::Skip;
    bool writePclConfig = true;
    QString versionNameOverride; // 非空时把版本重命名为该名称
};

/// 解包结果
struct UnpackResult
{
    bool ok = false;
    QString gameDir;
    QString versionId;
    QString javaExe;
    PackManifest manifest;
    QStringList writtenFiles;
    QStringList skippedFiles;
    QStringList failedFiles;
    QStringList messages;
    QString error;
};

/// 解包 .mcpk 到指定的 .minecraft 目录，并写入 PCL 配置
class Unpacker
{
public:
    Unpacker() = default;
    Unpacker(ProgressFn progress, LogFn log)
        : m_progress(std::move(progress)), m_log(std::move(log))
    {
    }

    UnpackResult run(const UnpackRequest &request);

    /// 只读取包内的清单（用于界面预览）
    static bool inspect(const QString &packagePath, PackManifest *manifest, QString *error = nullptr);

private:
    void report(int percent, const QString &message) const;
    void log(const QString &message) const;

    static QString remapVersionEntry(const QString &entryName, const QString &oldId,
                                     const QString &newId);
    static QString resolveTargetGameDir(const QString &target);
    static bool applyPlaceholders(const QString &filePath, const QString &gameDir,
                                  const QString &javaHome);
    static void fixVersionJsonId(const QString &versionDir, const QString &newId);

    ProgressFn m_progress;
    LogFn m_log;
};

} // namespace mcpacker
