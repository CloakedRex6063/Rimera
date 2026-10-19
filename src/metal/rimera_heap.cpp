#include "rimera_heap.hpp"
#include "rimera_device.hpp"
#include "rimera_metal.hpp"

RM::Heap::~Heap()
{
#ifdef RIMERA_DEBUG
    if (!m_allocations.empty())
        std::fprintf(stderr, "[Rimera] Heap destroyed with %zu live allocations\n",
                     m_allocations.size());
#endif
    if (m_heap && m_device)
    {
        auto* residency_set = m_device->GetCoreResidencySet();
        residency_set->RemoveAllocation(this);
        residency_set->Commit();
    }
    m_heap.reset();
}

void* RM::Heap::GetNativeHandle() const
{
    return m_heap.get();
}

RM::Result<void> RM::Heap::Initialize(Device* device, const HeapCreateInfo& info)
{
    if (!device || !info.size) return std::unexpected("Invalid heap creation parameters");
    m_device = device;
    m_memory = info.memory;
    m_size = info.size;
    m_free_blocks = {Block{.offset = 0, .size = m_size}};

    auto descriptor = NS::TransferPtr(MTL::HeapDescriptor::alloc()->init());
    descriptor->setType(MTL::HeapTypePlacement);
    descriptor->setSize(m_size);
    descriptor->setStorageMode(Metal::ToStorageMode(info.memory));
    descriptor->setHazardTrackingMode(MTL::HazardTrackingModeUntracked);

    auto* native_device = static_cast<MTL::Device*>(device->GetNativeHandle());
    m_heap = NS::TransferPtr(native_device->newHeap(descriptor.get()));
    if (!m_heap) return std::unexpected("Failed to create heap");

    auto* residency_set = m_device->GetCoreResidencySet();
    residency_set->AddAllocation(this);
    residency_set->Commit();
    return {};
}
