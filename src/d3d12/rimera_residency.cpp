#include "rimera_residency.hpp"
#include "rimera_device.hpp"

void RM::ResidencySet::AddNativeAllocation(void* native)
{
    auto* pageable = static_cast<ID3D12Pageable*>(native);
    m_resources.push_back(pageable);
    if (!m_resident) return;
    auto* native_device = static_cast<ID3D12Device15*>(m_device->GetNativeHandle());
    native_device->MakeResident(1, &pageable);
}

void RM::ResidencySet::RemoveNativeAllocation(void* native)
{
    auto* pageable = static_cast<ID3D12Pageable*>(native);
    const auto it = std::ranges::find(m_resources, pageable);
    if (it == m_resources.end()) return;
    m_resources.erase(it);
    if (!m_resident) return;
    auto* native_device = static_cast<ID3D12Device15*>(m_device->GetNativeHandle());
    native_device->Evict(1, &pageable);
}

void RM::ResidencySet::RemoveAllNativeAllocations()
{
    if (m_resident) EndNativeResidency();
    m_resources.clear();
}

void RM::ResidencySet::CommitNative() const
{
}

void RM::ResidencySet::RequestNativeResidency() const
{
    if (m_resident) return;
    auto* native_device = static_cast<ID3D12Device15*>(m_device->GetNativeHandle());
    native_device->MakeResident(static_cast<uint32_t>(m_resources.size()), m_resources.data());
}

void RM::ResidencySet::EndNativeResidency() const
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
