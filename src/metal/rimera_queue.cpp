#include "rimera_queue.hpp"

#include "rimera_device.hpp"

RM::Fence::Fence(Device* device) : m_device(device)
{
    auto* native_device = static_cast<MTL::Device*>(m_device->GetNativeHandle());
    m_event = NS::TransferPtr(native_device->newSharedEvent());
}

RM::Fence::~Fence() { m_event.reset(); }

void* RM::Fence::GetNativeHandle() const { return m_event.get(); }

void RM::Fence::Wait(uint64_t value) const
{
    m_event->waitUntilSignaledValue(value, UINT64_MAX);
}

RM::Queue::Queue(Device* device, QueueType) : m_device(device)
{
    auto* native_device = static_cast<MTL::Device*>(m_device->GetNativeHandle());
    m_queue = NS::TransferPtr(native_device->newMTL4CommandQueue());
}

RM::Queue::~Queue() { WaitIdle(); }

void RM::Queue::AddResidencySet(const ResidencySet* residency_set) const
{
    m_queue->addResidencySet(static_cast<MTL::ResidencySet*>(residency_set->GetNative()));
}

void RM::Queue::RemoveResidencySet(const ResidencySet* residency_set) const
{
    m_queue->removeResidencySet(static_cast<MTL::ResidencySet*>(residency_set->GetNative()));
}

void RM::Queue::WaitIdle()
{
    auto fence = m_device->CreateFence();
    Signal(fence, 1);
}

void RM::Queue::Signal(const Fence* fence, uint64_t value) const
{
    m_queue->signalEvent(static_cast<MTL::Event*>(fence->GetNativeHandle()), value);
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

void RM::Queue::Present() const
{
    auto* drawable = static_cast<MTL::Drawable*>(m_device->m_drawable);
    m_queue->signalDrawable(drawable);
    drawable->present();
}

void RM::Queue::AddResidencySet(const MTL::ResidencySet* residency_set) const
{
    m_queue->addResidencySet(residency_set);
}

void RM::Queue::RemoveResidencySet(const MTL::ResidencySet* residency_set) const
{
    m_queue->removeResidencySet(residency_set);
}