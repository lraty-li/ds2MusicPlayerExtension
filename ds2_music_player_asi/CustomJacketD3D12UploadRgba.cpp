#include "pch.h"

#include "CustomJacketInternal.h"

#include "HookUtils.h"

#include <d3d12.h>
#include <sstream>

namespace
{
std::string H(uint64_t value) { return HookUtils::HexU64(value); }

D3D12_HEAP_PROPERTIES UploadHeap()
{
    D3D12_HEAP_PROPERTIES props = {};
    props.Type = D3D12_HEAP_TYPE_UPLOAD;
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
        if ((value >> i) & 1) block[bitPos / 8] |= uint8_t(1u << (bitPos % 8));
    }
}

void EncodeBc7Solid(uint8_t* block, uint8_t r, uint8_t g, uint8_t b)
{
    memset(block, 0, 16);
    r |= 1; g |= 1; b |= 1;
    uint32_t bitPos = 0;
    PutBits(block, bitPos, 0x40, 7);
    PutBits(block, bitPos, r >> 1, 7); PutBits(block, bitPos, r >> 1, 7);
    PutBits(block, bitPos, g >> 1, 7); PutBits(block, bitPos, g >> 1, 7);
    PutBits(block, bitPos, b >> 1, 7); PutBits(block, bitPos, b >> 1, 7);
    PutBits(block, bitPos, 0x7F, 7); PutBits(block, bitPos, 0x7F, 7);
    PutBits(block, bitPos, 1, 1); PutBits(block, bitPos, 1, 1);
    PutBits(block, bitPos, 0, 3);
    for (uint32_t i = 1; i < 16; ++i) PutBits(block, bitPos, 0, 4);
}

void FillBc7FromRgba(uint8_t* dst, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp,
    const uint8_t* rgba, uint32_t srcW, uint32_t srcH)
{
    const uint32_t bw = (fp.Footprint.Width + 3) / 4;
    const uint32_t bh = (fp.Footprint.Height + 3) / 4;
    for (uint32_t by = 0; by < bh; ++by)
    {
        auto* row = dst + fp.Offset + uint64_t(by) * fp.Footprint.RowPitch;
        for (uint32_t bx = 0; bx < bw; ++bx)
        {
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (uint32_t yy = 0; yy < 4; ++yy)
            {
                const uint32_t dstY = by * 4 + yy;
                if (dstY >= fp.Footprint.Height) continue;
                const uint32_t y = uint32_t((uint64_t(dstY) * srcH) /
                    fp.Footprint.Height);
                for (uint32_t xx = 0; xx < 4; ++xx)
                {
                    const uint32_t dstX = bx * 4 + xx;
                    if (dstX >= fp.Footprint.Width) continue;
                    const uint32_t x = uint32_t((uint64_t(dstX) * srcW) /
                        fp.Footprint.Width);
                    const uint8_t* p = rgba + (uint64_t(y) * srcW + x) * 4;
                    r += p[0]; g += p[1]; b += p[2]; ++n;
                }
            }
            if (!n) n = 1;
            EncodeBc7Solid(row + bx * 16, uint8_t(r / n), uint8_t(g / n), uint8_t(b / n));
        }
    }
}

D3D12_RESOURCE_BARRIER Barrier(ID3D12Resource* res,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    return b;
}

HRESULT Wait(ID3D12Device* device, ID3D12CommandQueue* queue)
{
    ID3D12Fence* fence = nullptr;
    HRESULT hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
        __uuidof(ID3D12Fence), reinterpret_cast<void**>(&fence));
    if (FAILED(hr) || !fence) return hr;
    HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!eventHandle) { fence->Release(); return E_FAIL; }
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

HRESULT CopyUpload(ID3D12Device* device, ID3D12Resource* target,
    ID3D12Resource* upload, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp)
{
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    HRESULT hr = device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue),
        reinterpret_cast<void**>(&queue));
    if (FAILED(hr) || !queue) return hr;
    ID3D12CommandAllocator* alloc = nullptr;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        __uuidof(ID3D12CommandAllocator), reinterpret_cast<void**>(&alloc));
    if (FAILED(hr) || !alloc) { queue->Release(); return hr; }
    ID3D12GraphicsCommandList* list = nullptr;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc,
        nullptr, __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&list));
    if (SUCCEEDED(hr) && list)
    {
        auto a = Barrier(target, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COPY_DEST);
        list->ResourceBarrier(1, &a);
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = upload; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = fp;
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = target; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        auto b = Barrier(target, D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &b);
        hr = list->Close();
    }
    if (SUCCEEDED(hr))
    {
        ID3D12CommandList* lists[] = { list };
        queue->ExecuteCommandLists(1, lists);
        hr = Wait(device, queue);
    }
    if (list) list->Release();
    alloc->Release();
    queue->Release();
    return hr;
}

void LogUpload(const char* phase, const D3D12_RESOURCE_DESC& desc,
    uint64_t uploadBytes, HRESULT hr, const Logger& logger)
{
    std::ostringstream oss;
    oss << "txdx12rgba " << phase << " width=" << desc.Width
        << " height=" << desc.Height << " format=" << desc.Format
        << " uploadBytes=" << uploadBytes << " hr=" << H(uint32_t(hr));
    logger.Log(oss.str());
}
}

namespace CustomJacketInternal
{
bool TryUploadCustomJacketD3D12Rgba(uint64_t resource, const uint8_t* rgba,
    uint32_t width, uint32_t height, const Logger& logger)
{
    if (!resource || !rgba || width == 0 || height == 0) return false;
    auto* target = reinterpret_cast<ID3D12Resource*>(resource);
    D3D12_RESOURCE_DESC desc = target->GetDesc();
    if (desc.Format != DXGI_FORMAT_BC7_UNORM && desc.Format != DXGI_FORMAT_BC7_UNORM_SRGB)
    {
        LogUpload("skipped-format", desc, 0, E_INVALIDARG, logger);
        return false;
    }
    ID3D12Device* device = nullptr;
    HRESULT hr = target->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
    if (FAILED(hr) || !device) { LogUpload("get-device", desc, 0, hr, logger); return false; }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT rows = 0; UINT64 rowBytes = 0; UINT64 uploadBytes = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, &rows, &rowBytes, &uploadBytes);
    ID3D12Resource* upload = nullptr;
    auto heap = UploadHeap();
    auto buffer = BufferDesc(uploadBytes);
    hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource),
        reinterpret_cast<void**>(&upload));
    if (FAILED(hr) || !upload) { device->Release(); LogUpload("create-upload", desc, uploadBytes, hr, logger); return false; }
    uint8_t* mapped = nullptr;
    hr = upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    if (SUCCEEDED(hr) && mapped)
    {
        memset(mapped, 0, size_t(uploadBytes));
        FillBc7FromRgba(mapped, fp, rgba, width, height);
    }
    upload->Unmap(0, nullptr);
    LogUpload("prepared-bc7-rgba", desc, uploadBytes, hr, logger);
    if (SUCCEEDED(hr)) hr = CopyUpload(device, target, upload, fp);
    LogUpload("copy-rgba", desc, uploadBytes, hr, logger);
    upload->Release();
    device->Release();
    return SUCCEEDED(hr);
}
}
