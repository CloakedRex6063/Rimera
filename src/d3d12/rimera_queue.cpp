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

RM::Result<void> RM::Fence::Wait(uint64_t value) const
{
    if (value > m_fence->GetCompletedValue())
    {
        if (FAILED(m_fence->SetEventOnCompletion(value, nullptr)))
            return std::unexpected("Failed to wait for fence");
    }
    return {};
}

RM::Result<void> RM::Fence::Initialize(Device* device)
{
    m_device = device;
    auto* native_device = static_cast<ID3D12Device15*>(device->GetNativeHandle());
    if (FAILED(native_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
        return std::unexpected("Failed to create fence");
    return {};
}

RM::Queue::~Queue()
{
    m_queue.Reset();
}

void RM::Queue::AddResidencySet(const ResidencySet*) const
{
}

void RM::Queue::RemoveResidencySet(const ResidencySet*) const
{
}

RM::Result<void> RM::Queue::WaitIdle()
{
    auto result_fence = m_device->CreateFence();
    if (!result_fence) return std::unexpected(result_fence.error());
    auto* fence = std::move(*result_fence);
    auto val = ++m_value;
    auto signal = Signal(fence, val);
    auto waited = signal ? fence->Wait(val) : std::move(signal);
    m_device->DestroyFence(fence);
    return waited;
}

RM::Result<void> RM::Queue::Signal(const Fence* fence, uint64_t value) const
{
    auto* native_fence = static_cast<ID3D12Fence*>(fence->GetNativeHandle());
    if (FAILED(m_queue->Signal(native_fence, value)))
        return std::unexpected("Failed to signal fence");
    return {};
}

void RM::Queue::Submit(const Command* command) const
{
    auto* native_command = static_cast<ID3D12CommandList*>(command->GetNativeCommand());
    m_queue->ExecuteCommandLists(1, &native_command);
}

RM::Result<void> RM::Queue::Present() const
{
    if (FAILED(m_device->m_swapchain->Present(0, 0)))
        return std::unexpected("Failed to present swapchain");
    return {};
}

void* RM::Queue::GetNativeHandle() const
{
    return m_queue.Get();
}

RM::Result<void> RM::Queue::Initialize(Device* device, QueueType type)
{
    m_device = device;
    D3D12_COMMAND_QUEUE_DESC desc{
        .Type = D3D12::GetQueueType(type),
        .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0,
    };
    auto* native_device = static_cast<ID3D12Device*>(m_device->GetNativeHandle());
    if (FAILED(native_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_queue))))
        return std::unexpected("Failed to create command queue");
    return {};
}