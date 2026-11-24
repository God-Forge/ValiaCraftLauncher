#pragma once

// 全局共享的数据结构与工具声明。
// 命名空间统一使用 mcpacker。

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mcpacker {

/// 一个已安装的 Minecraft 版本（versions/<id>）
struct VersionInfo
{
    QString id;                 // 版本目录名，也是版本 id
    QString gameDir;            // .minecraft 根目录
    QString versionDir;         // <gameDir>/versions/<id>
    QString jsonPath;           // 版本 json
    QString jarPath;            // 客户端 jar（可能不存在，例如继承版本）
    QString mcVersion;          // 原版版本号，如 "1.20.1"
    QString loader;             // Vanilla / Forge / Fabric / NeoForge / Quilt / OptiFine / LiteLoader
    QString loaderVersion;      // 加载器版本，如 "47.4.16"
    int javaMajor = 0;          // json 声明或按原版版本推断的 Java 主版本
    QString type;               // release / snapshot / old_beta / old_alpha
    QDateTime releaseTime;
    bool isolated = false;      // 是否在 PCL 中开启了版本隔离
    bool hasPclConfig = false;  // 是否存在 versions/<id>/PCL/Setup.ini
    bool valid = false;

    /// 用于界面显示的简述
    QString describe() const;

    /// 原版版本数组（"1.20.1" -> {1,20,1}），用于排序
    static QVector<int> parseMcVersion(const QString &v);
    /// 比较两个原版版本号，返回 <0 / 0 / >0
    static int compareMcVersion(const QString &a, const QString &b);
};

/// 一个可用的 Java 运行时
struct JavaInfo
{
    QString javaExe;        // .../bin/java.exe
    QString javaHome;       // .../（bin 的父目录）
    int major = 0;          // 17 / 8 / 21
    QString fullVersion;    // "17.0.21"
    QString vendor;         // "Azul Systems, Inc."
    QString arch;           // "amd64" / "x86"
    bool valid = false;

    /// 用于界面显示的简述，例如 "Java 17.0.21 · Zulu · amd64"
    QString describe() const;
};

/// 解包冲突策略
enum class ConflictPolicy {
    Overwrite,  // 覆盖已有文件
    Skip,       // 跳过已有文件
    Rename,     // 重命名为 "xxx (2).ext"
};

QString conflictPolicyName(ConflictPolicy policy);

/// 统一的进度/日志回调
using ProgressFn = std::function<void(int percent, const QString &message)>;
using LogFn = std::function<void(const QString &message)>;

/// 生成人类可读的字节数，如 "1.23 GB"
QString formatBytes(qint64 bytes);

/// 简易耗时格式化，如 "3 分 12 秒"
QString formatDuration(qint64 ms);

} // namespace mcpacker
