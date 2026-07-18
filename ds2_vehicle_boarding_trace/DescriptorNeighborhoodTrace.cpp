#include "pch.h"
#include "DescriptorNeighborhoodTrace.h"

#include "VehicleSnapshot.h"

#include <atomic>
#include <mutex>
#include <sstream>
#include <vector>

namespace DescriptorNeighborhoodTrace {
namespace {

constexpr size_t kMaximumEntries = 192;
constexpr uint64_t kFlushDelayMs = 1000;

struct Entry {
    uintptr_t caller = 0;
    uintptr_t descriptor = 0;
    uintptr_t single = 0;
    uint64_t calls = 0;
    uint64_t firstMs = 0;
    uint64_t lastMs = 0;
    uint32_t firstFrame = 0;
    uint32_t lastFrame = 0;
    uint8_t mode = 0;
    float weight = 0.0f;
    float firstDuration = 0.0f;
    float lastDuration = 0.0f;
    float firstSync = 0.0f;
    float lastSync = 0.0f;
};

std::atomic<bool> g_active{false};
std::atomic<uint32_t> g_session{0};
std::atomic<uint64_t> g_startTick{0};
std::atomic<uint64_t> g_lastBoardingTick{0};
std::atomic<uintptr_t> g_syncState{0};
std::mutex g_mutex;
std::vector<Entry> g_entries;
const Logger* g_logger = nullptr;
uintptr_t g_moduleBase = 0;

Entry* FindOrCreate(uintptr_t caller, uintptr_t descriptor)
{
    for (Entry& entry : g_entries) {
        if (entry.caller == caller && entry.descriptor == descriptor)
            return &entry;
    }
    if (g_entries.size() >= kMaximumEntries)
        return nullptr;
    g_entries.push_back({});
    Entry& entry = g_entries.back();
    entry.caller = caller;
    entry.descriptor = descriptor;
    return &entry;
}

void LogEntry(uint32_t session, const Entry& entry)
{
    std::ostringstream oss;
    oss << "DescriptorNeighborhood session=" << session
        << " callerRva=" << VehicleSeatTrace::Hex(
            entry.caller - g_moduleBase)
        << " descriptor=" << VehicleSeatTrace::Hex(entry.descriptor)
        << " calls=" << entry.calls
        << " activeMs=" << entry.firstMs << ".." << entry.lastMs
        << " frame=" << entry.firstFrame << ".." << entry.lastFrame
        << " mode=" << static_cast<uint32_t>(entry.mode)
        << " weight=" << entry.weight
        << " single=" << VehicleSeatTrace::Hex(entry.single)
        << " duration=" << entry.firstDuration << ".." << entry.lastDuration
        << " sync=" << entry.firstSync << ".." << entry.lastSync;
    g_logger->Log(oss.str());
}

} // namespace

void Initialize(const Logger& logger, uintptr_t moduleBase)
{
    g_logger = &logger;
    g_moduleBase = moduleBase;
}

bool Active()
{
    return g_active.load();
}

void MarkBoarding(uint32_t session, uint64_t tick, uintptr_t syncState)
{
    if (g_session.load() != session) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_entries.clear();
        g_session.store(session);
        g_startTick.store(tick);
    }
    g_syncState.store(syncState);
    g_lastBoardingTick.store(tick);
    g_active.store(true);
}

void Observe(
    uintptr_t caller, uintptr_t descriptor, uint8_t mode, float weight,
    uint32_t frame, uintptr_t single, float duration,
    float syncDuration)
{
    if (!g_active.load()) {
        return;
    }

    const uint64_t elapsed = GetTickCount64() - g_startTick.load();
    std::lock_guard<std::mutex> lock(g_mutex);
    Entry* entry = FindOrCreate(caller, descriptor);
    if (!entry)
        return;
    if (!entry->calls) {
        entry->firstMs = elapsed;
        entry->firstFrame = frame;
        entry->mode = mode;
        entry->weight = weight;
        entry->firstDuration = duration;
        entry->firstSync = syncDuration;
    }
    ++entry->calls;
    entry->lastMs = elapsed;
    entry->lastFrame = frame;
    entry->single = single;
    entry->lastDuration = duration;
    entry->lastSync = syncDuration;
}

void FlushIfReady(uint64_t tick)
{
    const uint64_t last = g_lastBoardingTick.load();
    if (!g_active.load() || !last || tick - last < kFlushDelayMs)
        return;
    if (!g_active.exchange(false) || !g_logger)
        return;

    std::vector<Entry> entries;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        entries = g_entries;
    }
    const uint32_t session = g_session.load();
    g_logger->Log("DescriptorNeighborhood summary session=" +
        std::to_string(session) + " entries=" +
        std::to_string(entries.size()) + " syncState=" +
        VehicleSeatTrace::Hex(g_syncState.load()));
    for (const Entry& entry : entries)
        LogEntry(session, entry);
}

} // namespace DescriptorNeighborhoodTrace
