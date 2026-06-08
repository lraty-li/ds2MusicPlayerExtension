#include "pch.h"

#include "TextureUploadHistory.h"

#include "HookUtils.h"

#include <sstream>

namespace
{
constexpr uint32_t kCapacity = 16384;
TextureUploadHistory::Snapshot g_ring[kCapacity] = {};
volatile LONG64 g_nextSequence = 0;

const char* LabelFor(uint64_t texture, uint64_t hotPB, uint64_t noDataPB, uint64_t clonePB)
{
    if (texture == hotPB) return "hot";
    if (texture == noDataPB) return "noData";
    if (texture == clonePB) return "clone";
    return "";
}

bool IsTarget(uint64_t texture, uint64_t hotPB, uint64_t noDataPB, uint64_t clonePB)
{
    return texture && (texture == hotPB || texture == noDataPB || texture == clonePB);
}

void LogSnapshot(const TextureUploadHistory::Snapshot& s,
    const char* label, const Logger& logger)
{
    std::ostringstream oss;
    oss << "txupload match " << label
        << " call=" << s.callIndex
        << " tex=" << HookUtils::HexU64(s.textureDx12)
        << " reader=" << HookUtils::HexU64(s.reader)
        << " readerVt=" << HookUtils::HexU64(s.readerVtable)
        << " callerRva=" << HookUtils::HexU64(s.callerRva)
        << " hdr=" << HookUtils::HexU64(s.header)
        << " f2c=" << HookUtils::HexU64(s.field2C)
        << " f30=" << HookUtils::HexU64(s.field30)
        << " f60=" << HookUtils::HexU64(s.field60)
        << " f64=" << HookUtils::HexU64(s.field64)
        << " s80=" << HookUtils::HexU64(s.slot80)
        << " s88=" << HookUtils::HexU64(s.slot88)
        << " d90=" << HookUtils::HexU64(s.desc90)
        << " sD0=" << HookUtils::HexU64(s.slotD0)
        << " sD8=" << HookUtils::HexU64(s.slotD8)
        << " dE0=" << HookUtils::HexU64(s.descE0)
        << " rq8=" << HookUtils::HexU64(s.readerQ8)
        << " rq10=" << HookUtils::HexU64(s.readerQ10)
        << " rq18=" << HookUtils::HexU64(s.readerQ18)
        << " rq20=" << HookUtils::HexU64(s.readerQ20)
        << " rq28=" << HookUtils::HexU64(s.readerQ28)
        << " rq30=" << HookUtils::HexU64(s.readerQ30)
        << " rq38=" << HookUtils::HexU64(s.readerQ38)
        << " readerBase=" << HookUtils::HexU64(s.readerRegionBase)
        << " readerRegion=" << s.readerRegionSize
        << " tid=" << s.threadId
        << " stackLow=" << HookUtils::HexU64(s.stackLow)
        << " stackHigh=" << HookUtils::HexU64(s.stackHigh)
        << " readerOnStack=" << s.readerOnStack
        << " readerProtect=" << HookUtils::HexU64(s.readerProtect)
        << " readerType=" << HookUtils::HexU64(s.readerType)
        << " readerState=" << HookUtils::HexU64(s.readerState);
    logger.Log(oss.str());
}
} // namespace

namespace TextureUploadHistory
{
void Record(const Snapshot& snapshot)
{
    const LONG64 seq = InterlockedIncrement64(&g_nextSequence);
    Snapshot copy = snapshot;
    copy.callIndex = static_cast<uint64_t>(seq);
    g_ring[(seq - 1) % kCapacity] = copy;
}

void LogMatches(uint64_t hotPB, uint64_t noDataPB, uint64_t clonePB, const Logger& logger)
{
    const LONG64 end = g_nextSequence;
    const LONG64 begin = end > kCapacity ? end - kCapacity + 1 : 1;
    uint32_t hot = 0;
    uint32_t noData = 0;
    uint32_t clone = 0;

    for (LONG64 seq = begin; seq <= end; ++seq)
    {
        const Snapshot s = g_ring[(seq - 1) % kCapacity];
        if (s.callIndex != static_cast<uint64_t>(seq)) continue;
        if (!IsTarget(s.textureDx12, hotPB, noDataPB, clonePB)) continue;

        const char* label = LabelFor(s.textureDx12, hotPB, noDataPB, clonePB);
        if (s.textureDx12 == hotPB) ++hot;
        if (s.textureDx12 == noDataPB) ++noData;
        if (s.textureDx12 == clonePB) ++clone;
        LogSnapshot(s, label, logger);
    }

    std::ostringstream oss;
    oss << "txupload matches hot=" << hot
        << " noData=" << noData
        << " clone=" << clone
        << " seen=" << end
        << " capacity=" << kCapacity;
    logger.Log(oss.str());
}
} // namespace TextureUploadHistory
