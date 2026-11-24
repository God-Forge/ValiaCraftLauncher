#include "core/PclSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>

namespace mcpacker {

bool PclSettings::load(const QString &path, QString *error)
{
    clear();
    QFile file(path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 PCL 配置：%1（%2）").arg(path, file.errorString());
        return false;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QString text = QString::fromUtf8(raw);
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);

    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));
    for (const QString &rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        const qsizetype sep = line.indexOf(QLatin1Char(':'));
        if (sep <= 0)
            continue;
        const QString key = line.left(sep).trimmed();
        const QString value = line.mid(sep + 1);
        if (key.isEmpty())
            continue;
        setValue(key, value);
    }
    return true;
}

bool PclSettings::save(const QString &path, QString *error) const
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error)
            *error = QStringLiteral("无法创建配置目录：%1").arg(info.absolutePath());
        return false;
    }

    QString text;
    text.reserve(m_order.size() * 48);
    for (const QString &key : m_order)
        text += key + QLatin1Char(':') + m_values.value(key) + QStringLiteral("\r\n");

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入 PCL 配置：%1（%2）").arg(path, file.errorString());
        return false;
    }
    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        if (error)
            *error = QStringLiteral("写入 PCL 配置不完整：%1").arg(path);
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("提交 PCL 配置失败：%1").arg(path);
        return false;
    }
    return true;
}

bool PclSettings::contains(const QString &key) const
{
    return m_values.contains(key);
}

QString PclSettings::value(const QString &key, const QString &defaultValue) const
{
    return m_values.value(key, defaultValue);
}

void PclSettings::setValue(const QString &key, const QString &value)
{
    if (!m_values.contains(key))
        m_order.append(key);
    m_values.insert(key, value);
}

void PclSettings::remove(const QString &key)
{
    if (!m_values.contains(key))
        return;
    m_values.remove(key);
    m_order.removeAll(key);
}

QStringList PclSettings::keys() const
{
    QStringList list;
    list.reserve(m_order.size());
    for (const QString &k : m_order)
        list.append(k);
    return list;
}

void PclSettings::clear()
{
    m_order.clear();
    m_values.clear();
}

QString PclSettings::buildVersionInfo(const VersionInfo &version)
{
    QString base;
    if (version.type == QStringLiteral("release") || version.type.isEmpty())
        base = QStringLiteral("正式版 ");
    else if (version.type == QStringLiteral("snapshot"))
        base = QStringLiteral("快照 ");
    else if (version.type == QStringLiteral("old_beta"))
        base = QStringLiteral("远古 Beta ");
    else if (version.type == QStringLiteral("old_alpha"))
        base = QStringLiteral("远古 Alpha ");
    else
        base = QStringLiteral("未知 ");

    QString mc = version.mcVersion;
    if (mc.isEmpty())
        mc = version.id;
    base += mc;

    if (!version.loader.isEmpty() && version.loader != QStringLiteral("Vanilla")
        && version.loader != QStringLiteral("OptiFine")) {
        if (version.loaderVersion.isEmpty())
            base += QStringLiteral(", ") + version.loader;
        else
            base += QStringLiteral(", ") + version.loader + QLatin1Char(' ')
                  + version.loaderVersion;
    }
    if (version.loader == QStringLiteral("OptiFine")) {
        if (version.loaderVersion.isEmpty())
            base += QStringLiteral(", OptiFine");
        else
            base += QStringLiteral(", OptiFine ") + version.loaderVersion;
    }
    return base;
}

bool readPclIsolation(const QString &versionDir)
{
    const QString iniPath = QDir(versionDir).absoluteFilePath(QStringLiteral("PCL/Setup.ini"));
    // PCL2 新版本默认开启版本隔离；没有配置文件时按开启处理
    if (!QFileInfo::exists(iniPath))
        return true;

    PclSettings settings;
    QString ignored;
    settings.load(iniPath, &ignored);

    const QString v2 = settings.value(QString::fromLatin1(PclKeys::kIndieV2));
    if (!v2.isEmpty())
        return v2.compare(QStringLiteral("True"), Qt::CaseInsensitive) == 0;

    const QString v1 = settings.value(QString::fromLatin1(PclKeys::kIndie));
    if (!v1.isEmpty())
        return v1.trimmed() != QStringLiteral("0");
    // 配置为空（只有空文件）时也按 PCL 新版本默认开启隔离处理
    return true;
}

PclWriteResult writePclConfigs(const PclWriteOptions &options, QString *error)
{
    PclWriteResult result;
    if (options.gameDir.isEmpty()) {
        if (error)
            *error = QStringLiteral("游戏目录为空，无法写入 PCL 配置");
        result.ok = false;
        return result;
    }

    const QDir gameDir(options.gameDir);
    const QString globalPath = gameDir.absoluteFilePath(QStringLiteral("PCL/Setup.ini"));

    if (options.writeGlobal) {
        PclSettings global;
        QString loadError;
        global.load(globalPath, &loadError);
        QString folder = QDir::toNativeSeparators(gameDir.absolutePath());
        if (!folder.endsWith(QLatin1Char('\\')) && !folder.endsWith(QLatin1Char('/')))
            folder += QLatin1Char('\\');
        global.setValue(QString::fromLatin1(PclKeys::kLaunchFolderSelect), folder);

        QString saveError;
        if (global.save(globalPath, &saveError)) {
            result.writtenFiles.append(globalPath);
            result.messages.append(QStringLiteral("已写入 PCL 全局配置（启动文件夹）：%1")
                                       .arg(QDir::toNativeSeparators(globalPath)));
        } else {
            result.ok = false;
            result.messages.append(saveError);
        }
    }

    if (options.writeInstance) {
        const QString versionDir =
            gameDir.absoluteFilePath(QStringLiteral("versions/%1").arg(options.versionId));
        if (!QDir().mkpath(versionDir)) {
            result.ok = false;
            result.messages.append(
                QStringLiteral("无法创建版本目录：%1").arg(QDir::toNativeSeparators(versionDir)));
        } else {
            const QString instancePath = QDir(versionDir).absoluteFilePath(QStringLiteral("PCL/Setup.ini"));
            PclSettings settings;
            QString loadError;
            settings.load(instancePath, &loadError);

            const bool isolation = options.isolation;
            settings.setValue(QString::fromLatin1(PclKeys::kIndieV2),
                              isolation ? QStringLiteral("True") : QStringLiteral("False"));
            settings.setValue(QString::fromLatin1(PclKeys::kIndie),
                              isolation ? QStringLiteral("1") : QStringLiteral("0"));

            const VersionInfo &ver = options.version;
            // 注意：真实 PCL 样本中 VersionVanilla 存的是内部格式（如 20.0.1），
            // 为避免破坏 PCL 的解析，这里只写显示名，VersionVanilla 保留原值。
            if (!ver.mcVersion.isEmpty())
                settings.setValue(QString::fromLatin1(PclKeys::kVanillaName), ver.mcVersion);
            else if (!ver.id.isEmpty())
                settings.setValue(QString::fromLatin1(PclKeys::kVanillaName), ver.id);

            const QString loader = ver.loader;
            if (loader == QStringLiteral("Forge") && !ver.loaderVersion.isEmpty()) {
                settings.setValue(QString::fromLatin1(PclKeys::kForge), ver.loaderVersion);
                settings.remove(QString::fromLatin1(PclKeys::kNeoForge));
                settings.remove(QString::fromLatin1(PclKeys::kFabric));
                settings.remove(QString::fromLatin1(PclKeys::kQuilt));
            } else if (loader == QStringLiteral("NeoForge") && !ver.loaderVersion.isEmpty()) {
                settings.setValue(QString::fromLatin1(PclKeys::kNeoForge), ver.loaderVersion);
                settings.remove(QString::fromLatin1(PclKeys::kForge));
                settings.remove(QString::fromLatin1(PclKeys::kFabric));
                settings.remove(QString::fromLatin1(PclKeys::kQuilt));
            } else if (loader == QStringLiteral("Fabric") && !ver.loaderVersion.isEmpty()) {
                settings.setValue(QString::fromLatin1(PclKeys::kFabric), ver.loaderVersion);
                settings.remove(QString::fromLatin1(PclKeys::kForge));
                settings.remove(QString::fromLatin1(PclKeys::kNeoForge));
                settings.remove(QString::fromLatin1(PclKeys::kQuilt));
            } else if (loader == QStringLiteral("Quilt") && !ver.loaderVersion.isEmpty()) {
                settings.setValue(QString::fromLatin1(PclKeys::kQuilt), ver.loaderVersion);
                settings.remove(QString::fromLatin1(PclKeys::kForge));
                settings.remove(QString::fromLatin1(PclKeys::kNeoForge));
                settings.remove(QString::fromLatin1(PclKeys::kFabric));
            }
            if (loader != QStringLiteral("LiteLoader"))
                settings.setValue(QString::fromLatin1(PclKeys::kLiteLoader), QStringLiteral("False"));

            const QString info = PclSettings::buildVersionInfo(ver);
            if (!info.isEmpty())
                settings.setValue(QString::fromLatin1(PclKeys::kInfo), info);

            if (ver.releaseTime.isValid()) {
                settings.setValue(QString::fromLatin1(PclKeys::kReleaseTime),
                                  ver.releaseTime.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
            }

            if (!options.javaExe.isEmpty()) {
                settings.setValue(QString::fromLatin1(PclKeys::kJava),
                                  QDir::toNativeSeparators(options.javaExe));
            } else {
                // 不遗留来自其他机器的绝对路径
                settings.remove(QString::fromLatin1(PclKeys::kJava));
            }

            QString saveError;
            if (settings.save(instancePath, &saveError)) {
                result.writtenFiles.append(instancePath);
                result.messages.append(
                    QStringLiteral("已写入版本级 PCL 配置：%1")
                        .arg(QDir::toNativeSeparators(instancePath)));
            } else {
                result.ok = false;
                result.messages.append(saveError);
            }
        }
    }

    if (!result.ok && error && !result.messages.isEmpty())
        *error = result.messages.join(QLatin1Char('\n'));
    return result;
}

} // namespace mcpacker
