#include "pch.h"

#include "CustomJacketInternal.h"

#include "HookUtils.h"

#include <d3d12.h>
#include <sstream>

namespace
{
std::string H(uint64_t value)
{
    return HookUtils::HexU64(value);
}

void LogAttempt(const char* name, const D3D12_RESOURCE_DESC& desc,
    const D3D12_HEAP_PROPERTIES& heapProps, D3D12_HEAP_FLAGS heapFlags,
    D3D12_RESOURCE_STATES state, HRESULT hr, const Logger& logger)
{
    std::ostringstream oss;
    oss << "txdx12own create " << name
        << " dim=" << desc.Dimension
        << " align=" << desc.Alignment
        << " width=" << desc.Width
        << " height=" << desc.Height
        << " depthArray=" << desc.DepthOrArraySize
        << " mips=" << desc.MipLevels
        << " format=" << desc.Format
        << " samples=" << desc.SampleDesc.Count << "/" << desc.SampleDesc.Quality
        << " layout=" << desc.Layout
        << " flags=" << H(desc.Flags)
        << " heapType=" << heapProps.Type
        << " heapFlags=" << H(heapFlags)
        << " state=" << H(state)
        << " hr=" << H(static_cast<uint32_t>(hr));
    logger.Log(oss.str());
}

bool TryCreate(ID3D12Device* device, const char* name,
    const D3D12_RESOURCE_DESC& desc, const D3D12_HEAP_PROPERTIES& heapProps,
    D3D12_HEAP_FLAGS heapFlags, D3D12_RESOURCE_STATES state,
    ID3D12Resource** outResource, const Logger& logger)
{
    HRESULT hr = device->CreateCommittedResource(&heapProps, heapFlags, &desc,
        state, nullptr, __uuidof(ID3D12Resource),
        reinterpret_cast<void**>(outResource));
    LogAttempt(name, desc, heapProps, heapFlags, state, hr, logger);
    return SUCCEEDED(hr) && *outResource;
}

D3D12_HEAP_PROPERTIES DefaultHeapProps()
{
    D3D12_HEAP_PROPERTIES props = {};
    props.Type = D3D12_HEAP_TYPE_DEFAULT;
    props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    props.CreationNodeMask = 1;
    props.VisibleNodeMask = 1;
    return props;
}

D3D12_RESOURCE_DESC NormalizeTextureDesc(D3D12_RESOURCE_DESC desc)
{
    desc.Alignment = 0;
    desc.SampleDesc.Count = desc.SampleDesc.Count ? desc.SampleDesc.Count : 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    return desc;
}

} // namespace

namespace CustomJacketInternal
{
bool TryCreateCustomJacketD3D12ResourceLike(uint64_t sourceResource,
    uint64_t& outResource, const Logger& logger)
{
    outResource = 0;
    auto* source = reinterpret_cast<ID3D12Resource*>(sourceResource);
    D3D12_RESOURCE_DESC rawDesc = source->GetDesc();
    D3D12_HEAP_PROPERTIES rawHeapProps = {};
    D3D12_HEAP_FLAGS rawHeapFlags = D3D12_HEAP_FLAG_NONE;
    HRESULT hr = source->GetHeapProperties(&rawHeapProps, &rawHeapFlags);
    if (FAILED(hr))
    {
        logger.Log("txdx12own create skipped: GetHeapProperties failed");
        return false;
    }

    ID3D12Device* device = nullptr;
    hr = source->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
    if (FAILED(hr) || !device)
    {
        logger.Log("txdx12own create skipped: GetDevice failed");
        return false;
    }

    ID3D12Resource* created = nullptr;
    D3D12_RESOURCE_DESC texDesc = NormalizeTextureDesc(rawDesc);
    D3D12_HEAP_PROPERTIES defaultHeap = DefaultHeapProps();
    const D3D12_RESOURCE_STATES common = D3D12_RESOURCE_STATE_COMMON;
    const D3D12_RESOURCE_STATES copyDest = D3D12_RESOURCE_STATE_COPY_DEST;

    bool ok = TryCreate(device, "raw", rawDesc, rawHeapProps, rawHeapFlags,
        common, &created, logger);
    if (!ok)
    {
        ok = TryCreate(device, "raw-default-flags", rawDesc, rawHeapProps,
            D3D12_HEAP_FLAG_NONE, common, &created, logger);
    }
    if (!ok)
    {
        ok = TryCreate(device, "default-common", texDesc, defaultHeap,
            D3D12_HEAP_FLAG_NONE, common, &created, logger);
    }
    if (!ok)
    {
        ok = TryCreate(device, "default-copydest", texDesc, defaultHeap,
            D3D12_HEAP_FLAG_NONE, copyDest, &created, logger);
    }

    device->Release();
    if (!ok || !created) return false;
    outResource = reinterpret_cast<uint64_t>(created);
    return true;
}
} // namespace CustomJacketInternal
