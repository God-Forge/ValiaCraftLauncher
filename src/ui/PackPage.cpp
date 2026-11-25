#include "ui/PackPage.h"

#include "core/MinecraftLayout.h"
#include "ui/LogPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace mcpacker {

PackPage::PackPage(LogPanel *log, QWidget *parent) : QWidget(parent), m_log(log)
{
    buildUi();
    loadSettings();

    if (m_gameDirEdit->text().isEmpty()) {
        const QStringList dirs = MinecraftLayout::detectGameDirs();
        if (!dirs.isEmpty())
            m_gameDirEdit->setText(dirs.first());
    }
    onRefreshVersions();

    // 打开界面时后台扫描一次 Java
    QTimer::singleShot(300, this, &PackPage::onScanJava);
}

PackPage::~PackPage() = default;

void PackPage::buildUi()
{
    // 用滚动区承载内容，避免小窗口下控件被挤出可视范围
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outerLayout->addWidget(scroll);

    auto *content = new QWidget(scroll);
    auto *mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);
    scroll->setWidget(content);

    // ---------------- 游戏目录与版本 ----------------
    auto *gameGroup = new QGroupBox(QStringLiteral("游戏与版本"), this);
    auto *gameLayout = new QGridLayout(gameGroup);
    gameLayout->setColumnStretch(1, 1);

    gameLayout->addWidget(new QLabel(QStringLiteral(".minecraft 目录："), gameGroup), 0, 0);
    m_gameDirEdit = new QLineEdit(gameGroup);
    m_gameDirEdit->setPlaceholderText(QStringLiteral("例如 C:\\Users\\<你>\\.minecraft"));
    gameLayout->addWidget(m_gameDirEdit, 0, 1);
    m_browseGameDirBtn = new QPushButton(QStringLiteral("浏览…"), gameGroup);
    gameLayout->addWidget(m_browseGameDirBtn, 0, 2);

    gameLayout->addWidget(new QLabel(QStringLiteral("要打包的版本："), gameGroup), 1, 0);
    auto *versionRow = new QHBoxLayout();
    m_versionCombo = new QComboBox(gameGroup);
    m_versionCombo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_versionCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_versionCombo->setMinimumContentsLength(18);
    versionRow->addWidget(m_versionCombo, 1);
    m_refreshVersionsBtn = new QPushButton(QStringLiteral("刷新"), gameGroup);
    versionRow->addWidget(m_refreshVersionsBtn);
    gameLayout->addLayout(versionRow, 1, 1, 1, 2);

    m_versionLabel = new QLabel(QStringLiteral("（请选择版本）"), gameGroup);
    m_versionLabel->setWordWrap(true);
    m_versionLabel->setStyleSheet(QStringLiteral("color:#9aa0a6;"));
    gameLayout->addWidget(m_versionLabel, 2, 0, 1, 3);

    mainLayout->addWidget(gameGroup);

    // ---------------- Java ----------------
    auto *javaGroup = new QGroupBox(QStringLiteral("Java 运行时"), this);
    auto *javaLayout = new QGridLayout(javaGroup);
    javaLayout->setColumnStretch(1, 1);

    javaLayout->addWidget(new QLabel(QStringLiteral("Java："), javaGroup), 0, 0);
    m_javaCombo = new QComboBox(javaGroup);
    m_javaCombo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_javaCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_javaCombo->setMinimumContentsLength(18);
    javaLayout->addWidget(m_javaCombo, 0, 1);
    m_scanJavaBtn = new QPushButton(QStringLiteral("重新扫描"), javaGroup);
    m_addJavaBtn = new QPushButton(QStringLiteral("手动添加…"), javaGroup);
    javaLayout->addWidget(m_scanJavaBtn, 0, 2);
    javaLayout->addWidget(m_addJavaBtn, 0, 3);

    m_includeJavaCheck = new QCheckBox(QStringLiteral("把该 Java 运行时一起打进包里"), javaGroup);
    m_includeJavaCheck->setChecked(true);
    javaLayout->addWidget(m_includeJavaCheck, 1, 0, 1, 4);

    javaLayout->addWidget(new QLabel(QStringLiteral("包内目录名："), javaGroup), 2, 0);
    m_runtimeNameEdit = new QLineEdit(javaGroup);
    m_runtimeNameEdit->setPlaceholderText(QStringLiteral("runtime/mcpacker-java-17"));
    m_runtimeNameEdit->setToolTip(QStringLiteral("解包后会放在目标 .minecraft/runtime/<名称>/ 下"));
    javaLayout->addWidget(m_runtimeNameEdit, 2, 1, 1, 3);

    mainLayout->addWidget(javaGroup);

    // ---------------- 打包内容 ----------------
    auto *contentGroup = new QGroupBox(QStringLiteral("打包内容"), this);
    auto *contentLayout = new QGridLayout(contentGroup);
    contentLayout->setColumnStretch(1, 1);

    auto *versionOnlyLabel = new QLabel(QStringLiteral("版本目录及其中的模组、配置、存档"), contentGroup);
    versionOnlyLabel->setStyleSheet(QStringLiteral("color:#9aa0a6;"));
    contentLayout->addWidget(new QLabel(QStringLiteral("必选："), contentGroup), 0, 0);
    contentLayout->addWidget(versionOnlyLabel, 0, 1, 1, 2);

    m_includeInstanceCheck = new QCheckBox(QStringLiteral("非隔离版本的根目录实例文件（mods / config / saves 等）"), contentGroup);
    m_includeInstanceCheck->setChecked(true);
    contentLayout->addWidget(m_includeInstanceCheck, 1, 1, 1, 2);

    m_includePclCheck = new QCheckBox(QStringLiteral("PCL 配置（PCL/Setup.ini，含版本隔离与 Java 设置）"), contentGroup);
    m_includePclCheck->setChecked(true);
    contentLayout->addWidget(m_includePclCheck, 2, 1, 1, 2);

    m_includeLibrariesCheck = new QCheckBox(QStringLiteral("libraries（游戏依赖库，约 100MB）"), contentGroup);
    contentLayout->addWidget(m_includeLibrariesCheck, 3, 1, 1, 2);

    m_includeAssetsCheck = new QCheckBox(QStringLiteral("assets（音效/语言/资源索引，体积较大）"), contentGroup);
    contentLayout->addWidget(m_includeAssetsCheck, 4, 1, 1, 2);

    mainLayout->addWidget(contentGroup);

    // ---------------- 输出 ----------------
    auto *outputGroup = new QGroupBox(QStringLiteral("输出"), this);
    auto *outputLayout = new QGridLayout(outputGroup);
    outputLayout->setColumnStretch(1, 1);
    outputLayout->addWidget(new QLabel(QStringLiteral("整合包文件："), outputGroup), 0, 0);
    m_outputEdit = new QLineEdit(outputGroup);
    m_outputEdit->setPlaceholderText(QStringLiteral("保存为 .mcpk（本质是 ZIP，可用 7-Zip 打开）"));
    outputLayout->addWidget(m_outputEdit, 0, 1);
    m_browseOutputBtn = new QPushButton(QStringLiteral("浏览…"), outputGroup);
    outputLayout->addWidget(m_browseOutputBtn, 0, 2);

    auto *actionRow = new QHBoxLayout();
    m_startBtn = new QPushButton(QStringLiteral("开始打包"), outputGroup);
    m_startBtn->setMinimumHeight(34);
    m_startBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background:#2d7ef7; color:white; font-weight:bold; border-radius:6px; padding:0 18px; }"
        "QPushButton:disabled { background:#3a3f4b; color:#8b8f98; }"));
    m_progressBar = new QProgressBar(outputGroup);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressLabel = new QLabel(QStringLiteral("等待开始"), outputGroup);
    m_progressLabel->setStyleSheet(QStringLiteral("color:#9aa0a6;"));
    actionRow->addWidget(m_startBtn);
    actionRow->addWidget(m_progressBar, 1);
    outputLayout->addLayout(actionRow, 1, 0, 1, 3);
    outputLayout->addWidget(m_progressLabel, 2, 0, 1, 3);

    mainLayout->addWidget(outputGroup);
    mainLayout->addStretch(1);

    // ---------------- 信号 ----------------
    connect(m_browseGameDirBtn, &QPushButton::clicked, this, &PackPage::onBrowseGameDir);
    connect(m_refreshVersionsBtn, &QPushButton::clicked, this, &PackPage::onRefreshVersions);
    connect(m_versionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PackPage::onVersionChanged);
    connect(m_gameDirEdit, &QLineEdit::editingFinished, this, &PackPage::onRefreshVersions);
    connect(m_scanJavaBtn, &QPushButton::clicked, this, &PackPage::onScanJava);
    connect(m_addJavaBtn, &QPushButton::clicked, this, &PackPage::onAddJava);
    connect(m_javaCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PackPage::onJavaChanged);
    connect(m_runtimeNameEdit, &QLineEdit::textEdited, this,
            [this] { m_autoRuntimeName = false; });
    connect(m_includeJavaCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_javaCombo->setEnabled(on);
        m_scanJavaBtn->setEnabled(on);
        m_addJavaBtn->setEnabled(on);
        m_runtimeNameEdit->setEnabled(on);
    });
    connect(m_browseOutputBtn, &QPushButton::clicked, this, &PackPage::onBrowseOutput);
    connect(m_startBtn, &QPushButton::clicked, this, &PackPage::onStartPack);
}

void PackPage::loadSettings()
{
    QSettings settings;
    m_gameDirEdit->setText(settings.value(QStringLiteral("pack/gameDir")).toString());
    m_outputEdit->setText(settings.value(QStringLiteral("pack/outputPath")).toString());
    m_includeJavaCheck->setChecked(settings.value(QStringLiteral("pack/includeJava"), true).toBool());
    m_includeLibrariesCheck->setChecked(
        settings.value(QStringLiteral("pack/includeLibraries"), false).toBool());
    m_includeAssetsCheck->setChecked(
        settings.value(QStringLiteral("pack/includeAssets"), false).toBool());
    m_includeInstanceCheck->setChecked(
        settings.value(QStringLiteral("pack/includeInstanceFiles"), true).toBool());
    m_includePclCheck->setChecked(
        settings.value(QStringLiteral("pack/includePclConfig"), true).toBool());
    m_runtimeNameEdit->setText(
        settings.value(QStringLiteral("pack/runtimeName")).toString());
}

void PackPage::saveSettings() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("pack/gameDir"), m_gameDirEdit->text().trimmed());
    settings.setValue(QStringLiteral("pack/outputPath"), m_outputEdit->text().trimmed());
    settings.setValue(QStringLiteral("pack/includeJava"), m_includeJavaCheck->isChecked());
    settings.setValue(QStringLiteral("pack/includeLibraries"),
                      m_includeLibrariesCheck->isChecked());
    settings.setValue(QStringLiteral("pack/includeAssets"), m_includeAssetsCheck->isChecked());
    settings.setValue(QStringLiteral("pack/includeInstanceFiles"),
                      m_includeInstanceCheck->isChecked());
    settings.setValue(QStringLiteral("pack/includePclConfig"), m_includePclCheck->isChecked());
    settings.setValue(QStringLiteral("pack/runtimeName"), m_runtimeNameEdit->text().trimmed());
    if (m_versionCombo->count() > 0) {
        const QString id = m_versionCombo->currentData().toString();
        if (!id.isEmpty())
            settings.setValue(QStringLiteral("pack/version"), id);
    }
}

QString PackPage::gameDir() const
{
    return QDir::cleanPath(QDir::fromNativeSeparators(m_gameDirEdit->text().trimmed()));
}

void PackPage::setGameDir(const QString &dir)
{
    if (dir.isEmpty())
        return;
    const QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(dir));
    if (cleaned == gameDir())
        return;
    m_gameDirEdit->setText(QDir::toNativeSeparators(dir));
    onRefreshVersions();
}

bool PackPage::isBusy() const
{
    return m_packThread != nullptr;
}

void PackPage::onBrowseGameDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择 .minecraft 目录"), gameDir());
    if (dir.isEmpty())
        return;
    setGameDir(dir);
}

void PackPage::onRefreshVersions()
{
    const QString dir = gameDir();
    if (dir.isEmpty()) {
        m_versions.clear();
        applyVersions({});
        m_versionLabel->setText(QStringLiteral("请先选择 .minecraft 目录"));
        return;
    }
    if (!QFileInfo::exists(dir)) {
        m_versionLabel->setText(QStringLiteral("目录不存在：%1").arg(QDir::toNativeSeparators(dir)));
        return;
    }

    const QVector<VersionInfo> versions = MinecraftLayout::scanVersions(dir);
    applyVersions(versions);
    if (versions.isEmpty()) {
        m_log->appendWarning(QStringLiteral("在 %1 下没有找到任何版本")
                                 .arg(QDir::toNativeSeparators(dir)));
    } else {
        m_log->appendInfo(QStringLiteral("已加载 %1 个版本").arg(versions.size()));
    }
    saveSettings();
}

void PackPage::applyVersions(const QVector<VersionInfo> &versions)
{
    m_versions = versions;
    const QString previous = m_versionCombo->currentData().toString();

    QSignalBlocker blocker(m_versionCombo);
    m_versionCombo->clear();
    for (const VersionInfo &version : versions)
        m_versionCombo->addItem(version.describe(), version.id);

    int index = m_versionCombo->findData(previous);
    if (index < 0)
        index = m_versionCombo->findData(QSettings().value(QStringLiteral("pack/version")).toString());
    if (index >= 0)
        m_versionCombo->setCurrentIndex(index);
    else if (m_versionCombo->count() > 0)
        m_versionCombo->setCurrentIndex(0);
    blocker.unblock();

    onVersionChanged();
}

VersionInfo PackPage::currentVersion() const
{
    const QString id = m_versionCombo->currentData().toString();
    for (const VersionInfo &version : m_versions) {
        if (version.id == id)
            return version;
    }
    return VersionInfo();
}

JavaInfo PackPage::currentJava() const
{
    const QString exe = m_javaCombo->currentData().toString();
    for (const JavaInfo &java : m_javas) {
        if (java.javaExe == exe)
            return java;
    }
    return JavaInfo();
}

void PackPage::onVersionChanged()
{
    updateVersionSummary();
    const VersionInfo version = currentVersion();
    if (!version.id.isEmpty())
        updateOutputSuggestion(version);

    if (!m_javas.isEmpty() && version.javaMajor > 0) {
        const JavaInfo best = JavaScanner::pickBest(m_javas, version.javaMajor);
        const int index = m_javaCombo->findData(best.javaExe);
        if (index >= 0 && index != m_javaCombo->currentIndex()) {
            QSignalBlocker blocker(m_javaCombo);
            m_javaCombo->setCurrentIndex(index);
            blocker.unblock();
            onJavaChanged();
        }
    }
}

void PackPage::onJavaChanged()
{
    if (!m_autoRuntimeName)
        return;
    const JavaInfo java = currentJava();
    if (java.major > 0)
        m_runtimeNameEdit->setText(QStringLiteral("mcpacker-java-%1").arg(java.major));
}

void PackPage::updateVersionSummary()
{
    const VersionInfo version = currentVersion();
    if (version.id.isEmpty()) {
        m_versionLabel->setText(QStringLiteral("（没有可用版本）"));
        return;
    }

    const QString loaderText = version.loader.isEmpty()
                                   ? QStringLiteral("Vanilla")
                                   : (version.loaderVersion.isEmpty()
                                          ? version.loader
                                          : QStringLiteral("%1 %2").arg(version.loader,
                                                                        version.loaderVersion));
    m_versionLabel->setText(
        QStringLiteral("MC %1  ·  %2  ·  建议 Java %3  ·  PCL 版本隔离：%4\n目录：%5")
            .arg(version.mcVersion.isEmpty() ? version.id : version.mcVersion, loaderText,
                 version.javaMajor > 0 ? QString::number(version.javaMajor) : QStringLiteral("未知"),
                 version.isolated ? QStringLiteral("开启") : QStringLiteral("关闭"),
                 QDir::toNativeSeparators(version.versionDir)));
}

void PackPage::updateOutputSuggestion(const VersionInfo &version)
{
    if (!m_outputEdit->text().trimmed().isEmpty())
        return;
    const QString gameDir = this->gameDir();
    if (gameDir.isEmpty() || version.id.isEmpty())
        return;
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm"));
    const QString suggestion = QDir(QFileInfo(gameDir).absolutePath())
                                   .absoluteFilePath(QStringLiteral("%1-%2.mcpk")
                                                         .arg(version.id, stamp));
    m_outputEdit->setText(QDir::toNativeSeparators(suggestion));
}

void PackPage::onScanJava()
{
    if (m_javaThread)
        return;
    setJavaScanning(true);
    const QString dir = gameDir();
    auto *thread = QThread::create([this, dir] {
        const JavaScanner::ScanResult result = JavaScanner::scan(dir);
        QMetaObject::invokeMethod(
            this,
            [this, result] {
                setJavaScanning(false);
                applyJavas(result.javas);
                for (const QString &note : result.notes)
                    m_log->appendWarning(note);
                if (result.javas.isEmpty())
                    m_log->appendWarning(QStringLiteral("没有找到可用的 Java，可点击「手动添加…」"
                                                        "直接指定 java.exe"));
                else
                    m_log->appendInfo(QStringLiteral("Java 扫描完成：发现 %1 个 Java 运行时")
                                          .arg(result.javas.size()));
            },
            Qt::QueuedConnection);
    });
    m_javaThread = thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        thread->deleteLater();
        if (m_javaThread == thread)
            m_javaThread = nullptr;
    });
    thread->start();
}

void PackPage::setJavaScanning(bool scanning)
{
    m_scanJavaBtn->setEnabled(!scanning);
    m_addJavaBtn->setEnabled(!scanning);
    m_javaCombo->setEnabled(!scanning && m_includeJavaCheck->isChecked());
    if (scanning)
        m_scanJavaBtn->setText(QStringLiteral("扫描中…"));
    else
        m_scanJavaBtn->setText(QStringLiteral("重新扫描"));
}

void PackPage::applyJavas(const QVector<JavaInfo> &javas)
{
    const QString previous = m_javaCombo->currentData().toString();
    m_javas = javas;

    QSignalBlocker blocker(m_javaCombo);
    m_javaCombo->clear();
    for (const JavaInfo &java : javas) {
        m_javaCombo->addItem(java.describe(), java.javaExe);
        m_javaCombo->setItemData(m_javaCombo->count() - 1,
                                 QDir::toNativeSeparators(java.javaExe), Qt::ToolTipRole);
    }
    int index = m_javaCombo->findData(previous);
    if (index < 0 && !javas.isEmpty()) {
        const VersionInfo version = currentVersion();
        const JavaInfo best = JavaScanner::pickBest(javas, version.javaMajor);
        index = m_javaCombo->findData(best.javaExe);
    }
    if (index >= 0)
        m_javaCombo->setCurrentIndex(index);
    blocker.unblock();

    onJavaChanged();
}

void PackPage::onAddJava()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 java.exe"), QString(),
        QStringLiteral("Java 可执行文件 (java.exe);;所有文件 (*.*)"));
    if (file.isEmpty())
        return;

    JavaInfo info;
    QString error;
    if (!JavaScanner::probe(file, &info, &error)) {
        QMessageBox::warning(this, QStringLiteral("无法识别 Java"), error);
        return;
    }

    QVector<JavaInfo> javas = m_javas;
    bool exists = false;
    for (const JavaInfo &java : javas) {
        if (java.javaExe.compare(info.javaExe, Qt::CaseInsensitive) == 0) {
            exists = true;
            break;
        }
    }
    if (!exists)
        javas.append(info);

    applyJavas(javas);
    const int index = m_javaCombo->findData(info.javaExe);
    if (index >= 0)
        m_javaCombo->setCurrentIndex(index);

    m_log->appendInfo(QStringLiteral("已添加 Java：%1").arg(info.describe()));
}

void PackPage::onBrowseOutput()
{
    QString start = m_outputEdit->text().trimmed();
    if (start.isEmpty()) {
        const VersionInfo version = currentVersion();
        start = QDir(QFileInfo(gameDir()).absolutePath())
                    .absoluteFilePath((version.id.isEmpty() ? QStringLiteral("minecraft")
                                                            : version.id)
                                      + QStringLiteral(".mcpk"));
    }
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存整合包"), start,
                                                QStringLiteral("ValiaCraftLauncher 整合包 (*.mcpk);;"
                                                               "ZIP 压缩包 (*.zip);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".mcpk"), Qt::CaseInsensitive)
        && !path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
        path += QStringLiteral(".mcpk");
    m_outputEdit->setText(QDir::toNativeSeparators(path));
}

void PackPage::onStartPack()
{
    if (isBusy())
        return;

    const QString dir = gameDir();
    if (dir.isEmpty() || !QFileInfo::exists(dir)) {
        QMessageBox::warning(this, QStringLiteral("无法打包"),
                             QStringLiteral("请先选择一个存在的 .minecraft 目录。"));
        return;
    }

    const VersionInfo version = currentVersion();
    if (version.id.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无法打包"), QStringLiteral("请先选择要打包的版本。"));
        return;
    }

    if (m_includeJavaCheck->isChecked() && m_javas.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无法打包"),
                             QStringLiteral("还没有可用的 Java。请点击「重新扫描」或「手动添加…」。"));
        return;
    }

    QString output = m_outputEdit->text().trimmed();
    if (output.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无法打包"), QStringLiteral("请先指定输出的整合包文件。"));
        return;
    }
    output = QDir::cleanPath(QDir::fromNativeSeparators(output));
    if (QFileInfo::exists(output)) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("文件已存在"),
            QStringLiteral("目标文件已存在：\n%1\n\n要覆盖它吗？")
                .arg(QDir::toNativeSeparators(output)));
        if (answer != QMessageBox::Yes)
            return;
    }

    saveSettings();

    PackRequest request;
    request.gameDir = dir;
    request.version = version;
    request.includeJava = m_includeJavaCheck->isChecked();
    request.java = currentJava();
    request.javaRuntimeName = m_runtimeNameEdit->text().trimmed();
    request.includeLibraries = m_includeLibrariesCheck->isChecked();
    request.includeAssets = m_includeAssetsCheck->isChecked();
    request.includeInstanceFiles = m_includeInstanceCheck->isChecked();
    request.includePclConfig = m_includePclCheck->isChecked();
    request.outputPath = output;

    setBusy(true);
    m_progressBar->setValue(0);
    m_progressLabel->setText(QStringLiteral("准备中…"));
    m_log->appendLine(QStringLiteral("—— 开始打包「%1」——").arg(version.id));

    auto *thread = QThread::create([this, request] {
        const auto progress = [this](int percent, const QString &message) {
            QMetaObject::invokeMethod(
                this,
                [this, percent, message] {
                    m_progressBar->setValue(percent);
                    m_progressLabel->setText(message);
                },
                Qt::QueuedConnection);
        };
        const auto log = [this](const QString &message) {
            QMetaObject::invokeMethod(this, [this, message] { m_log->appendLine(message); },
                                      Qt::QueuedConnection);
        };
        Packer packer(progress, log);
        const PackResult result = packer.run(request);
        QMetaObject::invokeMethod(this, [this, result] { onPackFinished(result); },
                                  Qt::QueuedConnection);
    });
    m_packThread = thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        thread->deleteLater();
        if (m_packThread == thread)
            m_packThread = nullptr;
        setBusy(false);
    });
    thread->start();
}

void PackPage::onPackFinished(const PackResult &result)
{
    if (result.ok) {
        m_progressBar->setValue(100);
        m_progressLabel->setText(QStringLiteral("打包完成"));
        m_log->appendSuccess(QStringLiteral("生成：%1（%2 个文件，原始大小 %3）")
                                 .arg(QDir::toNativeSeparators(result.outputPath))
                                 .arg(result.fileCount)
                                 .arg(formatBytes(result.totalBytes)));
        for (const QString &warning : result.warnings)
            m_log->appendWarning(warning);

        QMessageBox box(QMessageBox::Information, QStringLiteral("打包完成"),
                        QStringLiteral("整合包已生成：\n%1\n\n共 %2 个文件，原始大小 %3。\n"
                                       "该文件为 ZIP 格式，可用 7-Zip 查看。")
                            .arg(QDir::toNativeSeparators(result.outputPath))
                            .arg(result.fileCount)
                            .arg(formatBytes(result.totalBytes)),
                        QMessageBox::Ok, this);
        box.exec();
    } else {
        m_progressBar->setValue(0);
        m_progressLabel->setText(QStringLiteral("打包失败"));
        m_log->appendError(result.error);
        QMessageBox::critical(this, QStringLiteral("打包失败"), result.error);
    }
}

void PackPage::setBusy(bool busy)
{
    const bool enable = !busy;
    m_gameDirEdit->setEnabled(enable);
    m_browseGameDirBtn->setEnabled(enable);
    m_versionCombo->setEnabled(enable);
    m_refreshVersionsBtn->setEnabled(enable);
    m_scanJavaBtn->setEnabled(enable);
    m_addJavaBtn->setEnabled(enable);
    m_includeJavaCheck->setEnabled(enable);
    m_runtimeNameEdit->setEnabled(enable && m_includeJavaCheck->isChecked());
    m_includeLibrariesCheck->setEnabled(enable);
    m_includeAssetsCheck->setEnabled(enable);
    m_includeInstanceCheck->setEnabled(enable);
    m_includePclCheck->setEnabled(enable);
    m_outputEdit->setEnabled(enable);
    m_browseOutputBtn->setEnabled(enable);
    m_startBtn->setEnabled(enable);
    m_startBtn->setText(busy ? QStringLiteral("正在打包…") : QStringLiteral("开始打包"));
    emit busyChanged(busy);
}

void PackPage::stopThread()
{
    if (m_packThread && m_packThread->isRunning()) {
        m_packThread->requestInterruption();
        if (!m_packThread->wait(8000)) {
            m_packThread->terminate();
            m_packThread->wait(1000);
        }
    }
    if (m_javaThread && m_javaThread->isRunning()) {
        m_javaThread->requestInterruption();
        if (!m_javaThread->wait(3000)) {
            m_javaThread->terminate();
            m_javaThread->wait(1000);
        }
    }
}

} // namespace mcpacker
