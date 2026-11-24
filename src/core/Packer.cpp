#include "core/Packer.h"

#include "core/MinecraftLayout.h"
#include "core/PclSettings.h"
#include "core/ZipWriter.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>

namespace mcpacker {
namespace {

/// 顶层实例文件打包时的排除项（这些目录/文件不属于实例内容）
const QSet<QString> &instanceExclusions()
{
    static const QSet<QString> kSet = {
        QStringLiteral("versions"),     QStringLiteral("libraries"),
        QStringLiteral("assets"),       QStringLiteral("runtime"),
        QStringLiteral("pcl"),          QStringLiteral("logs"),
        QStringLiteral("crash-reports"), QStringLiteral("usercache.json"),
        QStringLiteral("launcher_profiles.json"),
        QStringLiteral("launcher_accounts.json"),
        QStringLiteral("launcher_accounts_microsoft.json"),
        QStringLiteral("launcher_profiles_microsoft_store.json"),
        QStringLiteral("launcher_msa_credentials.bin"),
        QStringLiteral("launcher_log.txt"), QStringLiteral("manifest.json"),
        QStringLiteral(".mcpacker"),
    };
    return kSet;
}

QString sanitizeRuntimeName(const QString &name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar ch : name) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('-') || ch == QLatin1Char('_')
            || ch == QLatin1Char('.'))
            out.append(ch);
        else
            out.append(QLatin1Char('-'));
    }
    while (out.startsWith(QLatin1Char('.')) || out.startsWith(QLatin1Char('-')))
        out.remove(0, 1);
    if (out.isEmpty())
        out = QStringLiteral("java");
    return out;
}

QByteArray serializeSettings(const PclSettings &settings)
{
    QByteArray bytes;
    const QStringList keys = settings.keys();
    for (const QString &key : keys)
        bytes += (key + QLatin1Char(':') + settings.value(key) + QStringLiteral("\r\n")).toUtf8();
    return bytes;
}

} // namespace

void Packer::report(int percent, const QString &message) const
{
    if (m_progress)
        m_progress(qBound(0, percent, 100), message);
}

void Packer::log(const QString &message) const
{
    if (m_log)
        m_log(message);
}

void Packer::collectTree(const QString &rootDir, const QString &entryPrefix,
                         QVector<SourceFile> &out)
{
    const QDir root(rootDir);
    if (!root.exists())
        return;

    QDirIterator it(rootDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        if (!info.isFile())
            continue;
        const QString relative = root.relativeFilePath(info.absoluteFilePath());
        if (relative.isEmpty())
            continue;
        SourceFile file;
        file.entryName = entryPrefix + QLatin1Char('/') + relative;
        file.absolutePath = info.absoluteFilePath();
        file.size = info.size();
        out.append(file);
    }
}

void Packer::collectVersionFiles(const QString &versionDir, const QString &versionId,
                                 const QString &entryPrefix, bool includePclConfig,
                                 QVector<SourceFile> &out)
{
    Q_UNUSED(versionId);
    const QDir root(versionDir);
    if (!root.exists())
        return;

    QDirIterator it(versionDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        if (!info.isFile())
            continue;
        const QString relative = root.relativeFilePath(info.absoluteFilePath());
        const QString lowered = relative.toLower();

        // 日志、崩溃报告、备份、启动器缓存不进入整合包
        if (lowered.startsWith(QStringLiteral("logs/"))
            || lowered.startsWith(QStringLiteral("crash-reports/"))
            || lowered.startsWith(QStringLiteral("backups/"))
            || lowered == QStringLiteral("usercache.json")
            || lowered == QStringLiteral("usernamecache.json")
            || lowered == QStringLiteral("manifest.json")
            || lowered.endsWith(QStringLiteral(".log"))) {
            ++m_excludedCount;
            continue;
        }

        if (relative.compare(QStringLiteral("PCL/Setup.ini"), Qt::CaseInsensitive) == 0) {
            // 版本级 PCL 配置单独生成模板
            continue;
        }
        if (!includePclConfig && relative.startsWith(QStringLiteral("PCL/"), Qt::CaseInsensitive))
            continue;

        SourceFile file;
        file.entryName = entryPrefix + QLatin1Char('/') + relative;
        file.absolutePath = info.absoluteFilePath();
        file.size = info.size();
        out.append(file);
    }
}

void Packer::collectInstanceFiles(const QString &gameDir, const QVector<SourceFile> &versionFiles,
                                  QVector<SourceFile> &out)
{
    Q_UNUSED(versionFiles);
    const QDir root(gameDir);
    const QFileInfoList entries =
        root.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : entries) {
        const QString name = info.fileName();
        if (name.startsWith(QLatin1Char('.')))
            continue;
        if (instanceExclusions().contains(name.toLower()))
            continue;
        if (info.isDir()) {
            collectTree(info.absoluteFilePath(), name, out);
        } else if (info.isFile()) {
            SourceFile file;
            file.entryName = name;
            file.absolutePath = info.absoluteFilePath();
            file.size = info.size();
            out.append(file);
        }
    }
}

QByteArray Packer::buildGlobalPclTemplate(const QString &gameDir, QStringList *warnings)
{
    Q_UNUSED(warnings);
    const QString path = QDir(gameDir).absoluteFilePath(QStringLiteral("PCL/Setup.ini"));
    PclSettings settings;
    QString error;
    if (!settings.load(path, &error) && !error.isEmpty()) {
        if (warnings)
            warnings->append(error);
    }
    // 解包时替换为真实路径；用 PCL 直接导入时该值会被 PCL 覆盖
    settings.setValue(QString::fromLatin1(PclKeys::kLaunchFolderSelect),
                      QStringLiteral("{{GAME_DIR}}\\"));
    return serializeSettings(settings);
}

QByteArray Packer::buildInstancePclTemplate(const VersionInfo &version, bool includeJava,
                                            QStringList *warnings)
{
    Q_UNUSED(warnings);
    const QString path =
        QDir(version.versionDir).absoluteFilePath(QStringLiteral("PCL/Setup.ini"));
    PclSettings settings;
    QString error;
    settings.load(path, &error);

    settings.setValue(QString::fromLatin1(PclKeys::kIndieV2),
                      version.isolated ? QStringLiteral("True") : QStringLiteral("False"));
    settings.setValue(QString::fromLatin1(PclKeys::kIndie),
                      version.isolated ? QStringLiteral("1") : QStringLiteral("0"));
    // 只写显示名；VersionVanilla 在 PCL 中是内部格式，保留原值不覆盖
    if (!version.mcVersion.isEmpty())
        settings.setValue(QString::fromLatin1(PclKeys::kVanillaName), version.mcVersion);
    if (!version.loaderVersion.isEmpty()) {
        if (version.loader == QStringLiteral("Forge"))
            settings.setValue(QString::fromLatin1(PclKeys::kForge), version.loaderVersion);
        else if (version.loader == QStringLiteral("NeoForge"))
            settings.setValue(QString::fromLatin1(PclKeys::kNeoForge), version.loaderVersion);
        else if (version.loader == QStringLiteral("Fabric"))
            settings.setValue(QString::fromLatin1(PclKeys::kFabric), version.loaderVersion);
        else if (version.loader == QStringLiteral("Quilt"))
            settings.setValue(QString::fromLatin1(PclKeys::kQuilt), version.loaderVersion);
    }
    if (version.loader != QStringLiteral("LiteLoader"))
        settings.setValue(QString::fromLatin1(PclKeys::kLiteLoader), QStringLiteral("False"));

    const QString info = PclSettings::buildVersionInfo(version);
    if (!info.isEmpty())
        settings.setValue(QString::fromLatin1(PclKeys::kInfo), info);
    if (version.releaseTime.isValid())
        settings.setValue(QString::fromLatin1(PclKeys::kReleaseTime),
                          version.releaseTime.toString(QStringLiteral("yyyy-MM-dd HH:mm")));

    if (includeJava)
        settings.setValue(QString::fromLatin1(PclKeys::kJava),
                          QStringLiteral("{{JAVA_HOME}}/bin/java.exe"));
    else
        settings.remove(QString::fromLatin1(PclKeys::kJava));

    return serializeSettings(settings);
}

PackResult Packer::run(const PackRequest &request)
{
    PackResult result;
    auto fail = [&](const QString &message) {
        result.ok = false;
        result.error = message;
        log(QStringLiteral("[错误] ") + message);
        return result;
    };

    if (request.outputPath.isEmpty())
        return fail(QStringLiteral("请先选择输出文件路径"));
    if (request.gameDir.isEmpty() || !QFileInfo::exists(request.gameDir))
        return fail(QStringLiteral("游戏目录不存在：%1").arg(request.gameDir));
    if (request.version.id.isEmpty() || !QFileInfo::exists(request.version.versionDir))
        return fail(QStringLiteral("版本目录不存在：%1").arg(request.version.versionDir));

    VersionInfo version = MinecraftLayout::readVersion(request.gameDir, request.version.id);
    if (!version.valid)
        return fail(QStringLiteral("无法解析版本信息：%1").arg(request.version.id));

    if (request.includeJava) {
        if (request.java.javaExe.isEmpty() || !QFileInfo::exists(request.java.javaExe))
            return fail(QStringLiteral("请选择要一同打包的 Java 运行时"));
        if (!QFileInfo::exists(request.java.javaHome))
            return fail(QStringLiteral("Java 运行时目录不存在：%1").arg(request.java.javaHome));
    }

    const QString outputPath = QDir::cleanPath(QDir::fromNativeSeparators(request.outputPath));
    if (!QDir().mkpath(QFileInfo(outputPath).absolutePath()))
        return fail(QStringLiteral("无法创建输出目录：%1")
                        .arg(QFileInfo(outputPath).absolutePath()));
    QFile::remove(outputPath);

    log(QStringLiteral("准备打包版本「%1」：%2").arg(version.id, version.describe()));
    m_excludedCount = 0;

    // ------------------------------------------------------------------
    // 1. 收集文件清单
    // ------------------------------------------------------------------
    report(0, QStringLiteral("正在扫描文件…"));
    QVector<SourceFile> files;

    collectVersionFiles(version.versionDir, version.id,
                        QStringLiteral("versions/%1").arg(version.id),
                        request.includePclConfig, files);
    log(QStringLiteral("版本目录文件：%1 个").arg(files.size()));
    if (m_excludedCount > 0)
        log(QStringLiteral("已跳过日志/崩溃报告/备份/缓存文件：%1 个").arg(m_excludedCount));

    QString javaRuntimeName;
    QString javaExeRelative;
    if (request.includeJava) {
        javaRuntimeName = sanitizeRuntimeName(
            request.javaRuntimeName.isEmpty()
                ? QStringLiteral("mcpacker-java-%1").arg(request.java.major)
                : request.javaRuntimeName);
        const QDir javaHomeDir(request.java.javaHome);
        // java.exe 相对 Java 根目录的路径，通常是 bin/java.exe
        javaExeRelative = javaHomeDir.relativeFilePath(request.java.javaExe);
        if (javaExeRelative.isEmpty() || javaExeRelative.startsWith(QStringLiteral(".."))
            || QDir::isAbsolutePath(javaExeRelative))
            javaExeRelative = QStringLiteral("bin/java.exe");

        const int before = int(files.size());
        collectTree(request.java.javaHome, QStringLiteral("runtime/%1").arg(javaRuntimeName), files);
        log(QStringLiteral("Java 运行时文件：%1 个（%2）")
                .arg(files.size() - before)
                .arg(javaRuntimeName));
    }

    if (request.includeLibraries) {
        const int before = int(files.size());
        collectTree(QDir(request.gameDir).absoluteFilePath(QStringLiteral("libraries")),
                    QStringLiteral("libraries"), files);
        log(QStringLiteral("libraries 文件：%1 个").arg(files.size() - before));
    }
    if (request.includeAssets) {
        const int before = int(files.size());
        collectTree(QDir(request.gameDir).absoluteFilePath(QStringLiteral("assets")),
                    QStringLiteral("assets"), files);
        log(QStringLiteral("assets 文件：%1 个").arg(files.size() - before));
    }
    if (request.includeInstanceFiles && !version.isolated) {
        const int before = int(files.size());
        QVector<SourceFile> instancePart;
        collectInstanceFiles(request.gameDir, files, instancePart);
        files.append(instancePart);
        log(QStringLiteral("实例根目录文件：%1 个").arg(files.size() - before));
    }

    qint64 totalBytes = 0;
    for (const SourceFile &file : std::as_const(files))
        totalBytes += file.size;

    if (files.isEmpty())
        return fail(QStringLiteral("没有找到任何可打包的文件"));

    // ------------------------------------------------------------------
    // 2. 组装 manifest 与 PCL 配置
    // ------------------------------------------------------------------
    PackManifest manifest;
    manifest.tool = QStringLiteral("mcPacker " MCPACKER_VERSION);
    manifest.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    manifest.version = version;
    manifest.version.isolated = version.isolated;
    manifest.options.isolation = version.isolated;
    manifest.options.includeVersion = true;
    manifest.options.includeLibraries = request.includeLibraries;
    manifest.options.includeAssets = request.includeAssets;
    manifest.options.includeInstanceFiles =
        request.includeInstanceFiles && !version.isolated;
    manifest.options.includePclConfig = request.includePclConfig;
    manifest.sourceGameDir = QDir(request.gameDir).absolutePath();
    manifest.fileCount = files.size();
    manifest.totalBytes = totalBytes;
    manifest.java.included = request.includeJava;
    if (request.includeJava) {
        manifest.java.relativePath = QStringLiteral("runtime/%1").arg(javaRuntimeName);
        manifest.java.exeRelative =
            QStringLiteral("runtime/%1/%2").arg(javaRuntimeName, javaExeRelative);
        manifest.java.major = request.java.major;
        manifest.java.fullVersion = request.java.fullVersion;
        manifest.java.vendor = request.java.vendor;
        manifest.java.arch = request.java.arch;
        manifest.java.originalPath = request.java.javaExe;
        manifest.java.valid = true;
    }

    const QByteArray manifestBytes =
        QJsonDocument(manifest.toJson()).toJson(QJsonDocument::Indented);
    QByteArray globalPclBytes;
    QByteArray instancePclBytes;
    if (request.includePclConfig) {
        globalPclBytes = buildGlobalPclTemplate(request.gameDir, &result.warnings);
        instancePclBytes = buildInstancePclTemplate(version, request.includeJava, &result.warnings);
    }

    qint64 manifestExtra = qint64(manifestBytes.size());
    if (!globalPclBytes.isEmpty())
        manifestExtra += qint64(globalPclBytes.size());
    if (!instancePclBytes.isEmpty())
        manifestExtra += qint64(instancePclBytes.size());
    manifest.totalBytes = totalBytes + manifestExtra;

    // ------------------------------------------------------------------
    // 3. 写入 ZIP
    // ------------------------------------------------------------------
    ZipWriter writer(outputPath);
    QString zipError;
    if (!writer.open(&zipError))
        return fail(zipError);

    qint64 done = 0;
    const qint64 total = qMax<qint64>(manifest.totalBytes, 1);
    int lastPercent = -1;
    auto advance = [&](const QString &entryName, qint64 bytes) {
        done += bytes;
        const int percent = 10 + int(done * 90 / total);
        if (percent != lastPercent) {
            lastPercent = percent;
            report(percent, entryName);
        }
    };

    if (!writer.addData(QString::fromLatin1(PackManifest::kFileName), manifestBytes, &zipError))
        return fail(zipError);

    report(10, QStringLiteral("正在写入版本文件…"));
    for (const SourceFile &file : std::as_const(files)) {
        if (!writer.addFile(file.entryName, file.absolutePath, &zipError)) {
            result.warnings.append(zipError);
            log(QStringLiteral("[警告] ") + zipError);
            continue;
        }
        advance(file.entryName, file.size);
    }

    if (!globalPclBytes.isEmpty()) {
        if (!writer.addData(QStringLiteral("PCL/Setup.ini"), globalPclBytes, &zipError))
            return fail(zipError);
        advance(QStringLiteral("PCL/Setup.ini"), qint64(globalPclBytes.size()));
    }
    if (!instancePclBytes.isEmpty()) {
        const QString instanceEntry =
            QStringLiteral("versions/%1/PCL/Setup.ini").arg(version.id);
        if (!writer.addData(instanceEntry, instancePclBytes, &zipError))
            return fail(zipError);
        advance(instanceEntry, qint64(instancePclBytes.size()));
    }

    if (!writer.close(&zipError))
        return fail(zipError);

    const qint64 archiveSize = QFileInfo(outputPath).size();
    result.ok = true;
    result.outputPath = outputPath;
    result.fileCount = int(files.size());
    result.totalBytes = totalBytes;
    report(100, QStringLiteral("打包完成"));
    log(QStringLiteral("打包完成：%1（%2 个文件，原始 %3，压缩包 %4）")
            .arg(QDir::toNativeSeparators(outputPath))
            .arg(files.size())
            .arg(formatBytes(totalBytes), formatBytes(archiveSize)));
    return result;
}

} // namespace mcpacker
