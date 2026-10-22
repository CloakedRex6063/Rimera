#include "rimera_residency.hpp"
#include "rimera_device.hpp"

RM::ResidencySet::~ResidencySet()
{
    RemoveAllAllocations();
}

void RM::ResidencySet::AddNativeAllocation(void* native)
{
    m_resources.push_back(static_cast<ID3D12Pageable*>(native));
}

void RM::ResidencySet::RemoveNativeAllocation(void* native)
{
    const auto it = std::ranges::find(m_resources, static_cast<ID3D12Pageable*>(native));
    if (it != m_resources.end()) m_resources.erase(it);
}

void RM::ResidencySet::RemoveAllNativeAllocations()
{
    m_resources.clear();
}

void RM::ResidencySet::Commit() const
{
}

void RM::ResidencySet::RequestResidency() const
{
    auto* native_device = static_cast<ID3D12Device15*>(m_device->GetNativeHandle());
    native_device->MakeResident(static_cast<uint32_t>(m_resources.size()), m_resources.data());
}

void RM::ResidencySet::EndResidency() const
{
    auto* native_device = static_cast<ID3D12Device15*>(m_device->GetNativeHandle());
    native_device->Evict(static_cast<uint32_t>(m_resources.size()), m_resources.data());
}

void* RM::ResidencySet::GetNative() const
{
    return nullptr;
}

RM::Result<void> RM::ResidencySet::Initialize(Device* device)
{
    m_device = device;
    m_resources.reserve(100);
    return {};
}
