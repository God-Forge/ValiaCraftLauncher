#include "ui/UnpackPage.h"

#include "core/MinecraftLayout.h"
#include "ui/LogPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

namespace mcpacker {

UnpackPage::UnpackPage(LogPanel *log, QWidget *parent) : QWidget(parent), m_log(log)
{
    buildUi();
    setAcceptDrops(true);
    loadSettings();

    if (m_targetEdit->text().trimmed().isEmpty()) {
        const QStringList dirs = MinecraftLayout::detectGameDirs();
        if (!dirs.isEmpty())
            m_targetEdit->setText(QDir::toNativeSeparators(dirs.first()));
    }
    if (!m_packageEdit->text().trimmed().isEmpty())
        onInspect();
}

UnpackPage::~UnpackPage() = default;

void UnpackPage::buildUi()
{
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

    // ---------------- 整合包 ----------------
    auto *packageGroup = new QGroupBox(QStringLiteral("整合包"), this);
    auto *packageLayout = new QGridLayout(packageGroup);
    packageLayout->setColumnStretch(1, 1);

    packageLayout->addWidget(new QLabel(QStringLiteral("整合包文件："), packageGroup), 0, 0);
    m_packageEdit = new QLineEdit(packageGroup);
    m_packageEdit->setPlaceholderText(QStringLiteral("可以直接把 .mcpk / .zip 拖进窗口"));
    packageLayout->addWidget(m_packageEdit, 0, 1);
    m_browsePackageBtn = new QPushButton(QStringLiteral("浏览…"), packageGroup);
    packageLayout->addWidget(m_browsePackageBtn, 0, 2);

    m_packageLabel = new QLabel(QStringLiteral("（未选择整合包）"), packageGroup);
    m_packageLabel->setWordWrap(true);
    m_packageLabel->setStyleSheet(QStringLiteral("color:#9aa0a6;"));
    packageLayout->addWidget(m_packageLabel, 1, 0, 1, 3);

    mainLayout->addWidget(packageGroup);

    // ---------------- 目标与选项 ----------------
    auto *targetGroup = new QGroupBox(QStringLiteral("解包到"), this);
    auto *targetLayout = new QGridLayout(targetGroup);
    targetLayout->setColumnStretch(1, 1);

    targetLayout->addWidget(new QLabel(QStringLiteral("目标目录："), targetGroup), 0, 0);
    m_targetEdit = new QLineEdit(targetGroup);
    m_targetEdit->setPlaceholderText(QStringLiteral("目标 .minecraft 目录（不存在会自动创建）"));
    targetLayout->addWidget(m_targetEdit, 0, 1);
    m_browseTargetBtn = new QPushButton(QStringLiteral("浏览…"), targetGroup);
    targetLayout->addWidget(m_browseTargetBtn, 0, 2);

    targetLayout->addWidget(new QLabel(QStringLiteral("文件冲突："), targetGroup), 1, 0);
    m_policyCombo = new QComboBox(targetGroup);
    m_policyCombo->addItem(conflictPolicyName(ConflictPolicy::Skip),
                           int(ConflictPolicy::Skip));
    m_policyCombo->addItem(conflictPolicyName(ConflictPolicy::Overwrite),
                           int(ConflictPolicy::Overwrite));
    m_policyCombo->addItem(conflictPolicyName(ConflictPolicy::Rename),
                           int(ConflictPolicy::Rename));
    targetLayout->addWidget(m_policyCombo, 1, 1, 1, 2);

    m_writePclCheck = new QCheckBox(
        QStringLiteral("写入 PCL 配置（版本隔离 / Java 路径 / 启动文件夹）"), targetGroup);
    m_writePclCheck->setChecked(true);
    targetLayout->addWidget(m_writePclCheck, 2, 0, 1, 3);

    targetLayout->addWidget(new QLabel(QStringLiteral("版本名（可选）："), targetGroup), 3, 0);
    m_versionNameEdit = new QLineEdit(targetGroup);
    m_versionNameEdit->setPlaceholderText(
        QStringLiteral("留空则使用包内版本名；填写可避免覆盖同名版本"));
    targetLayout->addWidget(m_versionNameEdit, 3, 1, 1, 2);

    mainLayout->addWidget(targetGroup);

    // ---------------- 动作 ----------------
    auto *actionGroup = new QGroupBox(QStringLiteral("开始"), this);
    auto *actionLayout = new QGridLayout(actionGroup);
    actionLayout->setColumnStretch(1, 1);

    auto *actionRow = new QHBoxLayout();
    m_startBtn = new QPushButton(QStringLiteral("开始解包"), actionGroup);
    m_startBtn->setMinimumHeight(34);
    m_startBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background:#2d7ef7; color:white; font-weight:bold; border-radius:6px; padding:0 18px; }"
        "QPushButton:disabled { background:#3a3f4b; color:#8b8f98; }"));
    m_progressBar = new QProgressBar(actionGroup);
    m_progressBar->setValue(0);
    m_progressLabel = new QLabel(QStringLiteral("等待开始"), actionGroup);
    m_progressLabel->setStyleSheet(QStringLiteral("color:#9aa0a6;"));
    actionRow->addWidget(m_startBtn);
    actionRow->addWidget(m_progressBar, 1);
    actionLayout->addLayout(actionRow, 0, 0, 1, 3);
    actionLayout->addWidget(m_progressLabel, 1, 0, 1, 3);

    mainLayout->addWidget(actionGroup);
    mainLayout->addStretch(1);

    connect(m_browsePackageBtn, &QPushButton::clicked, this, &UnpackPage::onBrowsePackage);
    connect(m_browseTargetBtn, &QPushButton::clicked, this, &UnpackPage::onBrowseTarget);
    connect(m_packageEdit, &QLineEdit::editingFinished, this, &UnpackPage::onInspect);
    connect(m_startBtn, &QPushButton::clicked, this, &UnpackPage::onStartUnpack);
}

void UnpackPage::loadSettings()
{
    QSettings settings;
    m_packageEdit->setText(settings.value(QStringLiteral("unpack/packagePath")).toString());
    m_targetEdit->setText(settings.value(QStringLiteral("unpack/targetGameDir")).toString());
    const int policy = settings.value(QStringLiteral("unpack/policy"),
                                      int(ConflictPolicy::Skip)).toInt();
    const int index = m_policyCombo->findData(policy);
    if (index >= 0)
        m_policyCombo->setCurrentIndex(index);
    m_writePclCheck->setChecked(settings.value(QStringLiteral("unpack/writePcl"), true).toBool());
}

void UnpackPage::saveSettings() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("unpack/packagePath"), m_packageEdit->text().trimmed());
    settings.setValue(QStringLiteral("unpack/targetGameDir"), m_targetEdit->text().trimmed());
    settings.setValue(QStringLiteral("unpack/policy"), m_policyCombo->currentData().toInt());
    settings.setValue(QStringLiteral("unpack/writePcl"), m_writePclCheck->isChecked());
}

bool UnpackPage::isBusy() const
{
    return m_unpackThread != nullptr;
}

void UnpackPage::setPackage(const QString &path)
{
    m_packageEdit->setText(QDir::toNativeSeparators(path));
    onInspect();
}

void UnpackPage::onBrowsePackage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择整合包"), m_packageEdit->text().trimmed(),
        QStringLiteral("mcPacker 整合包 (*.mcpk);;ZIP 压缩包 (*.zip);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    setPackage(path);
}

void UnpackPage::onBrowseTarget()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择目标 .minecraft 目录"), m_targetEdit->text().trimmed());
    if (dir.isEmpty())
        return;
    m_targetEdit->setText(QDir::toNativeSeparators(dir));
}

void UnpackPage::onInspect()
{
    const QString path = m_packageEdit->text().trimmed();
    if (path.isEmpty()) {
        m_packageLabel->setText(QStringLiteral("（未选择整合包）"));
        return;
    }
    if (!QFileInfo::exists(path)) {
        m_packageLabel->setText(QStringLiteral("文件不存在：%1").arg(QDir::toNativeSeparators(path)));
        return;
    }

    PackManifest manifest;
    QString error;
    if (!Unpacker::inspect(path, &manifest, &error)) {
        m_packageLabel->setText(QStringLiteral("%1\n（将按普通 ZIP 解包）").arg(error));
        return;
    }

    QStringList lines;
    lines << QStringLiteral("版本：%1 · %2 %3 · MC %4")
                 .arg(manifest.version.id, manifest.version.loader,
                      manifest.version.loaderVersion, manifest.version.mcVersion);
    if (manifest.hasJava()) {
        lines << QStringLiteral("内嵌 Java：Java %1（%2）→ 解包后位于 %3/bin/java.exe")
                     .arg(manifest.java.major)
                     .arg(manifest.java.fullVersion.isEmpty() ? QStringLiteral("未知版本")
                                                              : manifest.java.fullVersion)
                     .arg(manifest.java.relativePath);
    } else {
        lines << QStringLiteral("内嵌 Java：无（解包后由 PCL 自动选择）");
    }
    lines << QStringLiteral("打包内容：libraries %1 · assets %2 · 实例文件 %3 · PCL 配置 %4")
                 .arg(manifest.options.includeLibraries ? QStringLiteral("✓") : QStringLiteral("✗"),
                      manifest.options.includeAssets ? QStringLiteral("✓") : QStringLiteral("✗"),
                      manifest.options.includeInstanceFiles ? QStringLiteral("✓")
                                                            : QStringLiteral("✗"),
                      manifest.options.includePclConfig ? QStringLiteral("✓")
                                                        : QStringLiteral("✗"));
    lines << QStringLiteral("创建时间：%1 · 文件数：%2 · 原始大小：%3")
                 .arg(manifest.createdAt.isEmpty() ? QStringLiteral("未知") : manifest.createdAt)
                 .arg(manifest.fileCount)
                 .arg(formatBytes(manifest.totalBytes));
    m_packageLabel->setText(lines.join(QLatin1Char('\n')));
}

void UnpackPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls())
        return;
    for (const QUrl &url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(QStringLiteral(".mcpk"), Qt::CaseInsensitive)
            || path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
            event->acceptProposedAction();
            return;
        }
    }
}

void UnpackPage::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(QStringLiteral(".mcpk"), Qt::CaseInsensitive)
            || path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
            setPackage(path);
            event->acceptProposedAction();
            return;
        }
    }
}

void UnpackPage::onStartUnpack()
{
    if (isBusy())
        return;

    const QString packagePath = m_packageEdit->text().trimmed();
    if (packagePath.isEmpty() || !QFileInfo::exists(packagePath)) {
        QMessageBox::warning(this, QStringLiteral("无法解包"),
                             QStringLiteral("请先选择存在的整合包文件。"));
        return;
    }
    const QString target = m_targetEdit->text().trimmed();
    if (target.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无法解包"),
                             QStringLiteral("请先选择目标 .minecraft 目录。"));
        return;
    }

    const QString resolved = QDir::cleanPath(QDir::fromNativeSeparators(target));
    if (!QFileInfo::exists(resolved)) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("目录不存在"),
            QStringLiteral("目标目录不存在：\n%1\n\n要创建它吗？").arg(QDir::toNativeSeparators(resolved)));
        if (answer != QMessageBox::Yes)
            return;
    }

    saveSettings();

    UnpackRequest request;
    request.packagePath = packagePath;
    request.targetGameDir = resolved;
    request.policy = ConflictPolicy(m_policyCombo->currentData().toInt());
    request.writePclConfig = m_writePclCheck->isChecked();
    request.versionNameOverride = m_versionNameEdit->text().trimmed();

    setBusy(true);
    m_progressBar->setValue(0);
    m_progressLabel->setText(QStringLiteral("准备中…"));
    m_log->appendLine(QStringLiteral("—— 开始解包：%1 ——")
                          .arg(QDir::toNativeSeparators(packagePath)));

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
        Unpacker unpacker(progress, log);
        const UnpackResult result = unpacker.run(request);
        QMetaObject::invokeMethod(this, [this, result] { onUnpackFinished(result); },
                                  Qt::QueuedConnection);
    });
    m_unpackThread = thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        thread->deleteLater();
        if (m_unpackThread == thread)
            m_unpackThread = nullptr;
        setBusy(false);
    });
    thread->start();
}

void UnpackPage::onUnpackFinished(const UnpackResult &result)
{
    if (result.ok) {
        m_progressBar->setValue(100);
        m_progressLabel->setText(QStringLiteral("解包完成"));
        m_log->appendSuccess(QStringLiteral("版本「%1」已解包到 %2")
                                 .arg(result.versionId, QDir::toNativeSeparators(result.gameDir)));
        m_log->appendLine(QStringLiteral("写入 %1 个文件，跳过 %2 个，失败 %3 个")
                              .arg(result.writtenFiles.size())
                              .arg(result.skippedFiles.size())
                              .arg(result.failedFiles.size()));
        for (const QString &message : result.messages)
            m_log->appendLine(message);
        for (const QString &failed : result.failedFiles)
            m_log->appendWarning(failed);

        QString text = QStringLiteral("解包完成。\n\n版本：%1\n位置：%2\n内嵌 Java：%3")
                           .arg(result.versionId, QDir::toNativeSeparators(result.gameDir),
                                result.javaExe.isEmpty()
                                    ? QStringLiteral("无（由 PCL 自动选择）")
                                    : QDir::toNativeSeparators(result.javaExe));
        if (!result.failedFiles.isEmpty())
            text += QStringLiteral("\n\n有 %1 个文件失败，详见日志。").arg(result.failedFiles.size());
        QMessageBox box(QMessageBox::Information, QStringLiteral("解包完成"), text,
                        QMessageBox::Ok, this);
        box.exec();
    } else {
        m_progressBar->setValue(0);
        m_progressLabel->setText(QStringLiteral("解包失败"));
        m_log->appendError(result.error);
        QMessageBox::critical(this, QStringLiteral("解包失败"), result.error);
    }
}

void UnpackPage::setBusy(bool busy)
{
    const bool enable = !busy;
    m_packageEdit->setEnabled(enable);
    m_browsePackageBtn->setEnabled(enable);
    m_targetEdit->setEnabled(enable);
    m_browseTargetBtn->setEnabled(enable);
    m_policyCombo->setEnabled(enable);
    m_writePclCheck->setEnabled(enable);
    m_versionNameEdit->setEnabled(enable);
    m_startBtn->setEnabled(enable);
    m_startBtn->setText(busy ? QStringLiteral("正在解包…") : QStringLiteral("开始解包"));
    emit busyChanged(busy);
}

void UnpackPage::stopThread()
{
    if (m_unpackThread && m_unpackThread->isRunning()) {
        m_unpackThread->requestInterruption();
        if (!m_unpackThread->wait(8000)) {
            m_unpackThread->terminate();
            m_unpackThread->wait(1000);
        }
    }
}

} // namespace mcpacker
