#include "pch.h"

#include "SourcePluginBank.h"

#include "GeneratedSourcePluginTemplates.h"

#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>

namespace
{
constexpr uint32_t kBankId = 0xAD400000;
constexpr uint32_t kEventId = 0xAD100000;
constexpr uint32_t kActionId = 0xAD200000;
constexpr uint32_t kSoundId = 0xAD800000;
constexpr uint32_t kSourcePluginObjectId = 0xAD810000;
constexpr uint32_t kSourcePluginKey = 0x01016A72;

constexpr uint32_t kGeneratedBankId = 1261543313u;
constexpr uint32_t kGeneratedEventId = 2236792162u;
constexpr uint32_t kGeneratedActionId = 460307619u;
constexpr uint32_t kGeneratedSoundId = 255311509u;
constexpr uint32_t kGeneratedSourcePluginObjectId = 586114608u;

constexpr char kLoadBankMemoryCopyName[] =
    "?LoadBankMemoryCopy@SoundEngine@AK@@YA?AW4AKRESULT@@PEBXIAEAI@Z";

using LoadBankMemoryCopyFn = int32_t(__cdecl*)(const void*, uint32_t, uint32_t*);

uint32_t ReadU32(const uint8_t* data)
{
    uint32_t value = 0;
    memcpy(&value, data, sizeof(value));
    return value;
}

void WriteU32(uint8_t* data, uint32_t value)
{
    memcpy(data, &value, sizeof(value));
}

uint32_t ReplaceU32(uint8_t* data, uint32_t size, uint32_t oldId, uint32_t newId)
{
    uint32_t hits = 0;
    for (uint32_t i = 0; i + 4 <= size; ++i)
    {
        if (ReadU32(data + i) == oldId)
        {
            WriteU32(data + i, newId);
            ++hits;
        }
    }
    return hits;
}

size_t AppendItem(std::vector<uint8_t>& out, uint8_t type,
    const uint8_t* body, uint32_t size)
{
    out.push_back(type);
    const size_t sizeOffset = out.size();
    out.resize(out.size() + 4);
    WriteU32(out.data() + sizeOffset, size);
    const size_t bodyOffset = out.size();
    out.insert(out.end(), body, body + size);
    return bodyOffset;
}

std::vector<uint8_t> BuildSourcePluginBank(const Logger& logger)
{
    const uint8_t* eventBody = GeneratedSourcePluginTemplates::kEvent;
    const uint32_t eventSize = GeneratedSourcePluginTemplates::kEventSize;
    const uint8_t* actionBody = GeneratedSourcePluginTemplates::kAction;
    const uint32_t actionSize = GeneratedSourcePluginTemplates::kActionSize;
    const uint8_t* soundBody = GeneratedSourcePluginTemplates::kSound;
    const uint32_t soundSize = GeneratedSourcePluginTemplates::kSoundSize;
    const uint8_t* sourceBody = GeneratedSourcePluginTemplates::kSourcePlugin;
    const uint32_t sourceSize = GeneratedSourcePluginTemplates::kSourcePluginSize;
    const uint8_t* bkhdBody = GeneratedSourcePluginTemplates::kBkhd;
    const uint32_t bkhdSize = GeneratedSourcePluginTemplates::kBkhdSize;
    {
        std::ostringstream oss;
        oss << "generated source plugin HIRC event=" << eventSize
            << " action=" << actionSize
            << " sound=" << soundSize
            << " sourcePlugin=" << sourceSize
            << " bkhd=" << bkhdSize;
        logger.Log(oss.str());
    }
    std::vector<uint8_t> bank;
    logger.Log("BuildSourcePluginBank: begin BKHD");
    bank.insert(bank.end(), {'B', 'K', 'H', 'D'});
    const size_t bkhdSizeOffset = bank.size();
    bank.resize(bank.size() + 4);
    WriteU32(bank.data() + bkhdSizeOffset, bkhdSize);
    const size_t bkhdBodyOffset = bank.size();
    bank.insert(bank.end(), bkhdBody, bkhdBody + bkhdSize);
    WriteU32(bank.data() + bkhdBodyOffset + 4, kBankId);

    bank.insert(bank.end(), {'H', 'I', 'R', 'C'});
    const size_t hircSizeOffset = bank.size();
    bank.resize(bank.size() + 4);
    const size_t hircStart = bank.size();
    bank.resize(bank.size() + 4);
    WriteU32(bank.data() + hircStart, 4);

    bank.reserve(8 + bkhdSize + 8 + 4 + 20 +
        eventSize + actionSize + soundSize + sourceSize);

    logger.Log("BuildSourcePluginBank: append SourcePlugin");
    size_t sourceOffset = AppendItem(bank, 0x11, sourceBody, sourceSize);
    uint8_t* newSource = bank.data() + sourceOffset;
    WriteU32(newSource + 0, kSourcePluginObjectId);

    logger.Log("BuildSourcePluginBank: append Sound");
    size_t soundOffset = AppendItem(bank, 2, soundBody, soundSize);
    uint8_t* newSound = bank.data() + soundOffset;
    WriteU32(newSound + 0, kSoundId);
    ReplaceU32(newSound, soundSize,
        kGeneratedSourcePluginObjectId, kSourcePluginObjectId);

    logger.Log("BuildSourcePluginBank: append Action");
    size_t actionOffset = AppendItem(bank, 3, actionBody, actionSize);
    uint8_t* newAction = bank.data() + actionOffset;
    WriteU32(newAction + 0, kActionId);
    ReplaceU32(newAction, actionSize, kGeneratedSoundId, kSoundId);
    ReplaceU32(newAction, actionSize, kGeneratedBankId, kBankId);

    logger.Log("BuildSourcePluginBank: append Event");
    size_t eventOffset = AppendItem(bank, 4, eventBody, eventSize);
    uint8_t* newEvent = bank.data() + eventOffset;
    WriteU32(newEvent + 0, kEventId);
    ReplaceU32(newEvent, eventSize, kGeneratedActionId, kActionId);

    WriteU32(bank.data() + hircSizeOffset,
        static_cast<uint32_t>(bank.size() - hircStart));
    logger.Log("BuildSourcePluginBank: complete");
    return bank;
}

const char* AkResultName(int32_t result)
{
    switch (result)
    {
    case 1: return "AK_Success";
    case 31: return "AK_InvalidParameter";
    case 64: return "AK_WrongBankVersion";
    case 69: return "AK_BankAlreadyLoaded";
    case 88: return "AK_PluginNotRegistered";
    case 91: return "AK_DuplicateUniqueID";
    case 92: return "AK_InitBankNotLoaded";
    case 100: return "AK_InvalidBankType";
    case 102: return "AK_NotInitialized";
    default: return "AKRESULT_other";
    }
}
}

namespace SourcePluginBank
{
int32_t TryLoad(HMODULE gameModule, const Logger& logger)
{
    if (!gameModule) return 2;
    const auto loadBank = reinterpret_cast<LoadBankMemoryCopyFn>(
        GetProcAddress(gameModule, kLoadBankMemoryCopyName));
    if (!loadBank)
    {
        logger.Log("source plugin bank skipped: LoadBankMemoryCopy export missing");
        return 2;
    }

    std::vector<uint8_t> bank = BuildSourcePluginBank(logger);
    if (bank.empty()) return 31;
    uint32_t loadedBankId = 0;
    const int32_t result =
        loadBank(bank.data(), static_cast<uint32_t>(bank.size()), &loadedBankId);

    std::ostringstream oss;
    oss << "LoadBankMemoryCopy generated source plugin bank result=" << result
        << "(" << AkResultName(result) << ")"
        << " bankId=0x" << std::hex << loadedBankId
        << " size=" << std::dec << bank.size()
        << " event=0x" << std::hex << kEventId
        << " sound=0x" << kSoundId
        << " sourcePluginObject=0x" << kSourcePluginObjectId
        << " pluginKey=0x" << kSourcePluginKey;
    logger.Log(oss.str());
    return result;
}
} // namespace SourcePluginBank
