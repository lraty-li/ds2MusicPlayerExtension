#include "pch.h"

#include "AudioPipeReader.h"

#include <cstdio>
#include <cstdint>
#include <mutex>
#include <string>

namespace
{
constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\ds2_tab_audio_pcm";
constexpr uint32_t kMagic = 0x44533241;
constexpr uint32_t kMaxPacketBytes = 65536;
constexpr uint32_t kRingFrames = 48000 * 4;
constexpr uint32_t kRingChannels = 2;

std::mutex g_logMutex;
std::mutex g_ringMutex;
HANDLE g_thread = nullptr;
HANDLE g_stopEvent = nullptr;
float g_ring[kRingFrames * kRingChannels] = {};
uint32_t g_readFrame = 0;
uint32_t g_writeFrame = 0;
uint32_t g_availableFrames = 0;

std::wstring GetLogPath()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring result = path;
    const size_t pos = result.find_last_of(L"\\/");
    if (pos != std::wstring::npos) result.resize(pos + 1);
    result += L"ds2_dll_music_resource.log";
    return result;
}

void Log(const char* text)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    HANDLE file = CreateFileW(GetLogPath().c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    char prefix[96] = {};
    wsprintfA(prefix, "[%02u:%02u:%02u.%03u][tid=%lu] ",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentThreadId());
    DWORD written = 0;
    WriteFile(file, prefix, static_cast<DWORD>(lstrlenA(prefix)), &written, nullptr);
    WriteFile(file, text, static_cast<DWORD>(lstrlenA(text)), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
}

bool ReadExact(HANDLE pipe, void* buffer, DWORD bytes)
{
    auto* cursor = static_cast<uint8_t*>(buffer);
    DWORD total = 0;
    while (total < bytes) {
        DWORD read = 0;
        if (!ReadFile(pipe, cursor + total, bytes - total, &read, nullptr) || read == 0) {
            return false;
        }
        total += read;
    }
    return true;
}

bool ShouldStop()
{
    return g_stopEvent && WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0;
}

void PushPcm16(const uint8_t* pcm, uint32_t frames, uint16_t channels)
{
    if (!pcm || channels == 0) return;
    std::lock_guard<std::mutex> lock(g_ringMutex);
    auto* samples = reinterpret_cast<const int16_t*>(pcm);
    for (uint32_t frame = 0; frame < frames; ++frame) {
        const uint32_t srcFrame = frame * channels;
        const float left = samples[srcFrame] / 32768.0f;
        const float right = channels > 1 ? samples[srcFrame + 1] / 32768.0f : left;
        const uint32_t dst = g_writeFrame * kRingChannels;
        g_ring[dst] = left;
        g_ring[dst + 1] = right;
        g_writeFrame = (g_writeFrame + 1) % kRingFrames;
        if (g_availableFrames < kRingFrames) {
            ++g_availableFrames;
        } else {
            g_readFrame = (g_readFrame + 1) % kRingFrames;
        }
    }
}

void ConsumePipe(HANDLE pipe)
{
    uint64_t packets = 0;
    uint64_t frames = 0;
    uint64_t lastLogTick = GetTickCount64();
    uint64_t lastSeq = UINT64_MAX;
    uint64_t drops = 0;

    while (!ShouldStop()) {
        uint32_t packetBytes = 0;
        if (!ReadExact(pipe, &packetBytes, sizeof(packetBytes))) break;
        if (packetBytes < 28 || packetBytes > kMaxPacketBytes) break;

        auto* packet = static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), 0, packetBytes));
        if (!packet) break;
        const bool ok = ReadExact(pipe, packet, packetBytes);
        if (!ok) {
            HeapFree(GetProcessHeap(), 0, packet);
            break;
        }

        const uint32_t magic = *reinterpret_cast<uint32_t*>(packet);
        const uint16_t channels = *reinterpret_cast<uint16_t*>(packet + 6);
        const uint32_t frameCount = *reinterpret_cast<uint32_t*>(packet + 12);
        const uint64_t seq = *reinterpret_cast<uint64_t*>(packet + 16);
        const uint32_t pcmBytes = *reinterpret_cast<uint32_t*>(packet + 24);
        if (magic == kMagic) {
            if (lastSeq != UINT64_MAX && seq != lastSeq + 1) {
                drops += seq > lastSeq ? seq - lastSeq - 1 : 1;
            }
            lastSeq = seq;
            packets++;
            frames += frameCount;
            if (28 + pcmBytes == packetBytes &&
                pcmBytes >= frameCount * channels * sizeof(int16_t)) {
                PushPcm16(packet + 28, frameCount, channels);
            }
        }
        HeapFree(GetProcessHeap(), 0, packet);

        const uint64_t now = GetTickCount64();
        if (now - lastLogTick >= 5000) {
            char line[160] = {};
            sprintf_s(line, "pipe pcm packets=%llu frames=%llu drops=%llu",
                static_cast<unsigned long long>(packets),
                static_cast<unsigned long long>(frames),
                static_cast<unsigned long long>(drops));
            Log(line);
            lastLogTick = now;
        }
    }
}

DWORD WINAPI ReaderThread(LPVOID)
{
    Log("audio pipe reader thread started");
    while (!ShouldStop()) {
        HANDLE pipe = CreateFileW(kPipeName, GENERIC_READ, 0, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }

        Log("audio pipe connected");
        ConsumePipe(pipe);
        CloseHandle(pipe);
        Log("audio pipe disconnected");
        Sleep(500);
    }
    Log("audio pipe reader thread stopped");
    return 0;
}
}

namespace AudioPipeReader
{
void Start()
{
    if (g_thread) return;
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, ReaderThread, nullptr, 0, nullptr);
}

void Stop()
{
    if (g_stopEvent) SetEvent(g_stopEvent);
    if (g_thread) {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
}

uint32_t Read(float* output, uint32_t frames, uint32_t channels)
{
    if (!output || channels == 0) return 0;
    uint32_t copied = 0;
    std::lock_guard<std::mutex> lock(g_ringMutex);
    while (copied < frames && g_availableFrames > 0) {
        const uint32_t src = g_readFrame * kRingChannels;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            output[ch * frames + copied] = g_ring[src + (ch > 0 ? 1 : 0)];
        }
        g_readFrame = (g_readFrame + 1) % kRingFrames;
        --g_availableFrames;
        ++copied;
    }
    return copied;
}
}
