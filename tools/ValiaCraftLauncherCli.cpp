// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 neolxsky
//
// ValiaCraftLauncher 命令行入口：用于脚本化打包/解包，以及自动化测试。
// GUI 主程序（ValiaCraftLauncher.exe）无窗口，本程序为控制台程序（ValiaCraftLauncherCli.exe）。

#include "core/Common.h"
#include "core/JavaScanner.h"
#include "core/MinecraftLayout.h"
#include "core/Packer.h"
#include "core/Unpacker.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>

using namespace mcpacker;

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    stream.setEncoding(QStringConverter::Utf8);
    return stream;
}

void printLine(const QString &text)
{
    out() << text << Qt::endl;
}

void progressFn(int percent, const QString &message)
{
    out() << QStringLiteral("\r[%1%] %2").arg(percent, 3).arg(message.left(70))
          << QString(10, QLatin1Char(' ')) << Qt::flush;
    if (percent >= 100)
        out() << Qt::endl;
}

void logFn(const QString &message)
{
    out() << Qt::endl << QStringLiteral("  · ") << message << Qt::endl;
}

int runList(const QString &gameDir)
{
    const QVector<VersionInfo> versions = MinecraftLayout::scanVersions(gameDir);
    if (versions.isEmpty()) {
        printLine(QStringLiteral("没有在 %1 下找到版本").arg(gameDir));
        return 1;
    }
    for (const VersionInfo &version : versions) {
        printLine(QStringLiteral("%1\n    %2").arg(version.id, version.describe()));
    }
    return 0;
}

int runPack(const QString &gameDir, const QString &versionId, const QString &output,
            const QString &javaExe, bool includeLibraries, bool includeAssets,
            bool includeInstance, bool includePcl, const QString &runtimeName)
{
    VersionInfo version = MinecraftLayout::readVersion(gameDir, versionId);
    if (!version.valid) {
        printLine(QStringLiteral("版本不存在或无法解析：%1").arg(versionId));
        return 2;
    }

    PackRequest request;
    request.gameDir = gameDir;
    request.version = version;
    request.includeJava = !javaExe.isEmpty();
    if (request.includeJava) {
        JavaInfo info;
        QString error;
        if (!JavaScanner::probe(javaExe, &info, &error)) {
            printLine(QStringLiteral("无法识别 Java：%1").arg(error));
            return 3;
        }
        request.java = info;
        printLine(QStringLiteral("Java：%1").arg(info.describe()));
    }
    request.javaRuntimeName = runtimeName;
    request.includeLibraries = includeLibraries;
    request.includeAssets = includeAssets;
    request.includeInstanceFiles = includeInstance;
    request.includePclConfig = includePcl;

    QString target = output;
    if (target.isEmpty()) {
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm"));
        target = QDir(QFileInfo(gameDir).absolutePath())
                     .absoluteFilePath(QStringLiteral("%1-%2.mcpk").arg(versionId, stamp));
    }
    request.outputPath = target;

    Packer packer(progressFn, logFn);
    const PackResult result = packer.run(request);
    if (!result.ok) {
        printLine(QStringLiteral("打包失败：%1").arg(result.error));
        return 4;
    }
    printLine(QStringLiteral("打包完成：%1（%2 个文件，%3）")
                  .arg(QDir::toNativeSeparators(result.outputPath))
                  .arg(result.fileCount)
                  .arg(formatBytes(QFileInfo(result.outputPath).size())));
    return 0;
}

int runUnpack(const QString &packagePath, const QString &target, ConflictPolicy policy,
              bool writePcl, const QString &versionName)
{
    UnpackRequest request;
    request.packagePath = packagePath;
    request.targetGameDir = target;
    request.policy = policy;
    request.writePclConfig = writePcl;
    request.versionNameOverride = versionName;

    Unpacker unpacker(progressFn, logFn);
    const UnpackResult result = unpacker.run(request);
    if (!result.ok) {
        printLine(QStringLiteral("解包失败：%1").arg(result.error));
        return 5;
    }
    printLine(QStringLiteral("解包完成：%1").arg(QDir::toNativeSeparators(result.gameDir)));
    printLine(QStringLiteral("版本：%1").arg(result.versionId));
    printLine(QStringLiteral("写入 %1 / 跳过 %2 / 失败 %3")
                  .arg(result.writtenFiles.size())
                  .arg(result.skippedFiles.size())
                  .arg(result.failedFiles.size()));
    if (!result.javaExe.isEmpty())
        printLine(QStringLiteral("Java：%1").arg(QDir::toNativeSeparators(result.javaExe)));
    for (const QString &message : result.messages)
        printLine(QStringLiteral("  · ") + message);
    for (const QString &failed : result.failedFiles)
        printLine(QStringLiteral("  ! ") + failed);
    return result.failedFiles.isEmpty() ? 0 : 6;
}

/// 构造一个假的 .minecraft 做端到端自测（打包 → 解包 → 校验 PCL 配置）
int runSelfTest(bool verbose)
{
    QTemporaryDir temp;
    if (!temp.isValid()) {
        printLine(QStringLiteral("无法创建临时目录"));
        return 10;
    }
    const QString source = temp.filePath(QStringLiteral("source/.minecraft"));
    const QString target = temp.filePath(QStringLiteral("target/.minecraft"));
    const QString package = temp.filePath(QStringLiteral("test.mcpk"));

    // 构造版本
    const QString versionId = QStringLiteral("测试版本-1.20.1");
    const QString versionDir = QDir(source).absoluteFilePath(QStringLiteral("versions/%1").arg(versionId));
    QDir().mkpath(versionDir + QStringLiteral("/mods"));
    QDir().mkpath(versionDir + QStringLiteral("/config"));

    const QString json = QStringLiteral(
        "{\"id\":\"%1\",\"type\":\"release\",\"releaseTime\":\"2023-06-12T21:25:00+08:00\","
        "\"javaVersion\":{\"majorVersion\":17},"
        "\"libraries\":[{\"name\":\"net.minecraft:client:1.20.1\"},"
        "{\"name\":\"net.minecraftforge:forge:47.4.16\"}]}")
                             .arg(versionId);
    auto writeFile = [](const QString &path, const QByteArray &data) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        return file.write(data) == data.size();
    };

    if (!writeFile(versionDir + QStringLiteral("/%1.json").arg(versionId), json.toUtf8())
        || !writeFile(versionDir + QStringLiteral("/mods/example.jar"), QByteArray(4096, 'x'))
        || !writeFile(versionDir + QStringLiteral("/mods/中文模组.jar"), QByteArray(2048, 'y'))
        || !writeFile(versionDir + QStringLiteral("/config/配置.txt"),
                      QByteArray("中文内容 test 123\n")))
        {
        printLine(QStringLiteral("构造测试数据失败"));
        return 11;
    }
    // 空的 PCL 配置
    writeFile(versionDir + QStringLiteral("/PCL/Setup.ini"), QByteArray());

    // 打包
    PackRequest request;
    request.gameDir = source;
    request.version = MinecraftLayout::readVersion(source, versionId);
    request.includeJava = false;
    request.includePclConfig = true;
    request.outputPath = package;

    Packer packer(nullptr, verbose ? logFn : LogFn());
    const PackResult packed = packer.run(request);
    if (!packed.ok) {
        printLine(QStringLiteral("自测：打包失败：%1").arg(packed.error));
        return 12;
    }
    if (!QFileInfo::exists(package)) {
        printLine(QStringLiteral("自测：没有生成压缩包"));
        return 13;
    }

    // 解包
    UnpackRequest unpackRequest;
    unpackRequest.packagePath = package;
    unpackRequest.targetGameDir = target;
    unpackRequest.policy = ConflictPolicy::Overwrite;
    unpackRequest.writePclConfig = true;
    Unpacker unpacker(nullptr, verbose ? logFn : LogFn());
    const UnpackResult unpacked = unpacker.run(unpackRequest);
    if (!unpacked.ok) {
        printLine(QStringLiteral("自测：解包失败：%1").arg(unpacked.error));
        return 14;
    }

    // 校验文件
    int failures = 0;
    const QVector<QPair<QString, qint64>> expected = {
        {QStringLiteral("versions/%1/%1.json").arg(versionId), -1},
        {QStringLiteral("versions/%1/mods/example.jar").arg(versionId), 4096},
        {QStringLiteral("versions/%1/mods/中文模组.jar").arg(versionId), 2048},
        {QStringLiteral("versions/%1/config/配置.txt").arg(versionId), -1},
    };
    for (const auto &item : expected) {
        const QString path = QDir(target).absoluteFilePath(item.first);
        if (!QFileInfo::exists(path)) {
            printLine(QStringLiteral("自测：缺少文件 %1").arg(item.first));
            ++failures;
            continue;
        }
        if (item.second >= 0 && QFileInfo(path).size() != item.second) {
            printLine(QStringLiteral("自测：文件大小不符 %1").arg(item.first));
            ++failures;
        }
    }

    // 校验 PCL 配置
    auto readKey = [](const QString &path, const QString &key) -> QString {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const qsizetype sep = line.indexOf(QLatin1Char(':'));
            if (sep > 0 && line.left(sep).trimmed() == key)
                return line.mid(sep + 1).trimmed();
        }
        return {};
    };

    const QString instanceIni =
        QDir(target).absoluteFilePath(QStringLiteral("versions/%1/PCL/Setup.ini").arg(versionId));
    const QString globalIni = QDir(target).absoluteFilePath(QStringLiteral("PCL/Setup.ini"));

    const QString indie = readKey(instanceIni, QStringLiteral("VersionArgumentIndieV2"));
    const QString vanillaName = readKey(instanceIni, QStringLiteral("VersionVanillaName"));
    const QString forge = readKey(instanceIni, QStringLiteral("VersionForge"));
    const QString info = readKey(instanceIni, QStringLiteral("Info"));
    const QString folder = readKey(globalIni, QStringLiteral("LaunchFolderSelect"));

    if (indie.compare(QStringLiteral("True"), Qt::CaseInsensitive) != 0) {
        printLine(QStringLiteral("自测：版本隔离未写入（VersionArgumentIndieV2=%1）").arg(indie));
        ++failures;
    }
    if (vanillaName != QStringLiteral("1.20.1")) {
        printLine(QStringLiteral("自测：VersionVanillaName 不正确：%1").arg(vanillaName));
        ++failures;
    }
    if (forge != QStringLiteral("47.4.16")) {
        printLine(QStringLiteral("自测：VersionForge 不正确：%1").arg(forge));
        ++failures;
    }
    if (info.isEmpty()) {
        printLine(QStringLiteral("自测：Info 为空"));
        ++failures;
    }
    if (folder.isEmpty() || !folder.startsWith(QDir::toNativeSeparators(target))) {
        printLine(QStringLiteral("自测：LaunchFolderSelect 不正确：%1").arg(folder));
        ++failures;
    }

    if (verbose) {
        printLine(QStringLiteral("Info=%1").arg(info));
        printLine(QStringLiteral("LaunchFolderSelect=%1").arg(folder));
        printLine(QStringLiteral("实例配置：%1").arg(instanceIni));
    }

    if (failures == 0) {
        printLine(QStringLiteral("自测通过：打包/解包/ZIP 读写/中文文件名/PCL 配置全部正常"));
        return 0;
    }
    printLine(QStringLiteral("自测失败：%1 项检查未通过").arg(failures));
    return 15;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ValiaCraftLauncherCli"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MCPACKER_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("ValiaCraftLauncher 命令行工具：打包/解包 Minecraft 整合包（作者：neolxsky）"));
    parser.addHelpOption();

    const QCommandLineOption listOpt(QStringLiteral("list"), QStringLiteral("列出游戏目录中的版本"));
    const QCommandLineOption packOpt(QStringLiteral("pack"), QStringLiteral("打包指定版本"));
    const QCommandLineOption unpackOpt(QStringLiteral("unpack"), QStringLiteral("解包整合包"));
    const QCommandLineOption selftestOpt(QStringLiteral("selftest"), QStringLiteral("运行内置自测"));
    const QCommandLineOption gameOpt(QStringList() << QStringLiteral("game"),
                                     QStringLiteral(".minecraft 目录"), QStringLiteral("dir"));
    const QCommandLineOption versionOpt(QStringList() << QStringLiteral("version"),
                                        QStringLiteral("版本 id"), QStringLiteral("id"));
    const QCommandLineOption outOpt(QStringList() << QStringLiteral("out"),
                                    QStringLiteral("输出 .mcpk 路径"), QStringLiteral("file"));
    const QCommandLineOption javaOpt(QStringList() << QStringLiteral("java"),
                                     QStringLiteral("要一同打包的 java.exe"), QStringLiteral("path"));
    const QCommandLineOption runtimeNameOpt(QStringList() << QStringLiteral("runtime-name"),
                                            QStringLiteral("包内 Java 目录名"), QStringLiteral("name"));
    const QCommandLineOption librariesOpt(QStringLiteral("libraries"),
                                          QStringLiteral("同时打包 libraries"));
    const QCommandLineOption assetsOpt(QStringLiteral("assets"), QStringLiteral("同时打包 assets"));
    const QCommandLineOption noInstanceOpt(QStringLiteral("no-instance"),
                                           QStringLiteral("不打包非隔离版本的根目录实例文件"));
    const QCommandLineOption noPclOpt(QStringLiteral("no-pcl"),
                                      QStringLiteral("不写入/不打包 PCL 配置"));
    const QCommandLineOption packageOpt(QStringList() << QStringLiteral("package"),
                                        QStringLiteral("要解包的 .mcpk"), QStringLiteral("file"));
    const QCommandLineOption targetOpt(QStringList() << QStringLiteral("target"),
                                       QStringLiteral("目标 .minecraft 目录"), QStringLiteral("dir"));
    const QCommandLineOption policyOpt(QStringList() << QStringLiteral("policy"),
                                       QStringLiteral("冲突策略：skip/overwrite/rename"),
                                       QStringLiteral("mode"), QStringLiteral("skip"));
    const QCommandLineOption versionNameOpt(QStringList() << QStringLiteral("version-name"),
                                            QStringLiteral("解包时重命名版本"), QStringLiteral("name"));
    const QCommandLineOption verboseOpt(QStringList() << QStringLiteral("verbose"),
                                        QStringLiteral("输出详细日志"));

    parser.addOptions({listOpt, packOpt, unpackOpt, selftestOpt, gameOpt, versionOpt, outOpt,
                       javaOpt, runtimeNameOpt, librariesOpt, assetsOpt, noInstanceOpt, noPclOpt,
                       packageOpt, targetOpt, policyOpt, versionNameOpt, verboseOpt});
    parser.process(app);

    const bool verbose = parser.isSet(verboseOpt);

    if (parser.isSet(selftestOpt))
        return runSelfTest(verbose);

    if (parser.isSet(listOpt)) {
        const QString game = parser.value(gameOpt);
        if (game.isEmpty()) {
            printLine(QStringLiteral("请用 --game 指定 .minecraft 目录"));
            return 1;
        }
        return runList(game);
    }

    if (parser.isSet(packOpt)) {
        const QString game = parser.value(gameOpt);
        const QString version = parser.value(versionOpt);
        if (game.isEmpty() || version.isEmpty()) {
            printLine(QStringLiteral("打包需要 --game <目录> 与 --version <版本>"));
            return 1;
        }
        return runPack(game, version, parser.value(outOpt), parser.value(javaOpt),
                       parser.isSet(librariesOpt), parser.isSet(assetsOpt),
                       !parser.isSet(noInstanceOpt), !parser.isSet(noPclOpt),
                       parser.value(runtimeNameOpt));
    }

    if (parser.isSet(unpackOpt)) {
        const QString packagePath = parser.value(packageOpt);
        const QString target = parser.value(targetOpt);
        if (packagePath.isEmpty() || target.isEmpty()) {
            printLine(QStringLiteral("解包需要 --package <文件> 与 --target <目录>"));
            return 1;
        }
        ConflictPolicy policy = ConflictPolicy::Skip;
        const QString mode = parser.value(policyOpt).toLower();
        if (mode == QStringLiteral("overwrite"))
            policy = ConflictPolicy::Overwrite;
        else if (mode == QStringLiteral("rename"))
            policy = ConflictPolicy::Rename;
        return runUnpack(packagePath, target, policy, !parser.isSet(noPclOpt),
                         parser.value(versionNameOpt));
    }

    parser.showHelp(0);
    return 0;
}
