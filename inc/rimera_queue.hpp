#pragma once
#include "rimera_residency.hpp"
#include "rimera_command.hpp"

namespace RM
{
    class Fence
    {
    public:
        ~Fence();
        [[nodiscard]] void* GetNativeHandle() const;
        void Wait(uint64_t value) const;

    private:
        friend class Device;
        Device* m_device;
#ifdef RIMERA_MAC
        Fence(const NS::SharedPtr<MTL::Device>& device);
        NS::SharedPtr<MTL::SharedEvent> m_event;
#elifdef RIMERA_WINDOWS
        Fence(Device* device);
        ComPtr<ID3D12Fence> m_fence;
#endif
    };

    class Queue
    {
    public:
        ~Queue();

        void AddResidencySet(const ResidencySet* residency_set) const;
        void RemoveResidencySet(const ResidencySet* residency_set) const;
        void WaitIdle();
        void Signal(const Fence* fence, uint64_t value) const;
        void Submit(const Command* command) const;
        void Present() const;

        void* GetNativeHandle() const;

    private:
#ifdef RIMERA_MAC
        void AddResidencySet(const MTL::ResidencySet* residency_set) const;
        void RemoveResidencySet(const MTL::ResidencySet* residency_set) const;
        NS::SharedPtr<MTL4::CommandQueue> m_queue;
        friend class Device;
#elifdef RIMERA_WINDOWS
        ComPtr<ID3D12CommandQueue> m_queue;
#endif
        friend class Device;
        Queue(Device* device, QueueType type);
        Device* m_device;
        uint64_t m_value = 0;
    };
} // namespace RM