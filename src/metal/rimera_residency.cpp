#include "rimera_residency.hpp"

#include "rimera_buffer.hpp"
#include "rimera_texture.hpp"

namespace RM
{
    ResidencySet::ResidencySet(const NS::SharedPtr<MTL::Device>& device)
    {
        auto* descriptor = MTL::ResidencySetDescriptor::alloc()->init();
        NS::Error* error;
        m_residency_set = NS::TransferPtr(device->newResidencySet(descriptor, &error));
        descriptor->release();
    }

    ResidencySet::~ResidencySet() { m_residency_set->removeAllAllocations(); }

    void ResidencySet::AddAllocation(const Texture* resource)
    {
        m_residency_set->addAllocation(static_cast<MTL::Resource*>(resource->GetNativeHandle()));
    }
    void ResidencySet::RemoveAllocation(const Texture* resource)
    {
        m_residency_set->removeAllocation(static_cast<MTL::Resource*>(resource->GetNativeHandle()));
    }

    void ResidencySet::AddAllocation(const Buffer* resource)
    {
        m_residency_set->addAllocation(static_cast<MTL::Resource*>(resource->GetNativeHandle()));
    }
    void ResidencySet::RemoveAllocation(const Buffer* resource)
    {
        m_residency_set->removeAllocation(static_cast<MTL::Resource*>(resource->GetNativeHandle()));
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
}  // namespace RM
