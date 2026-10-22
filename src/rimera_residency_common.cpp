#include "rimera_residency.hpp"
#include "rimera_buffer.hpp"
#include "rimera_device.hpp"
#include "rimera_heap.hpp"
#include "rimera_texture.hpp"

void RM::Resident::ReleaseResidency()
{
    while (!m_residency_sets.empty())
    {
        auto* residency_set = m_residency_sets.back();
        residency_set->RemoveResident(this);
        residency_set->Commit();
    }
}

#ifdef RIMERA_DEBUG
void RM::Resident::TrackQueue(const Queue* queue)
{
    if (std::ranges::contains(m_queue_uses, queue->m_id, &QueueUse::queue)) return;
    m_queue_uses.push_back({.queue = queue->m_id, .value = queue->m_fence->GetSignaledValue()});
}

void RM::Resident::ValidateIdle(std::string_view type) const
{
    if (!m_device) return;
    for (const auto& use : m_queue_uses)
    {
        const auto* queue = m_device->FindQueue(use.queue);
        if (!queue || !queue->IsBusySince(use.value)) continue;
        std::fprintf(stderr,
                     "[Rimera] Error: %.*s destroyed while still in use by a queue. Prefer "
                     "Device::Retire, or wait for the GPU work to finish before calling "
                     "DestroyImmediate\n",
                     static_cast<int>(type.size()),
                     type.data());
        return;
    }
}
#endif

void RM::ResidencySet::AddAllocation(Texture* resource)
{
    AddResident(resource, resource->GetNativeHandle());
}

void RM::ResidencySet::RemoveAllocation(Texture* resource)
{
    RemoveResident(resource);
}

void RM::ResidencySet::AddAllocation(Buffer* resource)
{
    AddResident(resource, resource->GetNativeHandle());
}

void RM::ResidencySet::RemoveAllocation(Buffer* resource)
{
    RemoveResident(resource);
}

void RM::ResidencySet::AddAllocation(Heap* heap)
{
    AddResident(heap, heap->GetNativeHandle());
}

void RM::ResidencySet::RemoveAllocation(Heap* heap)
{
    RemoveResident(heap);
}

void RM::ResidencySet::RemoveAllAllocations()
{
    for (const auto& [resident, native] : m_residents) std::erase(resident->m_residency_sets, this);
    m_residents.clear();
    RemoveAllNativeAllocations();
}

void RM::ResidencySet::AddResident(Resident* resident, void* native)
{
    if (!m_residents.emplace(resident, native).second) return;
    resident->m_residency_sets.push_back(this);
    AddNativeAllocation(native);
#ifdef RIMERA_DEBUG
    for (const auto id : m_queues)
        if (const auto* queue = m_device->FindQueue(id)) resident->TrackQueue(queue);
#endif
}

void RM::ResidencySet::RemoveResident(Resident* resident)
{
    const auto it = m_residents.find(resident);
    if (it == m_residents.end()) return;
    RemoveNativeAllocation(it->second);
    std::erase(resident->m_residency_sets, this);
    m_residents.erase(it);
}

#ifdef RIMERA_DEBUG
void RM::ResidencySet::TrackQueue(const Queue* queue)
{
    if (std::ranges::contains(m_queues, queue->m_id)) return;
    m_queues.push_back(queue->m_id);
    for (const auto& [resident, native] : m_residents) resident->TrackQueue(queue);
}

void RM::ResidencySet::UntrackQueue(const Queue* queue)
{
    std::erase(m_queues, queue->m_id);
}
#endif
