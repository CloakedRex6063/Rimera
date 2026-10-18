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
    return {};
}

RM::Queue::~Queue() = default;

void RM::Queue::AddResidencySet(const ResidencySet* residency_set) const
{
    m_queue->addResidencySet(static_cast<MTL::ResidencySet*>(residency_set->GetNative()));
}

void RM::Queue::RemoveResidencySet(const ResidencySet* residency_set) const
{
    m_queue->removeResidencySet(static_cast<MTL::ResidencySet*>(residency_set->GetNative()));
}

RM::Result<void> RM::Queue::WaitIdle()
{
    auto result_fence = m_device->CreateFence();
    if (!result_fence) return std::unexpected(result_fence.error());
    auto fence = std::move(*result_fence);
    auto signal = Signal(fence, 1);
    auto waited = signal ? fence->Wait(1) : std::move(signal);
    m_device->DestroyFence(fence);
    return waited;
}

RM::Result<void> RM::Queue::Signal(const Fence* fence, uint64_t value) const
{
    m_queue->signalEvent(static_cast<MTL::Event*>(fence->GetNativeHandle()), value);
    return {};
}

void* RM::Queue::GetNativeHandle() const
{
    return m_queue.get();
}

void RM::Queue::Submit(const Command* command) const
{
    MTL4::CommandBuffer* buffers[] = {
        static_cast<MTL4::CommandBuffer*>(command->GetNativeCommand())};
    m_queue->commit(buffers, 1);
}

RM::Result<void> RM::Queue::Present() const
{
    if (!m_device->m_drawable) return std::unexpected("No drawable is available");
    auto* drawable = static_cast<MTL::Drawable*>(m_device->m_drawable);
    m_queue->signalDrawable(drawable);
    drawable->present();
    return {};
}

void RM::Queue::AddResidencySet(const MTL::ResidencySet* residency_set) const
{
    m_queue->addResidencySet(residency_set);
}

void RM::Queue::RemoveResidencySet(const MTL::ResidencySet* residency_set) const
{
    m_queue->removeResidencySet(residency_set);
}