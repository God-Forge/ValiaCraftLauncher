#pragma once

#include "core/Common.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mcpacker {

/// PCL2（Plain Craft Launcher 2）配置键名常量。
///
/// PCL2 的配置文件是 `Key:Value` 形式的 ini（注意是英文冒号，不是等号），
/// 采用 UTF-8 编码、CRLF 换行。全局配置位于 `<.minecraft>/PCL/Setup.ini`，
/// 版本级配置位于 `<.minecraft>/versions/<版本>/PCL/Setup.ini`。
namespace PclKeys {
// 全局配置
inline constexpr auto kLaunchFolderSelect = "LaunchFolderSelect"; // 启动文件夹（.minecraft 根，值末尾带反斜杠）

// 版本级配置
inline constexpr auto kIndieV2 = "VersionArgumentIndieV2";     // True/False 版本隔离（新格式）
inline constexpr auto kIndie = "VersionArgumentIndie";         // 0/1 版本隔离（旧格式兼容）
inline constexpr auto kVanilla = "VersionVanilla";             // 原版版本号
inline constexpr auto kVanillaName = "VersionVanillaName";     // 原版版本显示名
inline constexpr auto kForge = "VersionForge";                 // Forge 版本
inline constexpr auto kNeoForge = "VersionNeoForge";           // NeoForge 版本
inline constexpr auto kFabric = "VersionFabric";               // Fabric 版本
inline constexpr auto kQuilt = "VersionQuilt";                 // Quilt 版本
inline constexpr auto kLiteLoader = "VersionLiteLoader";       // LiteLoader
inline constexpr auto kInfo = "Info";                          // 版本描述
inline constexpr auto kJava = "VersionArgumentJava";           // 自定义 Java 路径（尽力兼容）
inline constexpr auto kReleaseTime = "ReleaseTime";            // 发布时间
} // namespace PclKeys

/// 简单的 PCL ini 配置对象：保留键顺序，重复键后者覆盖前者。
class PclSettings
{
public:
    /// 读取文件；文件不存在时返回空配置（不是错误）。
    bool load(const QString &path, QString *error = nullptr);
    /// 原子写入（CRLF + UTF-8）。
    bool save(const QString &path, QString *error = nullptr) const;

    bool contains(const QString &key) const;
    QString value(const QString &key, const QString &defaultValue = QString()) const;
    void setValue(const QString &key, const QString &value);
    void remove(const QString &key);
    QStringList keys() const;
    int size() const { return int(m_order.size()); }
    bool isEmpty() const { return m_order.isEmpty(); }
    void clear();

    /// 生成 PCL 的版本描述串，例如 "正式版 1.20.1, Forge 47.4.16"
    static QString buildVersionInfo(const VersionInfo &version);

private:
    QVector<QString> m_order;
    QMap<QString, QString> m_values;
};

/// 写入 PCL 配置的选项
struct PclWriteOptions
{
    QString gameDir;      // .minecraft 根目录（绝对路径）
    QString versionId;    // 版本 id
    VersionInfo version;  // 版本信息（用于生成 Info / Vanilla / Forge 等）
    QString javaExe;      // 自定义 Java 路径；为空则不写入（交给 PCL 自动选择）
    bool isolation = true;       // 是否写入版本隔离
    bool writeGlobal = true;     // 是否写全局 PCL/Setup.ini
    bool writeInstance = true;   // 是否写版本级 PCL/Setup.ini
};

/// 写入 PCL 配置的结果
struct PclWriteResult
{
    QStringList writtenFiles;
    QStringList messages;
    bool ok = true;
};

/// 按选项写入 PCL 配置（合并已有配置，不覆盖无关键）。
PclWriteResult writePclConfigs(const PclWriteOptions &options, QString *error = nullptr);

/// 读取版本级 PCL 配置中的版本隔离状态（不存在时返回 false）。
bool readPclIsolation(const QString &versionDir);

} // namespace mcpacker
