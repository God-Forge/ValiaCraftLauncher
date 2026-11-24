#include "core/Unpacker.h"

#include "core/JavaScanner.h"
#include "core/MinecraftLayout.h"
#include "core/PclSettings.h"
#include "core/ZipReader.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace mcpacker {
namespace {

QString safeEntryName(const QString &name)
{
    QString out = name;
    out.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (out.startsWith(QLatin1Char('/')))
        out.remove(0, 1);
    return out;
}

/// 生成不冲突的文件名：xxx (2).ext
QString uniquePath(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.exists())
        return path;
    const QString dirPath = fi.absolutePath();
    const QString base = fi.completeBaseName();
    const QString suffix = fi.suffix();
    for (int n = 2; n < 10000; ++n) {
        const QString candidate =
            suffix.isEmpty()
                ? QStringLiteral("%1/%2 (%3)").arg(dirPath, base).arg(n)
                : QStringLiteral("%1/%2 (%3).%4").arg(dirPath, base).arg(n).arg(suffix);
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
    return path;
}

} // namespace

void Unpacker::report(int percent, const QString &message) const
{
    if (m_progress)
        m_progress(qBound(0, percent, 100), message);
}

void Unpacker::log(const QString &message) const
{
    if (m_log)
        m_log(message);
}

QString Unpacker::remapVersionEntry(const QString &entryName, const QString &oldId,
                                    const QString &newId)
{
    if (oldId.isEmpty() || newId.isEmpty() || oldId == newId)
        return entryName;

    const QString prefix = QStringLiteral("versions/%1/").arg(oldId);
    if (!entryName.startsWith(prefix))
        return entryName;

    QString rest = entryName.mid(prefix.size());
    if (rest == oldId + QStringLiteral(".json"))
        rest = newId + QStringLiteral(".json");
    else if (rest == oldId + QStringLiteral(".jar"))
        rest = newId + QStringLiteral(".jar");

    return QStringLiteral("versions/%1/").arg(newId) + rest;
}

QString Unpacker::resolveTargetGameDir(const QString &target)
{
    const QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(target.trimmed()));
    if (cleaned.isEmpty())
        return {};

    const QFileInfo info(cleaned);
    if (info.exists() && info.isDir() && !MinecraftLayout::looksLikeGameDir(cleaned)) {
        const QDir sub(QDir(cleaned).absoluteFilePath(QStringLiteral(".minecraft")));
        if (sub.exists())
            return QDir::cleanPath(sub.absolutePath());
    }
    return cleaned;
}

bool Unpacker::inspect(const QString &packagePath, PackManifest *manifest, QString *error)
{
    if (!manifest)
        return false;
    ZipReader reader;
    QString openError;
    if (!reader.open(packagePath, &openError)) {
        if (error)
            *error = openError;
        return false;
    }
    const ZipEntry *entry = reader.find(QString::fromLatin1(PackManifest::kFileName));
    if (!entry) {
        if (error)
            *error = QStringLiteral("压缩包内没有 manifest.json，可能不是 mcPacker 生成的整合包");
        return false;
    }
    QString readError;
    const QByteArray data = reader.read(*entry, &readError);
    if (!readError.isEmpty()) {
        if (error)
            *error = readError;
        return false;
    }
    bool valid = false;
    *manifest = PackManifest::fromJson(QJsonDocument::fromJson(data).object(), &valid);
    if (!valid) {
        if (error)
            *error = QStringLiteral("manifest.json 格式不正确");
        return false;
    }
    return true;
}

void Unpacker::fixVersionJsonId(const QString &versionDir, const QString &newId)
{
    const QDir dir(versionDir);
    const QString jsonPath = dir.absoluteFilePath(newId + QStringLiteral(".json"));
    if (!QFileInfo::exists(jsonPath))
        return;
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray raw = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject())
        return;
    QJsonObject object = doc.object();
    if (object.value(QStringLiteral("id")).toString() == newId)
        return;
    object.insert(QStringLiteral("id"), newId);
    doc.setObject(object);

    QSaveFile out(jsonPath);
    if (!out.open(QIODevice::WriteOnly))
        return;
    out.write(doc.toJson(QJsonDocument::Indented));
    out.commit();
}

bool Unpacker::applyPlaceholders(const QString &filePath, const QString &gameDir,
                                 const QString &javaHome)
{
    if (!QFileInfo::exists(filePath))
        return true;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QString text = QString::fromUtf8(file.readAll());
    file.close();

    const QString original = text;
    const QString gameDirText = QDir::toNativeSeparators(gameDir);
    if (!gameDirText.isEmpty())
        text.replace(QStringLiteral("{{GAME_DIR}}"), gameDirText);

    if (!javaHome.isEmpty())
        text.replace(QStringLiteral("{{JAVA_HOME}}"), QDir::toNativeSeparators(javaHome));
    else
        text.replace(QStringLiteral("{{JAVA_HOME}}"), QString());

    // 占位符替换后可能出现 "VersionArgumentJava:bin/java.exe" 这类残缺值，交给 writePclConfigs 修正
    if (text == original)
        return true;

    QSaveFile out(filePath);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(text.toUtf8());
    return out.commit();
}

UnpackResult Unpacker::run(const UnpackRequest &request)
{
    UnpackResult result;
    auto fail = [&](const QString &message) {
        result.ok = false;
        result.error = message;
        log(QStringLiteral("[错误] ") + message);
        return result;
    };

    if (request.packagePath.isEmpty() || !QFileInfo::exists(request.packagePath))
        return fail(QStringLiteral("压缩包不存在：%1").arg(request.packagePath));
    if (request.targetGameDir.trimmed().isEmpty())
        return fail(QStringLiteral("请先选择目标游戏目录"));

    const QString gameDir = resolveTargetGameDir(request.targetGameDir);
    if (gameDir.isEmpty())
        return fail(QStringLiteral("目标游戏目录无效"));
    if (!QDir().mkpath(gameDir))
        return fail(QStringLiteral("无法创建目标目录：%1").arg(gameDir));

    ZipReader reader;
    QString openError;
    if (!reader.open(request.packagePath, &openError))
        return fail(openError);

    // ------------------------------------------------------------------
    // 1. 读取清单（缺失时进入兼容模式）
    // ------------------------------------------------------------------
    PackManifest manifest;
    bool hasManifest = false;
    if (const ZipEntry *entry = reader.find(QString::fromLatin1(PackManifest::kFileName))) {
        QString readError;
        const QByteArray data = reader.read(*entry, &readError);
        if (!readError.isEmpty())
            return fail(QStringLiteral("读取 manifest.json 失败：%1").arg(readError));
        manifest = PackManifest::fromJson(QJsonDocument::fromJson(data).object(), &hasManifest);
    }

    if (!hasManifest) {
        // 兼容模式：从 versions/<id>/<id>.json 推断
        QString inferredId;
        for (const ZipEntry &entry : reader.entries()) {
            const QStringList parts = safeEntryName(entry.name).split(QLatin1Char('/'));
            if (parts.size() == 3 && parts.at(0) == QStringLiteral("versions")
                && parts.at(2) == parts.at(1) + QStringLiteral(".json")) {
                inferredId = parts.at(1);
                break;
            }
        }
        if (inferredId.isEmpty())
            return fail(QStringLiteral("压缩包既不包含 manifest.json，也找不到 versions/<版本>/<版本>.json 结构"));
        manifest = PackManifest();
        manifest.version.id = inferredId;
        manifest.tool = QStringLiteral("未知（兼容模式）");
        manifest.options.isolation = true;
        manifest.options.includeInstanceFiles = false;
        result.messages.append(QStringLiteral("未找到 manifest.json，已按传统整合包结构解包"));
        log(QStringLiteral("[提示] 未找到 manifest.json，按普通 ZIP 解包"));
    }

    QString versionId = request.versionNameOverride.trimmed();
    if (versionId.isEmpty())
        versionId = manifest.version.id;
    if (versionId.isEmpty())
        return fail(QStringLiteral("无法确定版本名称"));

    const QString oldVersionId = manifest.version.id;
    if (!oldVersionId.isEmpty() && versionId != oldVersionId) {
        QDir().mkpath(QDir(gameDir).absoluteFilePath(QStringLiteral("versions/%1").arg(versionId)));
        log(QStringLiteral("版本重命名：%1 → %2").arg(oldVersionId, versionId));
    }

    // ------------------------------------------------------------------
    // 2. 解压
    // ------------------------------------------------------------------
    const qint64 totalBytes = qMax<qint64>(reader.totalUncompressedBytes(), 1);
    qint64 doneBytes = 0;
    int lastPercent = -1;

    report(0, QStringLiteral("开始解压…"));
    for (const ZipEntry &entry : reader.entries()) {
        if (entry.name == QString::fromLatin1(PackManifest::kFileName))
            continue;

        const QString remapped = remapVersionEntry(safeEntryName(entry.name), oldVersionId, versionId);
        if (entry.isDirectory()) {
            const QString target = ZipReader::safeTargetPath(gameDir, remapped);
            if (!target.isEmpty())
                QDir().mkpath(target);
            continue;
        }

        const QString target = ZipReader::safeTargetPath(gameDir, remapped);
        if (target.isEmpty()) {
            result.failedFiles.append(entry.name + QStringLiteral("（非法路径）"));
            continue;
        }

        QString finalPath = target;
        if (QFileInfo::exists(finalPath)) {
            if (request.policy == ConflictPolicy::Skip) {
                result.skippedFiles.append(remapped);
                doneBytes += qint64(entry.uncompressedSize);
                continue;
            }
            if (request.policy == ConflictPolicy::Rename)
                finalPath = uniquePath(finalPath);
        }

        QDir().mkpath(QFileInfo(finalPath).absolutePath());

        QString readError;
        const QByteArray data = reader.read(entry, &readError);
        if (!readError.isEmpty()) {
            result.failedFiles.append(entry.name + QStringLiteral("（%1）").arg(readError));
            continue;
        }

        QSaveFile out(finalPath);
        if (!out.open(QIODevice::WriteOnly)) {
            result.failedFiles.append(
                entry.name + QStringLiteral("（无法写入：%1）").arg(out.errorString()));
            continue;
        }
        if (out.write(data) != data.size()) {
            out.cancelWriting();
            result.failedFiles.append(
                entry.name + QStringLiteral("（写入不完整）"));
            continue;
        }
        if (!out.commit()) {
            result.failedFiles.append(entry.name + QStringLiteral("（提交失败）"));
            continue;
        }
        result.writtenFiles.append(remapped);

        doneBytes += qint64(entry.uncompressedSize);
        const int percent = 5 + int(doneBytes * 80 / totalBytes);
        if (percent != lastPercent) {
            lastPercent = percent;
            report(percent, remapped);
        }
    }

    // ------------------------------------------------------------------
    // 3. Java 与版本 json 修正
    // ------------------------------------------------------------------
    const QString versionDir =
        QDir(gameDir).absoluteFilePath(QStringLiteral("versions/%1").arg(versionId));
    QString javaExe;
    QString javaHome;
    if (manifest.hasJava()) {
        QString rel = manifest.java.exeRelative;
        if (rel.isEmpty())
            rel = manifest.java.relativePath + QStringLiteral("/bin/java.exe");
        const QString candidate = QDir(gameDir).absoluteFilePath(rel);
        if (QFileInfo::exists(candidate)) {
            javaExe = candidate;
            javaHome = QDir(gameDir).absoluteFilePath(manifest.java.relativePath);
            if (!QFileInfo::exists(QDir(javaHome).absoluteFilePath(QStringLiteral("bin/java.exe")))) {
                result.messages.append(
                    QStringLiteral("提示：包内 Java 结构异常（bin/java.exe 不存在）"));
            }
        } else {
            result.messages.append(
                QStringLiteral("警告：包内声明的 Java 运行时没有解出：%1").arg(rel));
        }
    }

    if (versionId != oldVersionId && !oldVersionId.isEmpty())
        fixVersionJsonId(versionDir, versionId);

    // ------------------------------------------------------------------
    // 4. PCL 配置
    // ------------------------------------------------------------------
    report(88, QStringLiteral("正在写入 PCL 配置…"));
    if (request.writePclConfig) {
        const QString globalIni = QDir(gameDir).absoluteFilePath(QStringLiteral("PCL/Setup.ini"));
        const QString instanceIni =
            QDir(versionDir).absoluteFilePath(QStringLiteral("PCL/Setup.ini"));
        applyPlaceholders(globalIni, gameDir, javaHome);
        applyPlaceholders(instanceIni, gameDir, javaHome);

        PclWriteOptions options;
        options.gameDir = gameDir;
        options.versionId = versionId;
        options.version = manifest.version;
        options.version.id = versionId;
        options.javaExe = javaExe;
        options.isolation = manifest.options.isolation;
        options.writeGlobal = true;
        options.writeInstance = true;

        QString pclError;
        const PclWriteResult written = writePclConfigs(options, &pclError);
        result.messages.append(written.messages);
        if (!written.ok) {
            result.messages.append(QStringLiteral("警告：部分 PCL 配置写入失败"));
            log(QStringLiteral("[警告] ") + pclError);
        }
    }

    // 保存一份导入记录（便于日后排查）
    if (hasManifest) {
        const QString recordDir = QDir(gameDir).absoluteFilePath(QStringLiteral("PCL/mcPacker"));
        QDir().mkpath(recordDir);
        const QString recordPath =
            QDir(recordDir).absoluteFilePath(QStringLiteral("%1-%2.json")
                                                 .arg(versionId,
                                                      QDateTime::currentDateTime().toString(
                                                          QStringLiteral("yyyyMMdd-HHmmss"))));
        QFile record(recordPath);
        if (record.open(QIODevice::WriteOnly)) {
            manifest.version.id = versionId;
            record.write(QJsonDocument(manifest.toJson()).toJson(QJsonDocument::Indented));
            record.close();
        }
    }

    result.ok = true;
    result.gameDir = gameDir;
    result.versionId = versionId;
    result.javaExe = javaExe;
    report(100, QStringLiteral("解包完成"));
    log(QStringLiteral("解包完成：%1").arg(QDir::toNativeSeparators(gameDir)));
    log(QStringLiteral("写入 %1 个文件，跳过 %2 个，失败 %3 个")
            .arg(result.writtenFiles.size())
            .arg(result.skippedFiles.size())
            .arg(result.failedFiles.size()));
    if (!javaExe.isEmpty())
        log(QStringLiteral("包内 Java：%1").arg(QDir::toNativeSeparators(javaExe)));
    return result;
}

} // namespace mcpacker
