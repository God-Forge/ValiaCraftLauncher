#pragma once

#include "core/Common.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace mcpacker {

/// 负责识别 Minecraft 目录结构、枚举版本、解析版本 json（含 inheritsFrom 继承链）。
class MinecraftLayout
{
public:
    /// 常见 .minecraft 位置（%APPDATA%\.minecraft、当前目录等）
    static QStringList detectGameDirs();

    /// 判断目录是否像一个 .minecraft 根目录
    static bool looksLikeGameDir(const QString &path);

    /// 扫描 <gameDir>/versions 下的全部版本
    static QVector<VersionInfo> scanVersions(const QString &gameDir);

    /// 读取单个版本（解析 version json，含 inheritsFrom 继承链）
    static VersionInfo readVersion(const QString &gameDir, const QString &versionId);

    /// 按原版版本号推断所需的 Java 主版本
    static int inferJavaMajor(const QString &mcVersion);
};

} // namespace mcpacker
