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

uint64_t RM::Fence::GetCompletedValue() const
{
    return m_fence->GetCompletedValue();
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
#ifdef RIMERA_DEBUG
    if (m_device) m_device->m_queues.erase(m_id);
#endif
    while (!m_residency_sets.empty()) RemoveResidencySet(m_residency_sets.back());
    if (m_fence) m_device->DestroyImmediate(m_fence);
    m_queue.Reset();
}

void RM::Queue::AddResidencySet(ResidencySet* residency_set)
{
    residency_set->TrackQueue(this);
}

void RM::Queue::RemoveResidencySet(ResidencySet* residency_set)
{
    residency_set->UntrackQueue(this);
}

RM::Result<void> RM::Queue::SignalFence(Fence* fence, uint64_t value)
{
    auto* native_fence = static_cast<ID3D12Fence*>(fence->GetNativeHandle());
    if (FAILED(m_queue->Signal(native_fence, value)))
        return std::unexpected("Failed to signal fence");
    fence->m_value = std::max(fence->m_value, value);
    return {};
}

RM::Result<void> RM::Queue::Submit(const SubmitInfo& info)
{
    for (auto* residency_set : m_residency_sets) residency_set->Commit();
    for (const auto& wait : info.waits)
    {
        auto* native_fence = static_cast<ID3D12Fence*>(wait.fence->GetNativeHandle());
        if (FAILED(m_queue->Wait(native_fence, wait.value)))
            return std::unexpected("Failed to wait for fence on queue");
    }
    std::vector<ID3D12CommandList*> lists;
    lists.reserve(info.commands.size());
    for (const auto* command : info.commands)
        lists.push_back(static_cast<ID3D12CommandList*>(command->GetNativeCommand()));
    if (!lists.empty()) m_queue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
    for (const auto& signal : info.signals)
    {
        if (auto result = SignalFence(signal.fence, signal.value); !result) return result;
    }
    return SignalFence(m_fence, m_fence->m_value + 1);
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
    auto fence = m_device->CreateFence();
    if (!fence) return std::unexpected(fence.error());
    m_fence = *fence;
    return {};
}
