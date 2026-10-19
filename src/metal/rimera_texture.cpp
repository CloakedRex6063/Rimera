#include "rimera_texture.hpp"
#include "rimera_metal.hpp"
#include "rimera_device.hpp"
#include "rimera_heap.hpp"

RM::UInt2 RM::Texture::GetDimensions() const noexcept
{
    return {.x = static_cast<uint32_t>(m_texture->width()),
            .y = static_cast<uint32_t>(m_texture->height())};
}

RM::Result<void> RM::Texture::Initialize(Device* device, const TextureCreateInfo& info)
{
    if (!device) return std::unexpected("Invalid texture creation parameters");
    if (auto result = ValidateCreateInfo(info); !result) return std::unexpected(result.error());

    m_format = info.format;
    m_usage = info.usage;
    m_mip_count = info.mip_levels;
    m_layer_count = info.array_layers;
    m_device = device;
    auto descriptor = Metal::CreateTextureDescriptor(info);
    if (info.initial_data)
    {
        if (info.array_layers != 1 || !info.width || !info.height ||
            (info.format != Format::RGBA8_UNorm && info.format != Format::RGBA8_UNorm_sRGB) ||
            static_cast<uint64_t>(info.bytes_per_row) < static_cast<uint64_t>(info.width) * 4)
            return std::unexpected("Invalid initial texture upload");
        descriptor->setStorageMode(MTL::StorageModeShared);
    }
    auto* native_device = static_cast<MTL::Device*>(m_device->GetNativeHandle());
    m_texture = NS::TransferPtr(native_device->newTexture(descriptor.get()));
    if (!m_texture) return std::unexpected("Metal texture allocation failed");
    if (info.initial_data)
        m_texture->replaceRegion(MTL::Region::Make2D(0, 0, info.width, info.height),
                                 0,
                                 info.initial_data,
                                 info.bytes_per_row);
    auto resource_index_result = m_device->AddDescriptor(this);
    if (!resource_index_result) return std::unexpected(resource_index_result.error());
    m_resource_index = std::move(*resource_index_result);
    return {};
}

RM::Result<void> RM::Texture::Initialize(Device* device,
                                         Heap* heap,
                                         uint64_t offset,
                                         const TextureCreateInfo& info)
{
    if (!device || !heap) return std::unexpected("Invalid texture creation parameters");
    if (auto result = ValidateCreateInfo(info); !result) return std::unexpected(result.error());
    if (info.initial_data)
        return std::unexpected("Initial data is not supported for heap allocated textures");

    m_format = info.format;
    m_usage = info.usage;
    m_mip_count = info.mip_levels;
    m_layer_count = info.array_layers;
    m_device = device;
    m_heap = heap;
    m_heap_offset = offset;

    auto* native_heap = static_cast<MTL::Heap*>(heap->GetNativeHandle());
    auto descriptor = Metal::CreateTextureDescriptor(info);
    descriptor->setResourceOptions(native_heap->resourceOptions());
    m_texture = NS::TransferPtr(native_heap->newTexture(descriptor.get(), offset));
    if (!m_texture) return std::unexpected("Metal placed texture creation failed");
    auto resource_index_result = m_device->AddDescriptor(this);
    if (!resource_index_result) return std::unexpected(resource_index_result.error());
    m_resource_index = std::move(*resource_index_result);
    return {};
}

RM::Result<void> RM::Texture::Initialize(Device* device, MTL::Texture* texture)
{
    m_format = Metal::FromPixelFormat(texture->pixelFormat());
    m_usage = TextureUsage::RenderTarget;
    m_mip_count = texture->mipmapLevelCount();
    m_layer_count = texture->arrayLength();
    m_device = device;
    m_texture = NS::RetainPtr(texture);
    auto resource_index_result = m_device->AddDescriptor(this);
    if (!resource_index_result) return std::unexpected(resource_index_result.error());
    m_resource_index = std::move(*resource_index_result);
    return {};
}

RM::Result<void> RM::Texture::Initialize(Device* device, const TextureViewCreateInfo& info)
{
    m_device = device;
    if (auto result = ValidateView(info); !result) return std::unexpected(result.error());
    m_view_type = info.view_type;
    m_format = info.format == Format::Unknown ? info.texture->GetFormat() : info.format;
    m_usage = info.texture->GetUsage();
    m_mip_count = info.mip_count;
    m_layer_count = info.layer_count;
    auto descriptor = NS::TransferPtr(MTL::TextureViewDescriptor::alloc()->init());
    descriptor->setPixelFormat(Metal::ToPixelFormat(m_format));
    descriptor->setLevelRange(NS::Range(info.base_mip, info.mip_count));
    descriptor->setSliceRange(NS::Range(info.base_layer, info.layer_count));
    descriptor->setTextureType(info.layer_count > 1 ? MTL::TextureType2DArray : MTL::TextureType2D);
    m_texture = NS::TransferPtr(info.texture->m_texture->newTextureView(descriptor.get()));
    if (!m_texture) return std::unexpected("Metal texture view creation failed");
    auto resource_index_result = m_device->AddDescriptor(this);
    if (!resource_index_result) return std::unexpected(resource_index_result.error());
    m_resource_index = std::move(*resource_index_result);
    return {};
}

RM::Texture::~Texture()
{
    if (m_device && m_resource_index != UINT32_MAX) m_device->RemoveDescriptor(this);
    if (m_heap && m_heap_allocated) m_heap->Free(m_heap_offset);
}

void* RM::Texture::GetNativeHandle() const noexcept { return m_texture.get(); }
