#include "rimera_residency.hpp"
#include "rimera_device.hpp"

void RM::ResidencySet::AddNativeAllocation(void*)
{
}

void RM::ResidencySet::RemoveNativeAllocation(void*)
{
}

void RM::ResidencySet::RemoveAllNativeAllocations()
{
}

void RM::ResidencySet::CommitNative() const
{
}

void RM::ResidencySet::RequestNativeResidency() const
{
}

void RM::ResidencySet::EndNativeResidency() const
{
}

void* RM::ResidencySet::GetNative() const
{
    return nullptr;
}

RM::Result<void> RM::ResidencySet::Initialize(Device* device)
{
    m_device = device;
    return {};
}
