#include "rimera_queue.hpp"

RM::Fence::Fence(const NS::SharedPtr<MTL::Device>& device)
{
    m_event = NS::TransferPtr(device->newSharedEvent());
}

RM::Fence::~Fence() { m_event.reset(); }

void* RM::Fence::GetNativeHandle() const { return m_event.get(); }

RM::Queue::Queue(const NS::SharedPtr<MTL::Device>& device) : m_device(device)
{
    m_queue = NS::TransferPtr(m_device->newMTL4CommandQueue());
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

void RM::Queue::WaitIdle() const
{
    const auto event = NS::TransferPtr(m_device->newSharedEvent());

    m_queue->signalEvent(event.get(), 1);
    event->waitUntilSignaledValue(1, 1000);
}

void RM::Queue::Wait(void* presentable) const
{
    m_queue->wait(static_cast<MTL::Drawable*>(presentable));
}

void RM::Queue::Wait(const Fence* fence, const uint32_t value) const
{
    m_queue->wait(static_cast<MTL::Event*>(fence->GetNativeHandle()), value);
}

void RM::Queue::Signal(const Fence* fence, const uint32_t value) const
{
    m_queue->signalEvent(static_cast<MTL::Event*>(fence->GetNativeHandle()), value);
}

void RM::Queue::Submit(const Command* command) const
{
    MTL4::CommandBuffer* buffers[] = {
        static_cast<MTL4::CommandBuffer*>(command->GetNativeCommand())};
    m_queue->commit(buffers, 1);
}

void RM::Queue::Present(void* presentable) const
{
    auto* drawable = static_cast<MTL::Drawable*>(presentable);
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
