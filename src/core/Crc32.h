#pragma once

#include <QtGlobal>

namespace mcpacker {

/// 标准 CRC-32（IEEE 802.3，与 ZIP / zlib 一致）
quint32 crc32(const void *data, qsizetype length, quint32 seed = 0);

} // namespace mcpacker
