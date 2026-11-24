#pragma once

#include <QWidget>

class QTextEdit;

namespace mcpacker {

/// 共享日志面板：带时间戳、按级别着色。
class LogPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LogPanel(QWidget *parent = nullptr);

public slots:
    void appendLine(const QString &text);
    void appendInfo(const QString &text);
    void appendWarning(const QString &text);
    void appendError(const QString &text);
    void appendSuccess(const QString &text);
    void clear();

private:
    void appendColored(const QString &text, const QString &color);
    QTextEdit *m_view = nullptr;
};

} // namespace mcpacker
