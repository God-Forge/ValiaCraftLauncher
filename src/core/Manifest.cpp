#include "core/Manifest.h"

#include <QJsonDocument>

namespace mcpacker {

QJsonObject PackManifest::toJson() const
{
    QJsonObject versionObj;
    versionObj.insert(QStringLiteral("id"), version.id);
    versionObj.insert(QStringLiteral("mcVersion"), version.mcVersion);
    versionObj.insert(QStringLiteral("loader"), version.loader);
    versionObj.insert(QStringLiteral("loaderVersion"), version.loaderVersion);
    versionObj.insert(QStringLiteral("type"), version.type);
    if (version.releaseTime.isValid())
        versionObj.insert(QStringLiteral("releaseTime"),
                          version.releaseTime.toString(Qt::ISODate));
    versionObj.insert(QStringLiteral("javaMajor"), version.javaMajor);
    versionObj.insert(QStringLiteral("isolated"), version.isolated);

    QJsonObject javaObj;
    javaObj.insert(QStringLiteral("included"), java.included);
    javaObj.insert(QStringLiteral("relativePath"), java.relativePath);
    javaObj.insert(QStringLiteral("exeRelative"), java.exeRelative);
    javaObj.insert(QStringLiteral("major"), java.major);
    javaObj.insert(QStringLiteral("fullVersion"), java.fullVersion);
    javaObj.insert(QStringLiteral("vendor"), java.vendor);
    javaObj.insert(QStringLiteral("arch"), java.arch);
    javaObj.insert(QStringLiteral("originalPath"), java.originalPath);

    QJsonObject optionsObj;
    optionsObj.insert(QStringLiteral("isolation"), options.isolation);
    optionsObj.insert(QStringLiteral("includeVersion"), options.includeVersion);
    optionsObj.insert(QStringLiteral("includeLibraries"), options.includeLibraries);
    optionsObj.insert(QStringLiteral("includeAssets"), options.includeAssets);
    optionsObj.insert(QStringLiteral("includeInstanceFiles"), options.includeInstanceFiles);
    optionsObj.insert(QStringLiteral("includePclConfig"), options.includePclConfig);

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("mcpacker"));
    root.insert(QStringLiteral("formatVersion"), formatVersion);
    root.insert(QStringLiteral("tool"), tool);
    root.insert(QStringLiteral("createdAt"), createdAt);
    root.insert(QStringLiteral("version"), versionObj);
    root.insert(QStringLiteral("java"), javaObj);
    root.insert(QStringLiteral("options"), optionsObj);
    root.insert(QStringLiteral("sourceGameDir"), sourceGameDir);
    root.insert(QStringLiteral("fileCount"), fileCount);
    root.insert(QStringLiteral("totalBytes"), totalBytes);
    return root;
}

PackManifest PackManifest::fromJson(const QJsonObject &object, bool *ok)
{
    PackManifest manifest;
    bool valid = true;
    if (object.value(QStringLiteral("format")).toString() != QStringLiteral("mcpacker"))
        valid = false;

    manifest.formatVersion = object.value(QStringLiteral("formatVersion")).toInt(kFormatVersion);
    manifest.tool = object.value(QStringLiteral("tool")).toString(QStringLiteral("ValiaCraftLauncher"));
    manifest.createdAt = object.value(QStringLiteral("createdAt")).toString();

    const QJsonObject versionObj = object.value(QStringLiteral("version")).toObject();
    manifest.version.id = versionObj.value(QStringLiteral("id")).toString();
    manifest.version.mcVersion = versionObj.value(QStringLiteral("mcVersion")).toString();
    manifest.version.loader = versionObj.value(QStringLiteral("loader")).toString();
    manifest.version.loaderVersion = versionObj.value(QStringLiteral("loaderVersion")).toString();
    manifest.version.type = versionObj.value(QStringLiteral("type")).toString();
    manifest.version.releaseTime =
        QDateTime::fromString(versionObj.value(QStringLiteral("releaseTime")).toString(), Qt::ISODate);
    manifest.version.javaMajor = versionObj.value(QStringLiteral("javaMajor")).toInt();
    manifest.version.isolated = versionObj.value(QStringLiteral("isolated")).toBool();
    if (manifest.version.id.isEmpty())
        valid = false;

    const QJsonObject javaObj = object.value(QStringLiteral("java")).toObject();
    manifest.java.included = javaObj.value(QStringLiteral("included")).toBool();
    manifest.java.relativePath = javaObj.value(QStringLiteral("relativePath")).toString();
    manifest.java.exeRelative = javaObj.value(QStringLiteral("exeRelative")).toString();
    manifest.java.major = javaObj.value(QStringLiteral("major")).toInt();
    manifest.java.fullVersion = javaObj.value(QStringLiteral("fullVersion")).toString();
    manifest.java.vendor = javaObj.value(QStringLiteral("vendor")).toString();
    manifest.java.arch = javaObj.value(QStringLiteral("arch")).toString();
    manifest.java.originalPath = javaObj.value(QStringLiteral("originalPath")).toString();
    manifest.java.valid = manifest.java.included;

    const QJsonObject optionsObj = object.value(QStringLiteral("options")).toObject();
    manifest.options.isolation = optionsObj.value(QStringLiteral("isolation")).toBool(true);
    manifest.options.includeVersion = optionsObj.value(QStringLiteral("includeVersion")).toBool(true);
    manifest.options.includeLibraries =
        optionsObj.value(QStringLiteral("includeLibraries")).toBool(false);
    manifest.options.includeAssets = optionsObj.value(QStringLiteral("includeAssets")).toBool(false);
    manifest.options.includeInstanceFiles =
        optionsObj.value(QStringLiteral("includeInstanceFiles")).toBool(true);
    manifest.options.includePclConfig =
        optionsObj.value(QStringLiteral("includePclConfig")).toBool(true);

    manifest.sourceGameDir = object.value(QStringLiteral("sourceGameDir")).toString();
    manifest.fileCount = qint64(object.value(QStringLiteral("fileCount")).toDouble());
    manifest.totalBytes = qint64(object.value(QStringLiteral("totalBytes")).toDouble());

    if (ok)
        *ok = valid;
    return manifest;
}

} // namespace mcpacker
