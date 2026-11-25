#include "ui/MainWindow.h"

#include "ui/LogPanel.h"
#include "ui/PackPage.h"
#include "ui/UnpackPage.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>

namespace mcpacker {

MainWindow::MainWindow(const QString &initialGameDir, const QString &initialPackage,
                       QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("ValiaCraftLauncher — Minecraft 整合包打包器"));
    resize(1080, 760);

    m_log = new LogPanel(this);

    m_packPage = new PackPage(m_log, this);
    m_unpackPage = new UnpackPage(m_log, this);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(m_packPage, QStringLiteral("打包"));
    m_tabs->addTab(m_unpackPage, QStringLiteral("解包"));

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->addWidget(m_tabs);
    m_splitter->addWidget(m_log);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({520, 180});

    setCentralWidget(m_splitter);
    buildMenus();
    statusBar()->showMessage(QStringLiteral("就绪"));

    if (!initialGameDir.isEmpty())
        m_packPage->setGameDir(initialGameDir);
    if (!initialPackage.isEmpty())
        m_unpackPage->setPackage(initialPackage);

    connect(m_packPage, &PackPage::busyChanged, this,
            [this](bool busy) { statusBar()->showMessage(busy ? QStringLiteral("正在处理…")
                                                              : QStringLiteral("就绪")); });
    connect(m_unpackPage, &UnpackPage::busyChanged, this,
            [this](bool busy) { statusBar()->showMessage(busy ? QStringLiteral("正在处理…")
                                                              : QStringLiteral("就绪")); });

    m_log->appendInfo(QStringLiteral("欢迎使用 ValiaCraftLauncher：支持打包/解包 Minecraft 版本与 Java 运行时，"
                                     "并自动写入 PCL（Plain Craft Launcher 2）配置。"));
    m_log->appendInfo(QStringLiteral("作者：neolxsky · Copyright © 2026 neolxsky"));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    auto *openGameDir = new QAction(QStringLiteral("选择游戏目录…"), this);
    openGameDir->setShortcut(QKeySequence::Open);
    connect(openGameDir, &QAction::triggered, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择 .minecraft 目录"), m_packPage->gameDir());
        if (dir.isEmpty())
            return;
        m_tabs->setCurrentWidget(m_packPage);
        m_packPage->setGameDir(dir);
    });
    fileMenu->addAction(openGameDir);

    auto *exitAction = new QAction(QStringLiteral("退出"), this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(exitAction);

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    auto *aboutAction = new QAction(QStringLiteral("关于 ValiaCraftLauncher"), this);
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this, QStringLiteral("关于 ValiaCraftLauncher"),
            QStringLiteral("<h3>ValiaCraftLauncher %1</h3>"
                           "<p><b>作者：neolxsky</b><br>"
                           "Copyright © 2026 neolxsky · GNU GPL v3.0 or later</p>"
                           "<p>基于 C++20 + Qt %2 的 Minecraft 整合包打包器。</p>"
                           "<ul>"
                           "<li>打包/解包指定版本，可选择携带 Java 运行时</li>"
                           "<li>自动写入 PCL2 配置（版本隔离 / Java 路径 / 启动文件夹）</li>"
                           "<li>自定义 .mcpk 包格式（标准 ZIP，兼容 7-Zip / WinRAR 解压）</li>"
                           "</ul>")
                .arg(QString::fromLatin1(MCPACKER_VERSION), QString::fromLatin1(qVersion())));
    });
    helpMenu->addAction(aboutAction);
    auto *aboutQtAction = new QAction(QStringLiteral("关于 Qt"), this);
    connect(aboutQtAction, &QAction::triggered, qApp, &QApplication::aboutQt);
    helpMenu->addAction(aboutQtAction);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    const bool busy = m_packPage->isBusy() || m_unpackPage->isBusy();
    if (busy) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("任务进行中"),
            QStringLiteral("当前仍有打包/解包任务正在执行，确定要退出吗？\n"
                           "中途退出可能导致压缩包不完整。"));
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_packPage->stopThread();
        m_unpackPage->stopThread();
    }
    event->accept();
}

} // namespace mcpacker
