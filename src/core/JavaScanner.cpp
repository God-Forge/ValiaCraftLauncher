#include "core/JavaScanner.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace mcpacker {
namespace {

/// 粗略校验路径指向的是有效的 Windows PE 可执行文件（避免启动 16 位/脚本文件时
/// 触发系统「不支持的 16 位应用程序」错误框）。
bool looksLikePeExecutable(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = file.read(0x2000);
    if (head.size() < 0x40 || quint8(head.at(0)) != 'M' || quint8(head.at(1)) != 'Z')
        return false;

    auto readU32 = [&head](int off) -> quint32 {
        if (off + 4 > head.size())
            return 0;
        return quint32(quint8(head.at(off))) | (quint32(quint8(head.at(off + 1))) << 8)
               | (quint32(quint8(head.at(off + 2))) << 16)
               | (quint32(quint8(head.at(off + 3))) << 24);
    };

    const quint32 peOffset = readU32(0x3C);
    if (peOffset == 0 || peOffset + 6 > quint32(head.size()))
        return false;
    if (head.at(peOffset) != 'P' || head.at(peOffset + 1) != 'E' || head.at(peOffset + 2) != 0
        || head.at(peOffset + 3) != 0)
        return false;

    const quint16 machine = quint16(quint8(head.at(peOffset + 4)))
                            | (quint16(quint8(head.at(peOffset + 5))) << 8);
    // 0x8664=x64, 0x14c=x86, 0xAA64=ARM64, 0x1c0/0x1c4=ARM
    return machine == 0x8664 || machine == 0x14c || machine == 0xAA64 || machine == 0x1c0
           || machine == 0x1c4;
}

void suppressSystemErrorDialogs()
{
#ifdef Q_OS_WIN
    static bool done = false;
    if (!done) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX | SEM_NOGPFAULTERRORBOX);
        done = true;
    }
#endif
}

} // namespace
/// 解析 "17.0.21" / "1.8.0_491" / "21.0.12.1" 的主版本号
int majorFromVersionString(const QString &version)
{
    const QStringList parts = version.split(QLatin1Char('.'));
    if (parts.isEmpty())
        return 0;
    bool ok = false;
    int first = parts.first().toInt(&ok);
    if (!ok)
        return 0;
    if (first == 1 && parts.size() >= 2) {
        // 1.8.0_491 这种老式版本号
        return parts.at(1).toInt();
    }
    return first;
}

void addCandidates(QStringList &out, const QString &baseDir, const QStringList &patterns)
{
    if (baseDir.isEmpty())
        return;
    QDir dir(baseDir);
    if (!dir.exists())
        return;
    for (const QString &pattern : patterns) {
        const QFileInfoList hits =
            dir.entryInfoList(QStringList() << pattern, QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &hit : hits)
            out.append(QDir(hit.absoluteFilePath()).absoluteFilePath(QStringLiteral("bin/java.exe")));
    }
}

bool JavaScanner::isValidJavaExecutable(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QFileInfo info(QDir::fromNativeSeparators(path));
    return info.isFile() && info.fileName().compare(QStringLiteral("java.exe"), Qt::CaseInsensitive) == 0;
}

QStringList JavaScanner::candidateExecutables(const QString &gameDir)
{
    QStringList out;

    // PATH / JAVA_HOME
    const QString pathJava = QStandardPaths::findExecutable(QStringLiteral("java"));
    if (!pathJava.isEmpty())
        out.append(pathJava);
    const QStringList envVars = {QStringLiteral("JAVA_HOME"), QStringLiteral("JDK_HOME"),
                                 QStringLiteral("JRE_HOME")};
    for (const QString &var : envVars) {
        const QString home = qEnvironmentVariable(var.toStdString().c_str());
        if (!home.isEmpty())
            out.append(QDir(home).absoluteFilePath(QStringLiteral("bin/java.exe")));
    }

    // 常见 JDK 安装目录
    const QString programFiles = qEnvironmentVariable("ProgramFiles");
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
    const QStringList vendorDirs = {
        programFiles + QStringLiteral("/Java"),
        programFilesX86 + QStringLiteral("/Java"),
        programFiles + QStringLiteral("/Eclipse Adoptium"),
        programFiles + QStringLiteral("/Eclipse Foundation"),
        programFiles + QStringLiteral("/Microsoft"),
        programFiles + QStringLiteral("/Zulu"),
        programFiles + QStringLiteral("/Amazon Corretto"),
        programFiles + QStringLiteral("/BellSoft"),
        programFiles + QStringLiteral("/Semeru"),
        programFiles + QStringLiteral("/graalvm"),
        programFiles + QStringLiteral("/Java/jdk-*"),
    };
    for (const QString &base : vendorDirs)
        addCandidates(out, base, QStringList() << QStringLiteral("*"));

    // 用户目录下的 IDE 下载的 JDK
    const QString home = qEnvironmentVariable("USERPROFILE");
    if (!home.isEmpty()) {
        addCandidates(out, home + QStringLiteral("/.jdks"), QStringList() << QStringLiteral("*"));
        addCandidates(out, home + QStringLiteral("/.gradle/jdks"),
                      QStringList() << QStringLiteral("*"));
        addCandidates(out, home + QStringLiteral("/.sdkman/candidates/java"),
                      QStringList() << QStringLiteral("*"));
        addCandidates(out, home + QStringLiteral("/.IntelliJIdea/system/jdks"),
                      QStringList() << QStringLiteral("*"));
    }

    // 游戏自带 runtime（Mojang 运行时）
    if (!gameDir.isEmpty()) {
        const QString runtimeDir = QDir(gameDir).absoluteFilePath(QStringLiteral("runtime"));
        addCandidates(out, runtimeDir, QStringList() << QStringLiteral("*"));
        const QString jreDir = QDir(gameDir).absoluteFilePath(QStringLiteral("jre"));
        addCandidates(out, jreDir, QStringList() << QStringLiteral("*"));
    }

    // 去重（按路径，大小写不敏感）
    QStringList unique;
    QSet<QString> seen;
    for (const QString &candidate : std::as_const(out)) {
        const QString native = QDir::cleanPath(QDir::fromNativeSeparators(candidate));
        const QString key = native.toLower();
        if (native.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        unique.append(native);
    }
    return unique;
}

bool JavaScanner::probe(const QString &javaExe, JavaInfo *out, QString *error)
{
    if (!out)
        return false;
    *out = JavaInfo();
    if (!isValidJavaExecutable(javaExe)) {
        if (error)
            *error = QStringLiteral("不是有效的 java.exe：%1").arg(javaExe);
        return false;
    }
    // 无效的 PE（16 位程序、脚本、损坏文件）直接跳过，避免 Windows 弹错误框
    if (!looksLikePeExecutable(javaExe)) {
        if (error)
            *error = QStringLiteral("跳过 %1：不是有效的 Windows 可执行文件（可能是脚本或已损坏）")
                         .arg(QDir::toNativeSeparators(javaExe));
        return false;
    }
    suppressSystemErrorDialogs();

    QProcess proc;
    proc.setProgram(javaExe);
    proc.setArguments(QStringList() << QStringLiteral("-XshowSettings:properties")
                                    << QStringLiteral("-version"));
    proc.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    // 隐藏 java.exe 的控制台窗口，避免扫描时黑框闪烁
    proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
        if (args->startupInfo) {
            args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
            args->startupInfo->wShowWindow = SW_HIDE;
        }
    });
#endif
    proc.start();
    if (!proc.waitForStarted(3000)) {
        if (error) {
            // QProcess 在 Windows 上的 errorString 带有 "%1" 占位符模板，换成可读提示
            const QString reason = proc.errorString();
            const QString hint = reason.contains(QStringLiteral("%1"))
                                     ? QStringLiteral("无法启动（可能为 32 位或已损坏的 Java）")
                                     : reason;
            *error = QStringLiteral("跳过 %1：%2")
                         .arg(QFileInfo(javaExe).absoluteFilePath(), hint);
        }
        return false;
    }
    if (!proc.waitForFinished(6000)) {
        proc.kill();
        proc.waitForFinished(1000);
        if (error)
            *error = QStringLiteral("跳过 %1（探测超时）").arg(QFileInfo(javaExe).fileName());
        return false;
    }

    const QString output = QString::fromLocal8Bit(proc.readAll());
    QString version;
    QString vendor;
    QString arch;
    QString javaHome;

    static const QRegularExpression reProp(
        QStringLiteral("^\\s*([A-Za-z0-9_.]+)\\s*=\\s*(.+?)\\s*$"));
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const auto m = reProp.match(line);
        if (!m.hasMatch())
            continue;
        const QString key = m.captured(1);
        const QString value = m.captured(2).trimmed();
        if (key == QStringLiteral("java.version"))
            version = value;
        else if (key == QStringLiteral("java.vendor"))
            vendor = value;
        else if (key == QStringLiteral("os.arch"))
            arch = value;
        else if (key == QStringLiteral("java.home"))
            javaHome = value;
    }

    if (version.isEmpty()) {
        // 老版本 JVM 的输出：openjdk version "1.8.0_491"
        static const QRegularExpression reVersion(
            QStringLiteral("version\\s+\"([^\"]+)\""));
        const auto m = reVersion.match(output);
        if (m.hasMatch())
            version = m.captured(1);
    }

    const int major = majorFromVersionString(version);
    if (major <= 0) {
        if (error)
            *error = QStringLiteral("无法解析 Java 版本信息：%1").arg(javaExe);
        return false;
    }

    out->javaExe = QDir::cleanPath(QDir::fromNativeSeparators(javaExe));
    out->javaHome = javaHome.isEmpty() ? QFileInfo(out->javaExe).absolutePath() + QStringLiteral("/..")
                                       : QDir::cleanPath(QDir::fromNativeSeparators(javaHome));
    out->javaHome = QDir::cleanPath(out->javaHome);
    out->major = major;
    out->fullVersion = version;
    out->vendor = vendor;
    out->arch = arch;
    out->valid = true;
    return true;
}

JavaScanner::ScanResult JavaScanner::scan(const QString &gameDir, bool includeGameRuntime)
{
    ScanResult result;
    const QStringList candidates = candidateExecutables(includeGameRuntime ? gameDir : QString());

    QSet<QString> seenHomes;
    for (const QString &candidate : candidates) {
        if (!isValidJavaExecutable(candidate))
            continue;
        JavaInfo info;
        QString error;
        if (!probe(candidate, &info, &error)) {
            result.notes.append(error);
            continue;
        }
        const QString key = info.javaHome.toLower();
        if (seenHomes.contains(key))
            continue;
        seenHomes.insert(key);
        result.javas.append(info);
    }

    std::sort(result.javas.begin(), result.javas.end(), [](const JavaInfo &a, const JavaInfo &b) {
        if (a.major != b.major)
            return a.major > b.major;
        return a.javaExe.compare(b.javaExe, Qt::CaseInsensitive) < 0;
    });
    return result;
}

JavaInfo JavaScanner::pickBest(const QVector<JavaInfo> &javas, int requiredMajor)
{
    if (javas.isEmpty())
        return JavaInfo();

    // 精确匹配
    for (const JavaInfo &info : javas) {
        if (info.major == requiredMajor)
            return info;
    }
    // 比需求高的版本里最小
    JavaInfo bestHigher;
    for (const JavaInfo &info : javas) {
        if (info.major > requiredMajor
            && (bestHigher.major == 0 || info.major < bestHigher.major))
            bestHigher = info;
    }
    if (bestHigher.major > 0)
        return bestHigher;

    // 否则给最高版本
    JavaInfo best = javas.first();
    for (const JavaInfo &info : javas) {
        if (info.major > best.major)
            best = info;
    }
    return best;
}

} // namespace mcpacker
