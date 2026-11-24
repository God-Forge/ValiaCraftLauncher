#pragma once

#include "core/Common.h"
#include "core/JavaScanner.h"
#include "core/Packer.h"

#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QThread;

namespace mcpacker {

class LogPanel;

/// 打包页：选择游戏目录 / 版本 / Java，生成 .mcpk
class PackPage : public QWidget
{
    Q_OBJECT
public:
    explicit PackPage(LogPanel *log, QWidget *parent = nullptr);
    ~PackPage() override;

    bool isBusy() const;
    QString gameDir() const;
    void setGameDir(const QString &dir);
    void stopThread();

signals:
    void busyChanged(bool busy);

private slots:
    void onBrowseGameDir();
    void onRefreshVersions();
    void onVersionChanged();
    void onJavaChanged();
    void onScanJava();
    void onAddJava();
    void onBrowseOutput();
    void onStartPack();
    void onPackFinished(const PackResult &result);

private:
    void buildUi();
    void loadSettings();
    void saveSettings() const;
    void applyVersions(const QVector<VersionInfo> &versions);
    void applyJavas(const QVector<JavaInfo> &javas);
    void updateVersionSummary();
    void updateOutputSuggestion(const VersionInfo &version);
    void setBusy(bool busy);
    void setJavaScanning(bool scanning);
    VersionInfo currentVersion() const;
    JavaInfo currentJava() const;
    void appendLine(const QString &text);

    LogPanel *m_log = nullptr;

    QLineEdit *m_gameDirEdit = nullptr;
    QPushButton *m_browseGameDirBtn = nullptr;
    QComboBox *m_versionCombo = nullptr;
    QPushButton *m_refreshVersionsBtn = nullptr;
    QLabel *m_versionLabel = nullptr;
    QComboBox *m_javaCombo = nullptr;
    QPushButton *m_scanJavaBtn = nullptr;
    QPushButton *m_addJavaBtn = nullptr;
    QCheckBox *m_includeJavaCheck = nullptr;
    QLineEdit *m_runtimeNameEdit = nullptr;
    QCheckBox *m_includeLibrariesCheck = nullptr;
    QCheckBox *m_includeAssetsCheck = nullptr;
    QCheckBox *m_includeInstanceCheck = nullptr;
    QCheckBox *m_includePclCheck = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QPushButton *m_browseOutputBtn = nullptr;
    QPushButton *m_startBtn = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_progressLabel = nullptr;

    QVector<VersionInfo> m_versions;
    QVector<JavaInfo> m_javas;

    QThread *m_packThread = nullptr;
    QThread *m_javaThread = nullptr;
    bool m_autoRuntimeName = true;
};

} // namespace mcpacker
