#pragma once

#include "core/Common.h"

#include <QJsonObject>
#include <QString>

namespace mcpacker {

/// ValiaCraftLauncher 自定义包格式（.mcpk，本质是 ZIP）的清单。
/// 包根目录的 manifest.json 描述版本、Java、打包选项与来源。
struct PackManifest
{
    static constexpr int kFormatVersion = 1;
    static constexpr const char *kFileName = "manifest.json";

    int formatVersion = kFormatVersion;
    QString tool = QStringLiteral("ValiaCraftLauncher");
    QString createdAt;
    VersionInfo version;

    struct JavaPayload
    {
        bool included = false;
        QString relativePath;    // 包内 Java 根目录，如 "runtime/mcpacker-java-17"
        QString exeRelative;     // 包内 java.exe 相对路径
        int major = 0;
        QString fullVersion;
        QString vendor;
        QString arch;
        QString originalPath;    // 打包机上的原始路径（仅参考）
        bool valid = false;
    } java;

    struct Options
    {
        bool isolation = true;
        bool includeVersion = true;
        bool includeLibraries = false;
        bool includeAssets = false;
        bool includeInstanceFiles = true;
        bool includePclConfig = true;
    } options;

    QString sourceGameDir;
    qint64 fileCount = 0;
    qint64 totalBytes = 0;

    QJsonObject toJson() const;
    static PackManifest fromJson(const QJsonObject &object, bool *ok = nullptr);

    /// 是否有内嵌 Java 运行时
    bool hasJava() const { return java.included && !java.relativePath.isEmpty(); }
};

} // namespace mcpacker
