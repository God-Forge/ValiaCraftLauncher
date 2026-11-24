#include "core/MinecraftLayout.h"

#include "core/PclSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace mcpacker {
namespace {

/// 从 libraries 中读取 net.minecraft:client:<版本> 的原版版本号（最可靠来源）
QString mcVersionFromLibraries(const QJsonObject &root)
{
    const QJsonArray libraries = root.value(QStringLiteral("libraries")).toArray();
    for (const QJsonValue &value : libraries) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        const QStringList parts = name.split(QLatin1Char(':'));
        if (parts.size() >= 3 && parts.at(0) == QStringLiteral("net.minecraft")
            && parts.at(1) == QStringLiteral("client"))
            return parts.at(2);
    }
    return {};
}

/// 若 id 形如 "1.20.1" / "1.8.9" / "1.20.1-pre1" 则取出版本号
QString mcVersionFromId(const QString &versionId)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(\\d+\\.\\d+(?:\\.\\d+)?)"));
    const auto match = re.match(versionId);
    if (match.hasMatch())
        return match.captured(1);
    return {};
}

/// 从版本 id 名称中猜测加载器
void applyLoaderFromName(const QString &versionId, QString &loader, QString &loaderVersion)
{
    const QString lower = versionId.toLower();
    const QStringList tokens =
        lower.split(QRegularExpression(QStringLiteral("[^a-z0-9]+")), Qt::SkipEmptyParts);
    auto set = [&](const QString &name) {
        if (loader.isEmpty()) {
            loader = name;
            // 形如 "1.20.1-forge-47.4.16" 时取最后一段作为版本号
            if (loaderVersion.isEmpty()) {
                const QStringList tail = versionId.split(QLatin1Char('-'));
                if (tail.size() >= 2) {
                    const QString last = tail.last();
                    static const QRegularExpression verRe(QStringLiteral("^\\d+(\\.\\d+)*$"));
                    if (verRe.match(last).hasMatch())
                        loaderVersion = last;
                }
            }
        }
    };
    if (tokens.contains(QStringLiteral("neoforge")))
        set(QStringLiteral("NeoForge"));
    else if (tokens.contains(QStringLiteral("forge")))
        set(QStringLiteral("Forge"));
    else if (tokens.contains(QStringLiteral("fabric")))
        set(QStringLiteral("Fabric"));
    else if (tokens.contains(QStringLiteral("quilt")))
        set(QStringLiteral("Quilt"));
    else if (tokens.contains(QStringLiteral("optifine")))
        set(QStringLiteral("OptiFine"));
    else if (tokens.contains(QStringLiteral("liteloader")))
        set(QStringLiteral("LiteLoader"));
}

/// 从 libraries 中识别加载器；返回是否识别成功。
/// 注意：Forge 的库很多（accesstransformers 等），版本号应取 fmlloader / forge 本身。
bool applyLoaderFromLibraries(const QJsonObject &root, QString &loader, QString &loaderVersion)
{
    bool found = false;
    const QJsonArray libraries = root.value(QStringLiteral("libraries")).toArray();
    for (const QJsonValue &value : libraries) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        const QStringList parts = name.split(QLatin1Char(':'));
        if (parts.size() < 3)
            continue;
        const QString group = parts.at(0);
        const QString artifact = parts.at(1);
        const QString version = parts.at(2);

        if (group.startsWith(QStringLiteral("net.neoforged"))) {
            if (!found) {
                loader = QStringLiteral("NeoForge");
                found = true;
            }
            if (loader == QStringLiteral("NeoForge")
                && (artifact == QStringLiteral("neoforge")
                    || artifact == QStringLiteral("fml") || loaderVersion.isEmpty()))
                loaderVersion = version;
            continue;
        }
        if (group == QStringLiteral("net.minecraftforge")) {
            if (!found) {
                loader = QStringLiteral("Forge");
                found = true;
            }
            if (loader == QStringLiteral("Forge")) {
                if (artifact == QStringLiteral("fmlloader")) {
                    // 形如 "1.20.1-47.4.16" → "47.4.16"
                    const QString versionPart = version.section(QLatin1Char('-'), -1);
                    if (versionPart.contains(QLatin1Char('.')))
                        loaderVersion = versionPart;
                    else
                        loaderVersion = version;
                } else if (artifact == QStringLiteral("forge") && loaderVersion.isEmpty()) {
                    loaderVersion = version;
                }
            }
            continue;
        }
        if (group == QStringLiteral("net.fabricmc")) {
            if (!found) {
                loader = QStringLiteral("Fabric");
                found = true;
            }
            if (loader == QStringLiteral("Fabric") && artifact == QStringLiteral("fabric-loader"))
                loaderVersion = version;
            continue;
        }
        if (group == QStringLiteral("org.quiltmc")) {
            if (!found) {
                loader = QStringLiteral("Quilt");
                found = true;
            }
            if (loader == QStringLiteral("Quilt"))
                loaderVersion = version;
            continue;
        }
        if (group == QStringLiteral("net.optifine") || group == QStringLiteral("optifine")) {
            loader = QStringLiteral("OptiFine");
            loaderVersion = version;
            continue;
        }
        if (group == QStringLiteral("com.mumfrey")) {
            if (!found) {
                loader = QStringLiteral("LiteLoader");
                found = true;
            }
            if (loader == QStringLiteral("LiteLoader"))
                loaderVersion = version;
        }
    }
    return found;
}

/// 读取 arguments.game（或旧版 minecraftArguments）里 "--fml.xxx" 这类参数的值
QString fmlArgument(const QJsonObject &root, const QString &key)
{
    const QJsonArray game = root.value(QStringLiteral("arguments"))
                                .toObject()
                                .value(QStringLiteral("game"))
                                .toArray();
    for (int i = 0; i + 1 < game.size(); ++i) {
        if (game.at(i).toString() == key)
            return game.at(i + 1).toString();
    }
    const QString legacy = root.value(QStringLiteral("minecraftArguments")).toString();
    if (!legacy.isEmpty()) {
        const QStringList tokens = legacy.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (int i = 0; i + 1 < tokens.size(); ++i) {
            if (tokens.at(i) == key)
                return tokens.at(i + 1);
        }
    }
    return {};
}

QString pickJsonFile(const QString &versionDir)
{
    const QDir dir(versionDir);
    const QString preferred = dir.absoluteFilePath(dir.dirName() + QStringLiteral(".json"));
    if (QFileInfo::exists(preferred))
        return preferred;
    const QStringList jsons =
        dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files, QDir::Name);
    if (!jsons.isEmpty())
        return dir.absoluteFilePath(jsons.first());
    return {};
}

} // namespace

QStringList MinecraftLayout::detectGameDirs()
{
    QStringList result;
    auto addIfDir = [&result](const QString &path) {
        if (path.isEmpty())
            return;
        const QString native = QDir::cleanPath(QDir::fromNativeSeparators(path));
        if (!native.isEmpty() && QFileInfo(native).isDir() && !result.contains(native))
            result.append(native);
    };

    const QString appData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!appData.isEmpty())
        addIfDir(appData + QStringLiteral("/.minecraft"));

    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (!home.isEmpty())
        addIfDir(home + QStringLiteral("/.minecraft"));

    addIfDir(QDir::currentPath() + QStringLiteral("/.minecraft"));
    addIfDir(QCoreApplication::applicationDirPath() + QStringLiteral("/.minecraft"));

    return result;
}

bool MinecraftLayout::looksLikeGameDir(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QDir dir(path);
    if (!dir.exists())
        return false;
    return QFileInfo::exists(dir.absoluteFilePath(QStringLiteral("versions")))
           || QFileInfo::exists(dir.absoluteFilePath(QStringLiteral("assets")))
           || QFileInfo::exists(dir.absoluteFilePath(QStringLiteral("libraries")));
}

QVector<VersionInfo> MinecraftLayout::scanVersions(const QString &gameDir)
{
    QVector<VersionInfo> versions;
    if (gameDir.isEmpty())
        return versions;

    const QDir versionsDir(QDir(gameDir).absoluteFilePath(QStringLiteral("versions")));
    if (!versionsDir.exists())
        return versions;

    const QFileInfoList dirs = versionsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                         QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &dirInfo : dirs) {
        const QString versionId = dirInfo.fileName();
        if (pickJsonFile(dirInfo.absoluteFilePath()).isEmpty())
            continue;
        VersionInfo info = readVersion(gameDir, versionId);
        if (info.valid)
            versions.append(info);
    }

    std::sort(versions.begin(), versions.end(), [](const VersionInfo &a, const VersionInfo &b) {
        const QString ma = a.mcVersion.isEmpty() ? a.id : a.mcVersion;
        const QString mb = b.mcVersion.isEmpty() ? b.id : b.mcVersion;
        const int cmp = VersionInfo::compareMcVersion(ma, mb);
        if (cmp != 0)
            return cmp > 0;
        return a.id.compare(b.id, Qt::CaseInsensitive) < 0;
    });
    return versions;
}

VersionInfo MinecraftLayout::readVersion(const QString &gameDir, const QString &versionId)
{
    VersionInfo info;
    info.id = versionId;
    info.gameDir = QDir(gameDir).absolutePath();
    info.versionDir =
        QDir(info.gameDir).absoluteFilePath(QStringLiteral("versions/%1").arg(versionId));
    if (!QFileInfo::exists(info.versionDir))
        return info;

    info.jsonPath = pickJsonFile(info.versionDir);
    if (info.jsonPath.isEmpty())
        return info;

    info.hasPclConfig =
        QFileInfo::exists(QDir(info.versionDir).absoluteFilePath(QStringLiteral("PCL/Setup.ini")));
    info.isolated = readPclIsolation(info.versionDir);

    // 沿 inheritsFrom 收集 json 链（当前版本优先）
    QVector<QJsonObject> chain;
    QStringList chainIds;
    QSet<QString> seen;
    QString currentId = versionId;
    QString currentJson = info.jsonPath;
    while (!currentId.isEmpty() && !seen.contains(currentId) && chain.size() < 12) {
        seen.insert(currentId);
        QFile file(currentJson);
        if (!file.open(QIODevice::ReadOnly))
            break;
        const QByteArray data = file.readAll();
        file.close();
        const QJsonObject root = QJsonDocument::fromJson(data).object();
        if (root.isEmpty())
            break;
        chain.append(root);
        chainIds.append(currentId);

        const QString parent = root.value(QStringLiteral("inheritsFrom")).toString();
        if (parent.isEmpty())
            break;
        const QString parentDir =
            QDir(info.gameDir).absoluteFilePath(QStringLiteral("versions/%1").arg(parent));
        const QString parentJson = pickJsonFile(parentDir);
        if (parentJson.isEmpty())
            break;
        currentId = parent;
        currentJson = parentJson;
    }

    // 原版版本号：clientVersion（PCL 合并版）→ --fml.mcVersion → libraries → id → 继承链
    for (const QJsonObject &root : std::as_const(chain)) {
        const QString cv = root.value(QStringLiteral("clientVersion")).toString();
        if (!cv.isEmpty()) {
            info.mcVersion = cv;
            break;
        }
    }
    if (info.mcVersion.isEmpty()) {
        for (const QJsonObject &root : std::as_const(chain)) {
            const QString value = fmlArgument(root, QStringLiteral("--fml.mcVersion"));
            if (!value.isEmpty()) {
                info.mcVersion = value;
                break;
            }
        }
    }
    if (info.mcVersion.isEmpty()) {
        for (const QJsonObject &root : std::as_const(chain)) {
            const QString mc = mcVersionFromLibraries(root);
            if (!mc.isEmpty()) {
                info.mcVersion = mc;
                break;
            }
        }
    }
    if (info.mcVersion.isEmpty())
        info.mcVersion = mcVersionFromId(versionId);
    if (info.mcVersion.isEmpty() && !chainIds.isEmpty())
        info.mcVersion = mcVersionFromId(chainIds.last());

    // 加载器：libraries → --fml.* 参数 → PCL 版本配置 → id 名称
    for (const QJsonObject &root : std::as_const(chain)) {
        QString loader;
        QString loaderVersion;
        if (applyLoaderFromLibraries(root, loader, loaderVersion)) {
            info.loader = loader;
            info.loaderVersion = loaderVersion;
            break;
        }
    }
    if (info.loader.isEmpty()) {
        for (const QJsonObject &root : std::as_const(chain)) {
            const QString forge = fmlArgument(root, QStringLiteral("--fml.forgeVersion"));
            if (!forge.isEmpty()) {
                info.loader = QStringLiteral("Forge");
                info.loaderVersion = forge;
                break;
            }
            const QString neo = fmlArgument(root, QStringLiteral("--fml.neoForgeVersion"));
            if (!neo.isEmpty()) {
                info.loader = QStringLiteral("NeoForge");
                info.loaderVersion = neo;
                break;
            }
            const QString fmlLoader = fmlArgument(root, QStringLiteral("--fml.loaderVersion"));
            if (!fmlLoader.isEmpty()) {
                info.loader = QStringLiteral("Fabric");
                info.loaderVersion = fmlLoader;
                break;
            }
        }
    }

    PclSettings pclSettings;
    QString pclError;
    pclSettings.load(QDir(info.versionDir).absoluteFilePath(QStringLiteral("PCL/Setup.ini")),
                     &pclError);
    auto pclLoaderValue = [&pclSettings](const char *key) {
        const QString value =
            pclSettings.value(QString::fromLatin1(key));
        if (value.isEmpty() || value.compare(QStringLiteral("False"), Qt::CaseInsensitive) == 0
            || value.compare(QStringLiteral("0")) == 0)
            return QString();
        return value;
    };

    if (info.loader.isEmpty()) {
        if (!pclLoaderValue(PclKeys::kForge).isEmpty())
            info.loader = QStringLiteral("Forge");
        else if (!pclLoaderValue(PclKeys::kNeoForge).isEmpty())
            info.loader = QStringLiteral("NeoForge");
        else if (!pclLoaderValue(PclKeys::kFabric).isEmpty())
            info.loader = QStringLiteral("Fabric");
        else if (!pclLoaderValue(PclKeys::kQuilt).isEmpty())
            info.loader = QStringLiteral("Quilt");
    }
    if (info.loader.isEmpty()) {
        QString loader;
        QString loaderVersion;
        applyLoaderFromName(versionId, loader, loaderVersion);
        info.loader = loader;
        info.loaderVersion = loaderVersion;
    }
    if (info.loader.isEmpty())
        info.loader = QStringLiteral("Vanilla");

    if (info.loaderVersion.isEmpty()) {
        const char *key = nullptr;
        if (info.loader == QStringLiteral("Forge"))
            key = PclKeys::kForge;
        else if (info.loader == QStringLiteral("NeoForge"))
            key = PclKeys::kNeoForge;
        else if (info.loader == QStringLiteral("Fabric"))
            key = PclKeys::kFabric;
        else if (info.loader == QStringLiteral("Quilt"))
            key = PclKeys::kQuilt;
        if (key)
            info.loaderVersion = pclLoaderValue(key);
    }

    // Java 需求
    for (const QJsonObject &root : std::as_const(chain)) {
        const QJsonObject javaVersion = root.value(QStringLiteral("javaVersion")).toObject();
        const int major = javaVersion.value(QStringLiteral("majorVersion")).toInt();
        if (major > 0) {
            info.javaMajor = major;
            break;
        }
    }
    if (info.javaMajor <= 0)
        info.javaMajor = inferJavaMajor(info.mcVersion);

    // 类型与发布时间
    for (const QJsonObject &root : std::as_const(chain)) {
        if (info.type.isEmpty())
            info.type = root.value(QStringLiteral("type")).toString();
        if (!info.releaseTime.isValid()) {
            const QString iso = root.value(QStringLiteral("releaseTime")).toString();
            if (!iso.isEmpty())
                info.releaseTime = QDateTime::fromString(iso, Qt::ISODate);
        }
    }
    if (info.type.isEmpty())
        info.type = QStringLiteral("release");

    const QString jarPreferred =
        QDir(info.versionDir).absoluteFilePath(versionId + QStringLiteral(".jar"));
    if (QFileInfo::exists(jarPreferred))
        info.jarPath = jarPreferred;

    info.valid = true;
    return info;
}

int MinecraftLayout::inferJavaMajor(const QString &mcVersion)
{
    if (mcVersion.isEmpty())
        return 8;
    if (VersionInfo::compareMcVersion(mcVersion, QStringLiteral("1.20.5")) >= 0)
        return 21;
    if (VersionInfo::compareMcVersion(mcVersion, QStringLiteral("1.17")) >= 0)
        return 17;
    return 8;
}

} // namespace mcpacker
