#pragma once
#include "rimera_structs.hpp"

namespace RM
{
    class Device;

    class TextureView
    {
    public:
        [[nodiscard]] uint32_t GetResourceIndex() const noexcept { return m_resource_index; }
        ~TextureView();
        [[nodiscard]] Texture* GetTexture() const noexcept;
        [[nodiscard]] Format GetFormat() const noexcept { return m_info.format; }
        [[nodiscard]] void* GetView() const noexcept;

    private:
        friend class Device;
        TextureView(Device* device, const TextureViewCreateInfo& info);
        Device* m_device;
        TextureViewCreateInfo m_info;
        uint32_t m_resource_index = UINT32_MAX;
#ifdef RIMERA_WINDOWS
        D3D12_CPU_DESCRIPTOR_HANDLE m_cpu_handle;
        D3D12_GPU_DESCRIPTOR_HANDLE m_gpu_handle;
#elifdef RIMERA_MAC
        TextureView(Device* device, MTL::Texture* drawable);
        Texture* m_texture = nullptr;
        bool m_owns_texture = false;
        NS::SharedPtr<MTL::Texture> m_view = nullptr;
#endif
    };
}