#pragma once

#include "core/Common.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace mcpacker {

/// Java 运行时探测：扫描常见安装位置、游戏自带 runtime 目录，
/// 并通过 `java -XshowSettings:properties -version` 解析版本信息。
class JavaScanner
{
public:
    struct ScanResult
    {
        QVector<JavaInfo> javas;
        QStringList notes;   // 扫描过程中的提示（例如某些目录不可访问）
    };

    /// 扫描可用 Java。includeGameRuntime 为 true 时同时扫描 <gameDir>/runtime。
    static ScanResult scan(const QString &gameDir, bool includeGameRuntime = true);

    /// 探测单个 java.exe。失败返回 false 并写入 error。
    static bool probe(const QString &javaExe, JavaInfo *out, QString *error = nullptr);

    /// 候选 java.exe 路径列表（去重前）
    static QStringList candidateExecutables(const QString &gameDir);

    /// 为某个版本挑选最合适的 Java：优先主版本精确匹配，其次更高版本中最小的那个
    static JavaInfo pickBest(const QVector<JavaInfo> &javas, int requiredMajor);

    /// 判断路径是否是可执行的 java.exe
    static bool isValidJavaExecutable(const QString &path);
};

} // namespace mcpacker
