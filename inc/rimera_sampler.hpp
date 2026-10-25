#pragma once
#include "rimera_structs.hpp"

namespace RM
{
    class Device;

    class Sampler
    {
    public:
        Sampler(const Sampler&) = delete;
        Sampler& operator=(const Sampler&) = delete;
        ~Sampler();
        [[nodiscard]] uint32_t GetSamplerIndex() const { return m_index; }
        [[nodiscard]] void* GetNativeHandle() const;

    private:
        Sampler() = default;
        [[nodiscard]] Result<void> Initialize(Device* device, const SamplerCreateInfo& info);
        friend class Device;
        Device* m_device = nullptr;

#ifdef RIMERA_D3D12
        D3D12_CPU_DESCRIPTOR_HANDLE m_cpu_handle;
        D3D12_GPU_DESCRIPTOR_HANDLE m_gpu_handle;
#elifdef RIMERA_METAL
        NS::SharedPtr<MTL::SamplerState> m_sampler;
#endif
        uint32_t m_index = UINT32_MAX;
    };
}