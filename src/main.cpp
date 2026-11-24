// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 neolxsky
//
// mcPacker — Minecraft 整合包打包器（GUI 入口）

#include "ui/MainWindow.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QStringList>
#include <QStyleFactory>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("mcPacker"));
    QApplication::setApplicationName(QStringLiteral("mcPacker"));
    QApplication::setApplicationVersion(QStringLiteral(MCPACKER_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app_icon.png")));

    if (QStyleFactory::keys().contains(QStringLiteral("Fusion")))
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QFont font = QApplication::font();
    font.setPointSizeF(qMax(9.0, font.pointSizeF()));
    QApplication::setFont(font);

    // 简易启动参数：--game <目录> 指定 .minecraft；--unpack <文件> 打开解包页；
    // --screenshot <png> 为调试用，截图后自动退出
    QString gameDir;
    QString packageFile;
    QString screenshotPath;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("--game") && i + 1 < args.size())
            gameDir = args.at(++i);
        else if (args.at(i) == QStringLiteral("--unpack") && i + 1 < args.size())
            packageFile = args.at(++i);
        else if (args.at(i) == QStringLiteral("--screenshot") && i + 1 < args.size())
            screenshotPath = args.at(++i);
    }

    mcpacker::MainWindow window(gameDir, packageFile);
    window.show();

    if (!screenshotPath.isEmpty()) {
        // 离屏渲染时没有系统字体库，手动注册中文字体（仅截图调试用）
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyhbd.ttc"));
        QTimer::singleShot(4200, &window, [&window, screenshotPath] {
            window.grab().save(screenshotPath);
            QCoreApplication::quit();
        });
    }
    return app.exec();
}
