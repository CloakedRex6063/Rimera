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
#ifdef RIMERA_METAL
        NS::SharedPtr<MTL::SharedEvent> m_event;
#elifdef RIMERA_D3D12
        ComPtr<ID3D12Fence> m_fence;
#elifdef RIMERA_VULKAN
        VkSemaphore m_semaphore = VK_NULL_HANDLE;
#endif
    };

    class Queue
    {
    public:
        Queue(const Queue&) = delete;
        Queue& operator=(const Queue&) = delete;
        ~Queue();

        void AddResidencySet(ResidencySet* residency_set);
        void RemoveResidencySet(ResidencySet* residency_set);
        [[nodiscard]] Result<void> WaitIdle();
        [[nodiscard]] Result<void> Submit(const SubmitInfo& info);
        void Submit(const Command* command);

        [[nodiscard]] const Fence* GetFence() const noexcept { return m_fence; }
        void* GetNativeHandle() const;

    private:
        Queue() = default;
#ifdef RIMERA_METAL
        void AddResidencySet(const MTL::ResidencySet* residency_set) const;
        void RemoveResidencySet(const MTL::ResidencySet* residency_set) const;
        [[nodiscard]] Result<void> SignalFence(Fence* fence, uint64_t value);
        NS::SharedPtr<MTL4::CommandQueue> m_queue;
        friend class Swapchain;
#elifdef RIMERA_D3D12
        [[nodiscard]] Result<void> SignalFence(Fence* fence, uint64_t value);
        ComPtr<ID3D12CommandQueue> m_queue;
#elifdef RIMERA_VULKAN
        friend class Swapchain;
        VkQueue m_queue = VK_NULL_HANDLE;
        uint32_t m_family = VK_QUEUE_FAMILY_IGNORED;
#endif
        friend class Device;
        friend class Resident;
        friend class ResidencySet;
        [[nodiscard]] Result<void> Initialize(Device* device, QueueType type);
        [[nodiscard]] bool IsBusySince(uint64_t value) const;
        Device* m_device = nullptr;
        Fence* m_fence = nullptr;
        uint64_t m_id = 0;
        std::vector<ResidencySet*> m_residency_sets;
    };
} // namespace RM
