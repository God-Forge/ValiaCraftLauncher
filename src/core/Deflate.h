#pragma once

#include <QByteArray>
#include <QString>

namespace mcpacker {

/// 使用 Qt 内置 zlib 压缩，并剥离 zlib 包头/Adler32 尾，
/// 得到 ZIP 方法 8 所需的 raw deflate 数据流。
/// 失败（含空输入）时返回空 QByteArray。
QByteArray deflateRaw(const QByteArray &input, int level = -1);

/// 解压 raw deflate 数据流（ZIP 方法 8）。
/// expectedSize 仅用于预分配；为 0 时按增长方式追加。
/// 成功返回解压数据，失败返回空并通过 error 输出原因。
QByteArray inflateRaw(const QByteArray &raw, quint32 expectedSize, QString *error = nullptr);

} // namespace mcpacker
