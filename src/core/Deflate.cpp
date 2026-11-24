#include "core/Deflate.h"

#include <QByteArray>

#include <array>
#include <cstring>
#include <vector>

namespace mcpacker {
namespace {

constexpr int kMaxBits = 15;

// ---------------------------------------------------------------------------
// 位读取器（DEFLATE 是低位优先）
// ---------------------------------------------------------------------------
struct BitReader
{
    const quint8 *data = nullptr;
    qsizetype size = 0;
    qsizetype pos = 0;      // 下一个待读字节
    quint32 buffer = 0;
    int bitCount = 0;
    bool overrun = false;

    BitReader(const quint8 *d, qsizetype n) : data(d), size(n) {}

    void ensure(int need)
    {
        while (bitCount < need) {
            if (pos >= size) {
                overrun = true;
                return;
            }
            buffer |= quint32(data[pos++]) << bitCount;
            bitCount += 8;
        }
    }

    int bit()
    {
        ensure(1);
        if (overrun)
            return 0;
        int v = int(buffer & 1u);
        buffer >>= 1;
        --bitCount;
        return v;
    }

    int bits(int need)
    {
        if (need == 0)
            return 0;
        ensure(need);
        if (overrun)
            return 0;
        int v = int(buffer & ((1u << need) - 1u));
        buffer >>= need;
        bitCount -= need;
        return v;
    }

    void alignToByte()
    {
        int drop = bitCount & 7;
        buffer >>= drop;
        bitCount -= drop;
    }
};

// ---------------------------------------------------------------------------
// 规范 Huffman 树（puff.c 风格的 count/symbol 表示）
// ---------------------------------------------------------------------------
struct Huffman
{
    std::array<short, kMaxBits + 1> count{};
    std::vector<short> symbol;

    /// 返回 0 表示完整码表；>0 表示不完整；<0 表示超订。
    int construct(const quint8 *lengths, int n)
    {
        symbol.assign(size_t(n), 0);
        count.fill(0);
        for (int i = 0; i < n; ++i) {
            if (lengths[i] > kMaxBits)
                return -1;
            count[lengths[i]]++;
        }
        if (count[0] == n)
            return 0;

        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len) {
            left <<= 1;
            left -= count[len];
            if (left < 0)
                return left;
        }

        std::array<short, kMaxBits + 1> offs{};
        for (int len = 1; len < kMaxBits; ++len)
            offs[len + 1] = short(offs[len] + count[len]);

        for (int i = 0; i < n; ++i) {
            if (lengths[i] != 0) {
                size_t idx = size_t(offs[lengths[i]]++);
                if (idx < symbol.size())
                    symbol[idx] = short(i);
            }
        }
        return left;
    }

    /// 解码下一个符号，失败返回 -1。
    int decode(BitReader &br) const
    {
        int len = 0;
        int code = 0;
        int first = 0;
        int index = 0;
        for (len = 1; len <= kMaxBits; ++len) {
            code |= br.bit();
            const int c = count[len];
            if (code - c < first) {
                const int sym = index + (code - first);
                if (sym < 0 || size_t(sym) >= symbol.size())
                    return -1;
                return symbol[size_t(sym)];
            }
            index += c;
            first = (first + c) << 1;
            code <<= 1;
            if (br.overrun)
                return -1;
        }
        return -1;
    }
};

// ---------------------------------------------------------------------------
// 固定 Huffman 表（RFC1951 §3.2.6）
// ---------------------------------------------------------------------------
void buildFixedTrees(Huffman &lit, Huffman &dist)
{
    std::array<quint8, 288> lengths{};
    for (int i = 0; i < 144; ++i)
        lengths[size_t(i)] = 8;
    for (int i = 144; i < 256; ++i)
        lengths[size_t(i)] = 9;
    for (int i = 256; i < 280; ++i)
        lengths[size_t(i)] = 7;
    for (int i = 280; i < 288; ++i)
        lengths[size_t(i)] = 8;
    lit.construct(lengths.data(), 288);

    std::array<quint8, 30> dl{};
    dl.fill(5);
    dist.construct(dl.data(), 30);
}

// 长度码表
constexpr quint16 kLenBase[29] = {3,   4,   5,   6,   7,   8,   9,   10,  11,  13,
                                  15,  17,  19,  23,  27,  31,  35,  43,  51,  59,
                                  67,  83,  99,  115, 131, 163, 195, 227, 258};
constexpr quint8 kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

// 距离码表
constexpr quint16 kDistBase[30] = {1,    2,    3,    4,    5,     7,     9,     13,    17,   25,
                                   33,   49,   65,   97,   129,   193,   257,   385,   513,  769,
                                   1025, 1537, 2049, 3073, 4097,  6145,  8193,  12289, 16385, 24577};
constexpr quint8 kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                   6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

constexpr int kCodeLengthOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

// ---------------------------------------------------------------------------
// 块解码
// ---------------------------------------------------------------------------
bool inflateCodes(BitReader &br, const Huffman &lit, const Huffman &dist, std::vector<quint8> &out,
                  QString *error)
{
    for (;;) {
        const int sym = lit.decode(br);
        if (sym < 0) {
            if (error)
                *error = QStringLiteral("deflate 数据损坏：无效的长度/字面量符号");
            return false;
        }
        if (sym < 256) {
            out.push_back(quint8(sym));
            continue;
        }
        if (sym == 256)
            return true;

        const int li = sym - 257;
        if (li < 0 || li >= 29) {
            if (error)
                *error = QStringLiteral("deflate 数据损坏：长度符号越界");
            return false;
        }
        int len = kLenBase[li] + br.bits(kLenExtra[li]);
        if (br.overrun) {
            if (error)
                *error = QStringLiteral("deflate 数据截断");
            return false;
        }

        const int dsym = dist.decode(br);
        if (dsym < 0 || dsym >= 30) {
            if (error)
                *error = QStringLiteral("deflate 数据损坏：无效的距离符号");
            return false;
        }
        const int distance = kDistBase[dsym] + br.bits(kDistExtra[dsym]);
        if (br.overrun) {
            if (error)
                *error = QStringLiteral("deflate 数据截断");
            return false;
        }
        if (distance <= 0 || qsizetype(distance) > qsizetype(out.size())) {
            if (error)
                *error = QStringLiteral("deflate 数据损坏：回溯距离超出已解压数据");
            return false;
        }
        const size_t start = out.size() - size_t(distance);
        for (int i = 0; i < len; ++i)
            out.push_back(out[start + size_t(i)]);
    }
}

bool inflateStored(BitReader &br, std::vector<quint8> &out, QString *error)
{
    br.alignToByte();
    const int len = br.bits(16);
    const int nlen = br.bits(16);
    if (br.overrun) {
        if (error)
            *error = QStringLiteral("deflate 存储块头截断");
        return false;
    }
    if (((len ^ nlen) & 0xFFFF) != 0xFFFF) {
        if (error)
            *error = QStringLiteral("deflate 存储块长度校验失败");
        return false;
    }
    if (out.capacity() < out.size() + size_t(len))
        out.reserve(out.size() + size_t(len));
    for (int i = 0; i < len; ++i)
        out.push_back(quint8(br.bits(8)));
    if (br.overrun) {
        if (error)
            *error = QStringLiteral("deflate 存储块数据截断");
        return false;
    }
    return true;
}

bool inflateDynamic(BitReader &br, std::vector<quint8> &out, QString *error)
{
    const int hlit = br.bits(5) + 257;
    const int hdist = br.bits(5) + 1;
    const int hclen = br.bits(4) + 4;
    if (br.overrun) {
        if (error)
            *error = QStringLiteral("deflate 动态块头截断");
        return false;
    }
    if (hlit > 286 || hdist > 30) {
        if (error)
            *error = QStringLiteral("deflate 动态块码表数量非法");
        return false;
    }

    std::array<quint8, 19> clLengths{};
    for (int i = 0; i < hclen; ++i)
        clLengths[size_t(kCodeLengthOrder[i])] = quint8(br.bits(3));
    if (br.overrun) {
        if (error)
            *error = QStringLiteral("deflate 动态块码长表截断");
        return false;
    }

    Huffman clTree;
    if (clTree.construct(clLengths.data(), 19) != 0) {
        if (error)
            *error = QStringLiteral("deflate 码长码表非法");
        return false;
    }

    std::vector<quint8> lengths(size_t(hlit + hdist), 0);
    int idx = 0;
    while (idx < hlit + hdist) {
        const int sym = clTree.decode(br);
        if (sym < 0) {
            if (error)
                *error = QStringLiteral("deflate 码长解码失败");
            return false;
        }
        if (sym < 16) {
            lengths[size_t(idx++)] = quint8(sym);
            continue;
        }
        int repeat = 0;
        quint8 value = 0;
        if (sym == 16) {
            if (idx == 0) {
                if (error)
                    *error = QStringLiteral("deflate 码长重复引用越界");
                return false;
            }
            value = lengths[size_t(idx - 1)];
            repeat = 3 + br.bits(2);
        } else if (sym == 17) {
            repeat = 3 + br.bits(3);
        } else { // 18
            repeat = 11 + br.bits(7);
        }
        if (br.overrun) {
            if (error)
                *error = QStringLiteral("deflate 码长重复段截断");
            return false;
        }
        if (idx + repeat > hlit + hdist) {
            if (error)
                *error = QStringLiteral("deflate 码长重复段超出范围");
            return false;
        }
        for (int i = 0; i < repeat; ++i)
            lengths[size_t(idx++)] = value;
    }

    Huffman litTree;
    Huffman distTree;
    if (litTree.construct(lengths.data(), hlit) < 0) {
        if (error)
            *error = QStringLiteral("deflate 字面量码表非法");
        return false;
    }
    if (distTree.construct(lengths.data() + hlit, hdist) < 0) {
        if (error)
            *error = QStringLiteral("deflate 距离码表非法");
        return false;
    }
    return inflateCodes(br, litTree, distTree, out, error);
}

} // namespace

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------
QByteArray deflateRaw(const QByteArray &input, int level)
{
    if (input.isEmpty())
        return {};

    const QByteArray z = qCompress(input, level);
    // qCompress 返回「4 字节原始长度 + zlib 流 + 4 字节 Adler32」，
    // zlib 流头再占 2 字节；剥掉这三部分即为 raw deflate。
    if (z.size() <= 6)
        return {};

    const auto cmf = quint8(z.at(4));
    const auto flg = quint8(z.at(5));
    if ((cmf & 0x0F) != 8)
        return {};
    if (((quint32(cmf) << 8) | flg) % 31 != 0)
        return {};
    // zlib 的 FDICT 标志不支持
    if (flg & 0x20)
        return {};

    const qsizetype rawLen = z.size() - 4 - 2 - 4;
    if (rawLen <= 0)
        return {};
    return z.mid(6, rawLen);
}

QByteArray inflateRaw(const QByteArray &raw, quint32 expectedSize, QString *error)
{
    if (error)
        error->clear();
    if (raw.isEmpty())
        return {};

    BitReader br(reinterpret_cast<const quint8 *>(raw.constData()), raw.size());
    std::vector<quint8> out;
    if (expectedSize > 0 && expectedSize < (1u << 31))
        out.reserve(size_t(expectedSize));

    bool finalBlock = false;
    while (!finalBlock) {
        finalBlock = br.bit() != 0;
        const int type = br.bits(2);
        if (br.overrun) {
            if (error)
                *error = QStringLiteral("deflate 数据头截断");
            return {};
        }
        switch (type) {
        case 0: {
            if (!inflateStored(br, out, error))
                return {};
            break;
        }
        case 1: {
            Huffman lit;
            Huffman dist;
            buildFixedTrees(lit, dist);
            if (!inflateCodes(br, lit, dist, out, error))
                return {};
            break;
        }
        case 2: {
            if (!inflateDynamic(br, out, error))
                return {};
            break;
        }
        default:
            if (error)
                *error = QStringLiteral("deflate 块类型 3 非法");
            return {};
        }
        if (out.size() > 0xFFFFFFFFu) {
            if (error)
                *error = QStringLiteral("deflate 解压结果超过 4GB");
            return {};
        }
    }

    if (expectedSize > 0 && out.size() != expectedSize) {
        if (error)
            *error = QStringLiteral("deflate 解压长度不符：期望 %1，实际 %2")
                         .arg(expectedSize)
                         .arg(out.size());
        return {};
    }

    return QByteArray(reinterpret_cast<const char *>(out.data()), qsizetype(out.size()));
}

} // namespace mcpacker
