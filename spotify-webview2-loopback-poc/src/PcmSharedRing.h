#pragma once

#include "PcmChunkCodec.h"

#include <Windows.h>
#include <wrl.h>
#include "WebView2.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

class PcmSharedRing
{
public:
    HRESULT Initialize(ICoreWebView2Environment* environment);
    void Close();
    bool IsReady() const noexcept;
    ICoreWebView2SharedBuffer* Buffer() const noexcept;
    const std::wstring& DescriptorJson() const noexcept;
    bool HandleCommit(
        std::wstring_view message,
        DecodedPcmChunk& chunk,
        std::wstring& error);

private:
    static constexpr uint32_t kVersion = 1;
    static constexpr uint32_t kRingMagic = 0x31475244;
    static constexpr uint32_t kSlotMagic = 0x31504344;
    static constexpr uint32_t kCommitXor = 0xA5A5A5A5;
    static constexpr size_t kHeaderBytes = 64;
    static constexpr size_t kSlotHeaderBytes = 64;
    static constexpr size_t kSlotCount = 64;
    static constexpr size_t kPayloadCapacity = 19200;
    static constexpr size_t kSlotBytes =
        kSlotHeaderBytes + kPayloadCapacity;
    static constexpr size_t kBufferBytes =
        kHeaderBytes + kSlotCount * kSlotBytes;

    uint32_t Read32(size_t offset) const;
    void Write32(size_t offset, uint32_t value);
    void ResetSlot(size_t slot);

    Microsoft::WRL::ComPtr<ICoreWebView2SharedBuffer> buffer_;
    BYTE* bytes_ = nullptr;
    std::wstring descriptorJson_;
};
