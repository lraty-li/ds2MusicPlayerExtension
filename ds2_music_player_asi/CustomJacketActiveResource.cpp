#include "pch.h"

#include "CustomJacketInternal.h"

#include <atomic>

namespace
{
std::atomic<uint64_t> g_resource{0};
}

namespace CustomJacketInternal
{
void SetActiveCustomJacketD3D12Resource(uint64_t resource)
{
    g_resource.store(resource);
}

uint64_t GetActiveCustomJacketD3D12Resource()
{
    return g_resource.load();
}
}
