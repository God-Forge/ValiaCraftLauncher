#include "core/Crc32.h"

#include <array>
#include <cstring>

namespace mcpacker {
namespace {

struct Crc32Table
{
    std::array<quint32, 256> table{};

    constexpr Crc32Table()
    {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
    }
};

constexpr Crc32Table kTable{};

} // namespace

quint32 crc32(const void *data, qsizetype length, quint32 seed)
{
    quint32 c = seed ^ 0xFFFFFFFFu;
    const auto *p = static_cast<const quint8 *>(data);
    for (qsizetype i = 0; i < length; ++i)
        c = kTable.table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

} // namespace mcpacker
