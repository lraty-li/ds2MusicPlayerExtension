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

const char* FillBc7FromRgba(uint8_t* dst, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp,
    const uint8_t* rgba, uint32_t srcW, uint32_t srcH, const Logger& logger)
{
    auto* target = dst + fp.Offset;
    const uint32_t dstW = fp.Footprint.Width;
    const uint32_t dstH = fp.Footprint.Height;
    const uint32_t rowPitch = fp.Footprint.RowPitch;
    if (CustomJacketInternal::TryEncodeExternalBc7ToRows(target, dstW, dstH,
        rowPitch, rgba, srcW, srcH, logger))
    {
        return "prepared-bc7e-pixels";
    }
    return nullptr;
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
    oss << "txdx12image " << phase << " width=" << desc.Width
        << " height=" << desc.Height << " format=" << desc.Format
        << " uploadBytes=" << uploadBytes << " hr=" << H(uint32_t(hr));
    logger.Log(oss.str());
}
}

namespace CustomJacketInternal
{
bool TryUploadCustomJacketD3D12Pixels(uint64_t resource, const uint8_t* rgba,
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
    const char* preparedPhase = "prepared-bc7-pixels";
    hr = upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    if (SUCCEEDED(hr) && mapped)
    {
        memset(mapped, 0, size_t(uploadBytes));
        preparedPhase = FillBc7FromRgba(mapped, fp, rgba, width, height, logger);
        if (!preparedPhase)
        {
            hr = E_FAIL;
            preparedPhase = "prepared-bc7e-unavailable";
        }
    }
    upload->Unmap(0, nullptr);
    LogUpload(preparedPhase, desc, uploadBytes, hr, logger);
    if (SUCCEEDED(hr)) hr = CopyUpload(device, target, upload, fp);
    LogUpload("copy-pixels", desc, uploadBytes, hr, logger);
    upload->Release();
    device->Release();
    return SUCCEEDED(hr);
}
}
