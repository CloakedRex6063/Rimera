#pragma once
#include "rimera_structs.hpp"

namespace RM
{
    class Device;
    class Heap;

    class Texture
    {
    public:
        ~Texture();
        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;
        void* GetNativeHandle() const noexcept;
        [[nodiscard]] Format GetFormat() const noexcept { return m_format; }
        [[nodiscard]] EnumFlags<TextureUsage> GetUsage() const noexcept { return m_usage; }
        [[nodiscard]] UInt2 GetDimensions() const noexcept;
        [[nodiscard]] uint32_t GetMipLevels() const noexcept { return m_mip_count; }
        [[nodiscard]] uint32_t GetArrayLayers() const noexcept { return m_layer_count; }
        [[nodiscard]] uint32_t GetBaseMip() const noexcept { return m_base_mip; }
        [[nodiscard]] uint32_t GetBaseLayer() const noexcept { return m_base_layer; }
        [[nodiscard]] Heap* GetHeap() const noexcept { return m_heap; }
        [[nodiscard]] uint64_t GetHeapOffset() const noexcept { return m_heap_offset; }
        [[nodiscard]] uint32_t GetResourceIndex() const noexcept
        {
            if (m_view_type) return GetResourceIndex(*m_view_type);
            if (m_usage.contains(TextureUsage::ShaderRead))
                return GetResourceIndex(TextureViewType::ShaderRead);
            if (m_usage.contains(TextureUsage::ShaderWrite))
                return GetResourceIndex(TextureViewType::ShaderReadWrite);
            return GetResourceIndex(m_format == Format::Depth16_UNorm ||
                                            m_format == Format::Depth32_Float ||
                                            m_format == Format::Depth24_UNorm_Stencil8 ||
                                            m_format == Format::Depth32_Float_Stencil8
                                        ? TextureViewType::DepthStencil
                                        : TextureViewType::RenderTarget);
        }
        [[nodiscard]] uint32_t GetResourceIndex(TextureViewType type) const noexcept
        {
#ifdef RIMERA_MAC
            return m_resource_index;
#elifdef RIMERA_WINDOWS
            return m_descriptors[static_cast<uint32_t>(type)];
#endif
        }

    private:
        Texture() = default;
        [[nodiscard]] Result<void> ValidateView(const TextureViewCreateInfo& info) const;
        [[nodiscard]] static Result<void> ValidateCreateInfo(const TextureCreateInfo& info);
        [[nodiscard]] Result<void> Initialize(Device* device, const TextureCreateInfo& info);
        [[nodiscard]] Result<void> Initialize(Device* device, const TextureViewCreateInfo& info);
        [[nodiscard]] Result<void> Initialize(Device* device,
                                              Heap* heap,
                                              uint64_t offset,
                                              const TextureCreateInfo& info);

        std::optional<TextureViewType> m_view_type;
        Format m_format;
        EnumFlags<TextureUsage> m_usage;
        uint32_t m_base_mip = 0;
        uint32_t m_mip_count = 1;
        uint32_t m_base_layer = 0;
        uint32_t m_layer_count = 1;

        friend class Device;
        friend class Heap;
        Device* m_device = nullptr;
        Heap* m_heap = nullptr;
        uint64_t m_heap_offset = 0;
        bool m_heap_allocated = false;
#ifdef RIMERA_MAC
        [[nodiscard]] Result<void> Initialize(Device* device, MTL::Texture* texture);
        NS::SharedPtr<MTL::Texture> m_texture;
        uint32_t m_resource_index = UINT32_MAX;
#elifdef RIMERA_WINDOWS
        [[nodiscard]] Result<void> Initialize(Device* device,
                                              ID3D12Resource* resource,
                                              EnumFlags<TextureUsage> usage);
        [[nodiscard]] Result<void> CreateDescriptors();
        ComPtr<D3D12MA::Allocation> m_allocation;
        ComPtr<ID3D12Resource> m_texture;
        std::array<uint32_t, 4> m_descriptors{UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
#endif
    };
}
