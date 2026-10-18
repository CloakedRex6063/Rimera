#include "rimera_residency.hpp"

#include "rimera_buffer.hpp"
#include "rimera_device.hpp"
#include "rimera_texture.hpp"

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

    ResidencySet::~ResidencySet()
    {
        if (m_residency_set) m_residency_set->removeAllAllocations();
    }

    void ResidencySet::AddAllocation(Texture* resource)
    {
        m_residency_set->addAllocation(static_cast<MTL::Resource*>(resource->GetNativeHandle()));
    }

    void ResidencySet::RemoveAllocation(const Texture* resource)
    {
        m_residency_set->removeAllocation(static_cast<MTL::Resource*>(resource->GetNativeHandle()));
    }

    void ResidencySet::AddAllocation(Buffer* resource)
    {
        m_residency_set->addAllocation(static_cast<MTL::Resource*>(resource->GetNativeHandle()));
    }

    void ResidencySet::RemoveAllocation(const Buffer* resource)
    {
        m_residency_set->removeAllocation(static_cast<MTL::Resource*>(resource->GetNativeHandle()));
    }

    void ResidencySet::RemoveAllAllocations()
    {
        m_residency_set->removeAllAllocations();
    }

    void ResidencySet::AddAllocation(const MTL::Resource* resource) const
    {
        m_residency_set->addAllocation(resource);
    }

    void ResidencySet::RemoveAllocation(const MTL::Resource* resource) const
    {
        m_residency_set->removeAllocation(resource);
    }

    void ResidencySet::Commit() const { m_residency_set->commit(); }

    void ResidencySet::RequestResidency() const { m_residency_set->requestResidency(); }

    void ResidencySet::EndResidency() const { m_residency_set->endResidency(); }

    void* ResidencySet::GetNative() const { return m_residency_set.get(); }
} // namespace RM