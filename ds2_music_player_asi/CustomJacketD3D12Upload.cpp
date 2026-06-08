#include "pch.h"

#include "CustomJacketInternal.h"

#include "HookUtils.h"

#include <d3d12.h>
#include <sstream>
#include <vector>

namespace
{
std::string H(uint64_t value) { return HookUtils::HexU64(value); }

D3D12_HEAP_PROPERTIES UploadHeapProps()
{
    D3D12_HEAP_PROPERTIES props = {};
    props.Type = D3D12_HEAP_TYPE_UPLOAD;
    props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    props.CreationNodeMask = 1;
    props.VisibleNodeMask = 1;
    return props;
}

D3D12_RESOURCE_DESC BufferDesc(uint64_t size)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

void PutBits(uint8_t* block, uint32_t& bitPos, uint64_t value, uint32_t bits)
{
    for (uint32_t i = 0; i < bits; ++i, ++bitPos)
    {
        if ((value >> i) & 1)
        {
            block[bitPos / 8] |= static_cast<uint8_t>(1u << (bitPos % 8));
        }
    }
}

void EncodeBc7Mode6Solid(uint8_t* block, uint8_t r, uint8_t g, uint8_t b)
{
    memset(block, 0, 16);
    r |= 1;
    g |= 1;
    b |= 1;
    constexpr uint8_t alpha = 0xFF;
    uint32_t bitPos = 0;

    PutBits(block, bitPos, 0x40, 7);
    PutBits(block, bitPos, r >> 1, 7);
    PutBits(block, bitPos, r >> 1, 7);
    PutBits(block, bitPos, g >> 1, 7);
    PutBits(block, bitPos, g >> 1, 7);
    PutBits(block, bitPos, b >> 1, 7);
    PutBits(block, bitPos, b >> 1, 7);
    PutBits(block, bitPos, alpha >> 1, 7);
    PutBits(block, bitPos, alpha >> 1, 7);
    PutBits(block, bitPos, 1, 1);
    PutBits(block, bitPos, 1, 1);
    PutBits(block, bitPos, 0, 3);
    for (uint32_t i = 1; i < 16; ++i)
    {
        PutBits(block, bitPos, 0, 4);
    }
}

void FillBc7Blocks(uint8_t* dst, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp)
{
    const uint32_t blocksWide = (fp.Footprint.Width + 3) / 4;
    const uint32_t blocksHigh = (fp.Footprint.Height + 3) / 4;
    for (uint32_t y = 0; y < blocksHigh; ++y)
    {
        auto* row = dst + fp.Offset + static_cast<uint64_t>(y) * fp.Footprint.RowPitch;
        for (uint32_t x = 0; x < blocksWide; ++x)
        {
            auto* block = row + x * 16;
            const bool alt = ((x / 8 + y / 6) & 1) != 0;
            const bool left = x < blocksWide / 2;
            EncodeBc7Mode6Solid(block,
                left ? 0xFF : 0x21,
                alt ? 0xFF : 0x21,
                (!left && !alt) ? 0xFF : 0x21);
        }
    }
}

void LogUpload(const char* phase, const D3D12_RESOURCE_DESC& desc,
    uint64_t uploadBytes, HRESULT hr, const Logger& logger)
{
    std::ostringstream oss;
    oss << "txdx12upload " << phase
        << " width=" << desc.Width
        << " height=" << desc.Height
        << " format=" << desc.Format
        << " uploadBytes=" << uploadBytes
        << " hr=" << H(static_cast<uint32_t>(hr));
    logger.Log(oss.str());
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

HRESULT WaitForQueue(ID3D12Device* device, ID3D12CommandQueue* queue)
{
    ID3D12Fence* fence = nullptr;
    HRESULT hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
        __uuidof(ID3D12Fence), reinterpret_cast<void**>(&fence));
    if (FAILED(hr) || !fence) return hr;

    HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!eventHandle)
    {
        fence->Release();
        return HRESULT_FROM_WIN32(GetLastError());
    }

    hr = queue->Signal(fence, 1);
    if (SUCCEEDED(hr) && fence->GetCompletedValue() < 1)
    {
        hr = fence->SetEventOnCompletion(1, eventHandle);
        if (SUCCEEDED(hr)) WaitForSingleObject(eventHandle, INFINITE);
    }
    CloseHandle(eventHandle);
    fence->Release();
    return hr;
}

HRESULT SubmitCopy(ID3D12Device* device, ID3D12Resource* target,
    ID3D12Resource* upload, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp)
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    HRESULT hr = device->CreateCommandQueue(&queueDesc,
        __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&queue));
    if (FAILED(hr) || !queue) return hr;

    ID3D12CommandAllocator* allocator = nullptr;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        __uuidof(ID3D12CommandAllocator), reinterpret_cast<void**>(&allocator));
    if (FAILED(hr) || !allocator) { queue->Release(); return hr; }

    ID3D12GraphicsCommandList* list = nullptr;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, nullptr, __uuidof(ID3D12GraphicsCommandList),
        reinterpret_cast<void**>(&list));
    if (SUCCEEDED(hr) && list)
    {
        auto toCopy = Transition(target, D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_COPY_DEST);
        list->ResourceBarrier(1, &toCopy);

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = upload;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = fp;
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = target;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        auto toShader = Transition(target, D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &toShader);
        hr = list->Close();
    }
    if (SUCCEEDED(hr))
    {
        ID3D12CommandList* lists[] = { list };
        queue->ExecuteCommandLists(1, lists);
        hr = WaitForQueue(device, queue);
    }

    if (list) list->Release();
    allocator->Release();
    queue->Release();
    return hr;
}
} // namespace

namespace CustomJacketInternal
{
bool TryUploadCustomJacketD3D12TestPattern(uint64_t resource, const Logger& logger)
{
    auto* target = reinterpret_cast<ID3D12Resource*>(resource);
    D3D12_RESOURCE_DESC desc = target->GetDesc();
    if (desc.Format != DXGI_FORMAT_BC7_UNORM
        && desc.Format != DXGI_FORMAT_BC7_UNORM_SRGB)
    {
        LogUpload("skipped-format", desc, 0, E_INVALIDARG, logger);
        return false;
    }

    ID3D12Device* device = nullptr;
    HRESULT hr = target->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
    if (FAILED(hr) || !device) { LogUpload("get-device", desc, 0, hr, logger); return false; }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT rows = 0;
    UINT64 rowBytes = 0;
    UINT64 uploadBytes = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, &rows, &rowBytes, &uploadBytes);

    ID3D12Resource* upload = nullptr;
    D3D12_HEAP_PROPERTIES heapProps = UploadHeapProps();
    D3D12_RESOURCE_DESC uploadDesc = BufferDesc(uploadBytes);
    hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
        &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        __uuidof(ID3D12Resource), reinterpret_cast<void**>(&upload));
    if (FAILED(hr) || !upload)
    {
        LogUpload("create-upload", desc, uploadBytes, hr, logger);
        device->Release();
        return false;
    }

    uint8_t* mapped = nullptr;
    hr = upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    if (SUCCEEDED(hr) && mapped)
    {
        memset(mapped, 0, static_cast<size_t>(uploadBytes));
        FillBc7Blocks(mapped, fp);
    }
    upload->Unmap(0, nullptr);
    LogUpload("prepared-bc7", desc, uploadBytes, hr, logger);
    if (SUCCEEDED(hr)) hr = SubmitCopy(device, target, upload, fp);
    LogUpload("copy", desc, uploadBytes, hr, logger);

    upload->Release();
    device->Release();
    return SUCCEEDED(hr);
}
} // namespace CustomJacketInternal
