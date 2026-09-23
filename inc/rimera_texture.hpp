#pragma once
#include "rimera_structs.hpp"

namespace RM
{
    class Device;
    class Texture
    {
    public:
        ~Texture();
        [[nodiscard]] void* GetNativeHandle() const noexcept;
        [[nodiscard]] uint32_t GetResourceIndex() const noexcept { return m_resource_index; }

    private:
        Texture(Device* device, const TextureCreateInfo& info);

        uint32_t m_resource_index = UINT32_MAX;

#ifdef RIMERA_MAC
        Device* m_device;
        NS::SharedPtr<MTL::Texture> m_texture;
        friend class Device;
#endif
    };
}  // namespace RM
