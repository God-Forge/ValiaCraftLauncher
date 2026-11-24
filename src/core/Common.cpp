#include "core/Common.h"

#include <QLocale>
#include <QRegularExpression>

namespace mcpacker {

QString VersionInfo::describe() const
{
    QString mc = mcVersion;
    if (mc.isEmpty())
        mc = id;

    QString text = mc;
    if (!loader.isEmpty() && loader != QStringLiteral("Vanilla")) {
        text += QStringLiteral(" · ") + loader;
        if (!loaderVersion.isEmpty())
            text += QLatin1Char(' ') + loaderVersion;
    }
    if (javaMajor > 0)
        text += QStringLiteral(" · 需 Java %1").arg(javaMajor);
    if (isolated)
        text += QStringLiteral(" · 隔离");
    return text;
}

QVector<int> VersionInfo::parseMcVersion(const QString &v)
{
    QVector<int> parts;
    const QStringList tokens = v.split(QLatin1Char('.'));
    for (const QString &token : tokens) {
        const QString digits = token;
        bool ok = false;
        const int n = digits.toInt(&ok);
        if (!ok)
            break;
        parts.append(n);
    }
    return parts;
}

int VersionInfo::compareMcVersion(const QString &a, const QString &b)
{
    const QVector<int> pa = parseMcVersion(a);
    const QVector<int> pb = parseMcVersion(b);
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const int va = i < pa.size() ? pa.at(i) : 0;
        const int vb = i < pb.size() ? pb.at(i) : 0;
        if (va != vb)
            return va < vb ? -1 : 1;
    }
    return 0;
}

QString JavaInfo::describe() const
{
    QString text;
    if (major > 0)
        text = QStringLiteral("Java %1").arg(major);
    else
        text = QStringLiteral("Java（未知版本）");
    if (!fullVersion.isEmpty() && fullVersion != QString::number(major))
        text += QStringLiteral("（%1）").arg(fullVersion);
    if (!vendor.isEmpty())
        text += QStringLiteral(" · %1").arg(vendor);
    if (!arch.isEmpty())
        text += QStringLiteral(" · %1").arg(arch);
    return text;
}

QString conflictPolicyName(ConflictPolicy policy)
{
    switch (policy) {
    case ConflictPolicy::Overwrite:
        return QStringLiteral("覆盖已有文件");
    case ConflictPolicy::Skip:
        return QStringLiteral("跳过已有文件");
    case ConflictPolicy::Rename:
        return QStringLiteral("重命名保留两者");
    }
    return QStringLiteral("覆盖已有文件");
}

QString formatBytes(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("-");
    const double kb = 1024.0;
    const double mb = kb * 1024.0;
    const double gb = mb * 1024.0;
    const double value = double(bytes);
    if (value >= gb)
        return QStringLiteral("%1 GB").arg(QString::number(value / gb, 'f', 2));
    if (value >= mb)
        return QStringLiteral("%1 MB").arg(QString::number(value / mb, 'f', 2));
    if (value >= kb)
        return QStringLiteral("%1 KB").arg(QString::number(value / kb, 'f', 2));
    return QStringLiteral("%1 B").arg(bytes);
}

QString formatDuration(qint64 ms)
{
    if (ms < 1000)
        return QStringLiteral("%1 毫秒").arg(ms);
    const qint64 seconds = ms / 1000;
    if (seconds < 60)
        return QStringLiteral("%1 秒").arg(seconds);
    const qint64 minutes = seconds / 60;
    const qint64 rest = seconds % 60;
    return QStringLiteral("%1 分 %2 秒").arg(minutes).arg(rest);
}

} // namespace mcpacker
