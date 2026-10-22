#include "rimera_residency.hpp"

#include "rimera_device.hpp"

namespace RM
{
    Result<void> ResidencySet::Initialize(Device* device)
    {
        m_device = device;
        auto* native_device = static_cast<MTL::Device*>(m_device->GetNativeHandle());
        auto descriptor = NS::TransferPtr(MTL::ResidencySetDescriptor::alloc()->init());
        NS::Error* error = nullptr;
        m_residency_set = NS::TransferPtr(native_device->newResidencySet(descriptor.get(), &error));
        if (!m_residency_set) return std::unexpected("Failed to create residency set");
        return {};
    }

    void ResidencySet::AddNativeAllocation(void* native)
    {
        m_residency_set->addAllocation(static_cast<MTL::Allocation*>(native));
    }

    void ResidencySet::RemoveNativeAllocation(void* native)
    {
        m_residency_set->removeAllocation(static_cast<MTL::Allocation*>(native));
    }

    void ResidencySet::RemoveAllNativeAllocations()
    {
        if (m_residency_set) m_residency_set->removeAllAllocations();
    }

    void ResidencySet::AddAllocation(const MTL::Allocation* allocation)
    {
        m_residency_set->addAllocation(allocation);
        m_dirty = true;
    }

    void ResidencySet::RemoveAllocation(const MTL::Allocation* allocation)
    {
        m_residency_set->removeAllocation(allocation);
        m_dirty = true;
    }

    void ResidencySet::CommitNative() const { m_residency_set->commit(); }

    void ResidencySet::RequestNativeResidency() const { m_residency_set->requestResidency(); }

    void ResidencySet::EndNativeResidency() const { m_residency_set->endResidency(); }

    void* ResidencySet::GetNative() const { return m_residency_set.get(); }
} // namespace RM
