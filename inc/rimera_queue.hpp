#pragma once
#include "rimera_residency.hpp"
#include "rimera_command.hpp"

namespace RM
{
    class Fence
    {
    public:
        Fence(const NS::SharedPtr<MTL::Device>& device);
        ~Fence();

        [[nodiscard]] void* GetNativeHandle() const;

    private:
#ifdef RIMERA_MAC
        NS::SharedPtr<MTL::SharedEvent> m_event;
#endif
    };

    class Queue
    {
    public:
#ifdef RIMERA_MAC
        Queue(const NS::SharedPtr<MTL::Device>& device);
#endif
        ~Queue();

        void AddResidencySet(const ResidencySet* residency_set) const;
        void RemoveResidencySet(const ResidencySet* residency_set) const;
        void WaitIdle() const;
        void Wait(void* presentable) const;
        void Wait(const Fence* fence, uint32_t value) const;
        void Signal(const Fence* fence, uint32_t value) const;
        void Submit(const Command* command) const;
        void Present(void* presentable) const;

    private:
#ifdef RIMERA_MAC
        void AddResidencySet(const MTL::ResidencySet* residency_set) const;
        void RemoveResidencySet(const MTL::ResidencySet* residency_set) const;
        NS::SharedPtr<MTL4::CommandQueue> m_queue;
        NS::SharedPtr<MTL::Device> m_device;
        friend class Device;
#endif
    };
}  // namespace RM