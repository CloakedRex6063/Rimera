#pragma once
#include "rimera_structs.hpp"

namespace RM
{
    class Device;

    class Texture
    {
    public:
        ~Texture();
        void* GetNativeHandle() const noexcept;
        [[nodiscard]] Format GetFormat() const noexcept { return m_format; }
        [[nodiscard]] EnumFlags<TextureUsage> GetUsage() const noexcept { return m_usage; }
        [[nodiscard]] UInt2 GetDimensions() const noexcept;
        [[nodiscard]] uint32_t GetMipLevels() const;

    private:
        Texture(Device* device, const TextureCreateInfo& info);

        Format m_format;
        EnumFlags<TextureUsage> m_usage;

        friend class Device;
        Device* m_device;
#ifdef RIMERA_MAC
        NS::SharedPtr<MTL::Texture> m_texture;
#elifdef RIMERA_WINDOWS
        Texture(Device* device, ID3D12Resource* resource, EnumFlags<TextureUsage> usage);
        ComPtr<ID3D12Resource> m_texture;
        ComPtr<D3D12MA::Allocation> m_allocation = nullptr;
#endif
    };
} // namespace RM