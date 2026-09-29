#include "rimera_texture.hpp"
#include "rimera_metal.hpp"
#include "stdexcept"

RM::Texture::Texture(Device* device, const TextureCreateInfo& info) : m_device(device)
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
    descriptor->setUsage(Metal::ToTextureUsage(info.usage));
    if (info.initial_data)
    {
        if (info.array_layers != 1 || !info.width || !info.height ||
            (info.format != Format::RGBA8_UNorm && info.format != Format::RGBA8_UNorm_sRGB) ||
            uint64_t(info.bytes_per_row) < uint64_t(info.width) * 4)
            throw std::runtime_error("Invalid initial texture upload");
        descriptor->setStorageMode(MTL::StorageModeShared);
    }
    auto* native_device = static_cast<MTL::Device*>(m_device->GetNativeHandle());
    m_texture = NS::TransferPtr(native_device->newTexture(descriptor.get()));
    if (!m_texture) throw std::runtime_error("Metal texture allocation failed");
    if (info.initial_data)
        m_texture->replaceRegion(MTL::Region::Make2D(0, 0, info.width, info.height), 0,
                                 info.initial_data, info.bytes_per_row);
    m_resource_index = m_device->AddDescriptor(this);
}

RM::Texture::~Texture() { m_device->RemoveDescriptor(m_resource_index); }

void* RM::Texture::GetNativeHandle() const noexcept { return m_texture.get(); }