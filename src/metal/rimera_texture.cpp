#include "rimera_texture.hpp"
#include "rimera_metal.hpp"
#include "stdexcept"

RM::UInt2 RM::Texture::GetDimensions() const noexcept
{
    return {.x = static_cast<uint32_t>(m_texture->width()),
            .y = static_cast<uint32_t>(m_texture->height())};
}

uint32_t RM::Texture::GetMipLevels() const noexcept
{
    return m_texture->mipmapLevelCount();
}

RM::Texture::Texture(Device* device, const TextureCreateInfo& info) : m_format(info.format),
    m_device(device)
{
    NS::SharedPtr<MTL::TextureDescriptor> descriptor =
        NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
    descriptor->setPixelFormat(Metal::ToPixelFormat(info.format));
    descriptor->setHeight(info.height);
    descriptor->setWidth(info.width);
    descriptor->setAllowGPUOptimizedContents(true);
    descriptor->setArrayLength(info.array_layers);
    descriptor->setMipmapLevelCount(info.mip_levels);
    descriptor->setTextureType(MTL::TextureType2D);
    auto usage = Metal::ToTextureUsage(info.usage);
    usage |= MTL::TextureUsagePixelFormatView;
    descriptor->setUsage(usage);
    if (info.initial_data)
    {
        if (info.array_layers != 1 || !info.width || !info.height ||
            (info.format != Format::RGBA8_UNorm && info.format != Format::RGBA8_UNorm_sRGB) ||
            static_cast<uint64_t>(info.bytes_per_row) < static_cast<uint64_t>(info.width) * 4)
            throw std::runtime_error("Invalid initial texture upload");
        descriptor->setStorageMode(MTL::StorageModeShared);
    }
    auto* native_device = static_cast<MTL::Device*>(m_device->GetNativeHandle());
    m_texture = NS::TransferPtr(native_device->newTexture(descriptor.get()));
    if (!m_texture) throw std::runtime_error("Metal texture allocation failed");
    if (info.initial_data)
        m_texture->replaceRegion(MTL::Region::Make2D(0, 0, info.width, info.height),
                                 0,
                                 info.initial_data,
                                 info.bytes_per_row);
}

RM::Texture::Texture(Device* device, const NS::SharedPtr<MTL::Texture>& texture) :
    m_format(Metal::FromPixelFormat(texture->pixelFormat())),
    m_device(device), m_texture(texture)
{
}

RM::Texture::~Texture()
{
}

void* RM::Texture::GetNativeHandle() const noexcept { return m_texture.get(); }