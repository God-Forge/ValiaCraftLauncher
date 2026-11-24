#pragma once

#include "core/Common.h"
#include "core/Manifest.h"
#include "core/Unpacker.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QThread;
class QDragEnterEvent;
class QDropEvent;

namespace mcpacker {

class LogPanel;

/// 解包页：把 .mcpk 解压到目标 .minecraft，并写入 PCL 配置
class UnpackPage : public QWidget
{
    Q_OBJECT
public:
    explicit UnpackPage(LogPanel *log, QWidget *parent = nullptr);
    ~UnpackPage() override;

    bool isBusy() const;
    void stopThread();
    void setPackage(const QString &path);

signals:
    void busyChanged(bool busy);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onBrowsePackage();
    void onBrowseTarget();
    void onInspect();
    void onStartUnpack();
    void onUnpackFinished(const UnpackResult &result);

private:
    void buildUi();
    void loadSettings();
    void saveSettings() const;
    void setBusy(bool busy);

    LogPanel *m_log = nullptr;

    QLineEdit *m_packageEdit = nullptr;
    QPushButton *m_browsePackageBtn = nullptr;
    QLabel *m_packageLabel = nullptr;
    QLineEdit *m_targetEdit = nullptr;
    QPushButton *m_browseTargetBtn = nullptr;
    QComboBox *m_policyCombo = nullptr;
    QCheckBox *m_writePclCheck = nullptr;
    QLineEdit *m_versionNameEdit = nullptr;
    QPushButton *m_startBtn = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_progressLabel = nullptr;

    QThread *m_unpackThread = nullptr;
};

} // namespace mcpacker
