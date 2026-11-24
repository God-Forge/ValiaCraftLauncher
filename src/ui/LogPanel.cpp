#include "ui/LogPanel.h"

#include <QDateTime>
#include <QFontDatabase>
#include <QScrollBar>
#include <QTextEdit>
#include <QVBoxLayout>

namespace mcpacker {

LogPanel::LogPanel(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_view = new QTextEdit(this);
    m_view->setReadOnly(true);
    m_view->setAcceptRichText(true);
    m_view->setLineWrapMode(QTextEdit::WidgetWidth);
    m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_view->document()->setMaximumBlockCount(8000);
    layout->addWidget(m_view);
}

void LogPanel::appendColored(const QString &text, const QString &color)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString html =
        QStringLiteral("<span style=\"color:#7f8c8d;\">[%1]</span> "
                       "<span style=\"color:%2; white-space:pre-wrap;\">%3</span>")
            .arg(stamp, color, text.toHtmlEscaped());
    m_view->append(html);
    m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->maximum());
}

void LogPanel::appendLine(const QString &text)
{
    appendColored(text, QStringLiteral("#d0d0d0"));
}

void LogPanel::appendInfo(const QString &text)
{
    appendColored(text, QStringLiteral("#8ab4f8"));
}

void LogPanel::appendWarning(const QString &text)
{
    appendColored(QStringLiteral("[警告] ") + text, QStringLiteral("#e6b450"));
}

void LogPanel::appendError(const QString &text)
{
    appendColored(QStringLiteral("[错误] ") + text, QStringLiteral("#f07178"));
}

void LogPanel::appendSuccess(const QString &text)
{
    appendColored(QStringLiteral("[完成] ") + text, QStringLiteral("#7ec699"));
}

void LogPanel::clear()
{
    m_view->clear();
}

} // namespace mcpacker
