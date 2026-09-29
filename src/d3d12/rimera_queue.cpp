#include "rimera_queue.hpp"
#include "rimera_d3d12.hpp"
#include "rimera_device.hpp"

RM::Fence::~Fence()
{
    m_fence.Reset();
}

void* RM::Fence::GetNativeHandle() const
{
    return m_fence.Get();
}

void RM::Fence::Wait(uint64_t value) const
{
    if (value > m_fence->GetCompletedValue())
    {
        m_fence->SetEventOnCompletion(value, nullptr);
    }
}

RM::Fence::Fence(Device* device) : m_device(device)
{
    auto* native_device = static_cast<ID3D12Device15*>(device->GetNativeHandle());
    native_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
}

RM::Queue::~Queue()
{
    WaitIdle();
    m_queue.Reset();
}

void RM::Queue::AddResidencySet(const ResidencySet*) const
{
}

void RM::Queue::RemoveResidencySet(const ResidencySet*) const
{
}

void RM::Queue::WaitIdle()
{
    auto* fence = m_device->CreateFence();
    auto val = ++m_value;
    Signal(fence, val);
    fence->Wait(val);
    m_device->DestroyFence(fence);
}

void RM::Queue::Signal(const Fence* fence, uint64_t value) const
{
    auto* native_fence = static_cast<ID3D12Fence*>(fence->GetNativeHandle());
    m_queue->Signal(native_fence, value);
}

void RM::Queue::Submit(const Command* command) const
{
    auto* native_command = static_cast<ID3D12CommandList*>(command->GetNativeCommand());
    m_queue->ExecuteCommandLists(1, &native_command);
}

void RM::Queue::Present() const
{
    auto& frame_data = m_device->m_frame_datas[m_device->m_current_frame];
    Signal(frame_data.swapchain_fence, ++frame_data.fence_value);
    m_device->m_swapchain->Present(0, 0);
    m_device->m_current_frame = (m_device->m_current_frame + 1) % 3;
}

void* RM::Queue::GetNativeHandle() const
{
    return m_queue.Get();
}

RM::Queue::Queue(Device* device, QueueType type) : m_device(device)
{
    D3D12_COMMAND_QUEUE_DESC desc{
        .Type = D3D12::GetQueueType(type),
        .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0,
    };
    auto* native_device = static_cast<ID3D12Device*>(m_device->GetNativeHandle());
    native_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_queue));
}