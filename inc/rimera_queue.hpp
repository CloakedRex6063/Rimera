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
        [[nodiscard]] Result<void> Wait(uint64_t value) const;

    private:
        Fence() = default;
        friend class Device;
        Device* m_device = nullptr;
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

        void AddResidencySet(const ResidencySet* residency_set) const;
        void RemoveResidencySet(const ResidencySet* residency_set) const;
        [[nodiscard]] Result<void> WaitIdle();
        [[nodiscard]] Result<void> Signal(const Fence* fence, uint64_t value) const;
        void Submit(const Command* command) const;

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
        [[nodiscard]] Result<void> Initialize(Device* device, QueueType type);
        Device* m_device = nullptr;
        uint64_t m_value = 0;
    };
} // namespace RM