#include "rimera_heap.hpp"
#include "rimera_d3d12.hpp"
#include "rimera_device.hpp"

RM::Heap::~Heap()
{
#ifdef RIMERA_DEBUG
    if (!m_allocations.empty())
        std::fprintf(stderr, "[Rimera] Heap destroyed with %zu live allocations\n",
                     m_allocations.size());
#endif
    ReleaseResidency();
    m_heap.Reset();
}

void* RM::Heap::GetNativeHandle() const
{
    return m_heap.Get();
}

RM::Result<void> RM::Heap::Initialize(Device* device, const HeapCreateInfo& info)
{
    if (!device || !info.size) return std::unexpected("Invalid heap creation parameters");
    m_device = device;
    m_memory = info.memory;
    constexpr uint64_t alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    m_size = (info.size + alignment - 1) / alignment * alignment;
    m_free_blocks = {Block{.offset = 0, .size = m_size}};

    auto* native_device = static_cast<ID3D12Device15*>(device->GetNativeHandle());
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    if (FAILED(native_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,
                                                  &options,
                                                  sizeof(options))))
        return std::unexpected("Failed to query resource heap tier");
    if (options.ResourceHeapTier < D3D12_RESOURCE_HEAP_TIER_2)
        return std::unexpected("Heaps require resource heap tier 2");

    const D3D12_HEAP_DESC desc{
        .SizeInBytes = m_size,
        .Properties =
        {
            .Type = D3D12::GetHeapType(info.memory),
            .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
            .CreationNodeMask = 0,
            .VisibleNodeMask = 0,
        },
        .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
        .Flags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
    };
    if (FAILED(native_device->CreateHeap(&desc, IID_PPV_ARGS(&m_heap))))
        return std::unexpected("Failed to create heap");
    m_device->GetCoreResidencySet()->AddAllocation(this);
    return {};
}
