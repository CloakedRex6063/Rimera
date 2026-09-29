#pragma once
#include "rimera_structs.hpp"

namespace RM
{
    class Device;

    class Sampler
    {
    public:
        ~Sampler();
        [[nodiscard]] uint32_t GetSamplerIndex() const { return m_index; }

    private:
        Sampler(Device* device, const SamplerCreateInfo& info);
        friend class Device;
        Device* m_device;

#ifdef RIMERA_WINDOWS
        D3D12_CPU_DESCRIPTOR_HANDLE m_cpu_handle;
        D3D12_GPU_DESCRIPTOR_HANDLE m_gpu_handle;
#endif
        uint32_t m_index;
    };
}