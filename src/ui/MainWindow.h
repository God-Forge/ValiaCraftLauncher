#pragma once

#include <QMainWindow>

class QSplitter;
class QTabWidget;

namespace mcpacker {

class LogPanel;
class PackPage;
class UnpackPage;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QString &initialGameDir = QString(),
                        const QString &initialPackage = QString(),
                        QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildMenus();

    LogPanel *m_log = nullptr;
    PackPage *m_packPage = nullptr;
    UnpackPage *m_unpackPage = nullptr;
    QTabWidget *m_tabs = nullptr;
    QSplitter *m_splitter = nullptr;
};

} // namespace mcpacker
