#include "rimera_queue.hpp"

#include "rimera_device.hpp"

RM::Result<void> RM::Fence::Initialize(Device* device)
{
    m_device = device;
    auto* native_device = static_cast<MTL::Device*>(m_device->GetNativeHandle());
    m_event = NS::TransferPtr(native_device->newSharedEvent());
    if (!m_event) return std::unexpected("Failed to create shared event");
    return {};
}

RM::Fence::~Fence() { m_event.reset(); }

void* RM::Fence::GetNativeHandle() const { return m_event.get(); }

uint64_t RM::Fence::GetCompletedValue() const { return m_event->signaledValue(); }

RM::Result<void> RM::Fence::Wait(uint64_t value) const
{
    if (!m_event->waitUntilSignaledValue(value, UINT64_MAX))
        return std::unexpected("Failed to wait for fence");
    return {};
}

RM::Result<void> RM::Queue::Initialize(Device* device, QueueType)
{
    m_device = device;
    auto* native_device = static_cast<MTL::Device*>(m_device->GetNativeHandle());
    m_queue = NS::TransferPtr(native_device->newMTL4CommandQueue());
    if (!m_queue) return std::unexpected("Failed to create command queue");
    auto fence = m_device->CreateFence();
    if (!fence) return std::unexpected(fence.error());
    m_fence = *fence;
    return {};
}

RM::Queue::~Queue()
{
#ifdef RIMERA_DEBUG
    if (m_device) m_device->m_queues.erase(m_id);
#endif
    while (!m_residency_sets.empty()) RemoveResidencySet(m_residency_sets.back());
    if (m_fence) m_device->DestroyImmediate(m_fence);
}

void RM::Queue::AddResidencySet(ResidencySet* residency_set)
{
    residency_set->TrackQueue(this);
    m_queue->addResidencySet(static_cast<MTL::ResidencySet*>(residency_set->GetNative()));
}

void RM::Queue::RemoveResidencySet(ResidencySet* residency_set)
{
    residency_set->UntrackQueue(this);
    m_queue->removeResidencySet(static_cast<MTL::ResidencySet*>(residency_set->GetNative()));
}

RM::Result<void> RM::Queue::SignalFence(Fence* fence, uint64_t value)
{
    m_queue->signalEvent(static_cast<MTL::Event*>(fence->GetNativeHandle()), value);
    fence->m_value = std::max(fence->m_value, value);
    return {};
}

void* RM::Queue::GetNativeHandle() const
{
    return m_queue.get();
}

RM::Result<void> RM::Queue::Submit(const SubmitInfo& info)
{
    for (auto* residency_set : m_residency_sets) residency_set->Commit();
    for (const auto& wait : info.waits)
        m_queue->wait(static_cast<MTL::Event*>(wait.fence->GetNativeHandle()), wait.value);
    std::vector<MTL4::CommandBuffer*> buffers;
    buffers.reserve(info.commands.size());
    for (const auto* command : info.commands)
        buffers.push_back(static_cast<MTL4::CommandBuffer*>(command->GetNativeCommand()));
    if (!buffers.empty()) m_queue->commit(buffers.data(), buffers.size());
    for (const auto& signal : info.signals)
    {
        if (auto result = SignalFence(signal.fence, signal.value); !result) return result;
    }
    return SignalFence(m_fence, m_fence->m_value + 1);
}

void RM::Queue::AddResidencySet(const MTL::ResidencySet* residency_set) const
{
    m_queue->addResidencySet(residency_set);
}

void RM::Queue::RemoveResidencySet(const MTL::ResidencySet* residency_set) const
{
    m_queue->removeResidencySet(residency_set);
}
