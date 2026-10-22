#pragma once
#include "rimera_residency.hpp"
#include "rimera_command.hpp"

namespace RM
{
    class Fence
    {
    public:
        Fence(const Fence&) = delete;
        Fence& operator=(const Fence&) = delete;
        ~Fence();
        [[nodiscard]] void* GetNativeHandle() const;
        [[nodiscard]] uint64_t GetCompletedValue() const;
        [[nodiscard]] uint64_t GetSignaledValue() const noexcept { return m_value; }
        [[nodiscard]] bool IsComplete(uint64_t value) const { return GetCompletedValue() >= value; }
        [[nodiscard]] Result<void> Wait(uint64_t value) const;
        [[nodiscard]] Result<void> Wait() const { return Wait(m_value); }

    private:
        Fence() = default;
        friend class Device;
        friend class Queue;
        Device* m_device = nullptr;
        uint64_t m_value = 0;
        [[nodiscard]] Result<void> Initialize(Device* device);
#ifdef RIMERA_MAC
        NS::SharedPtr<MTL::SharedEvent> m_event;
#elifdef RIMERA_WINDOWS
        ComPtr<ID3D12Fence> m_fence;
#endif
    };

    class Queue
    {
    public:
        Queue(const Queue&) = delete;
        Queue& operator=(const Queue&) = delete;
        ~Queue();

        void AddResidencySet(ResidencySet* residency_set) const;
        void RemoveResidencySet(ResidencySet* residency_set) const;
        [[nodiscard]] Result<void> WaitIdle() const;
        [[nodiscard]] Result<void> Signal(Fence* fence, uint64_t value) const;
        [[nodiscard]] Result<uint64_t> Signal(Fence* fence) const;
        void Submit(const Command* command) const;

        [[nodiscard]] const Fence* GetFence() const noexcept { return m_fence; }
        void* GetNativeHandle() const;

    private:
        Queue() = default;
#ifdef RIMERA_MAC
        void AddResidencySet(const MTL::ResidencySet* residency_set) const;
        void RemoveResidencySet(const MTL::ResidencySet* residency_set) const;
        NS::SharedPtr<MTL4::CommandQueue> m_queue;
        friend class Swapchain;
#elifdef RIMERA_WINDOWS
        ComPtr<ID3D12CommandQueue> m_queue;
#endif
        friend class Device;
        friend class Resident;
        friend class ResidencySet;
        [[nodiscard]] Result<void> Initialize(Device* device, QueueType type);
        [[nodiscard]] bool IsBusySince(uint64_t value) const;
        Device* m_device = nullptr;
        Fence* m_fence = nullptr;
        uint64_t m_id = 0;
    };
} // namespace RM
