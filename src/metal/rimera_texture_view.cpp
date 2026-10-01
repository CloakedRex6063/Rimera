#include "rimera_texture_view.hpp"
#include "rimera_device.hpp"
#include "rimera_metal.hpp"

RM::TextureView::~TextureView()
{
    m_device->RemoveDescriptor(this);
    if (m_owns_texture)
    {
        delete m_texture;
    }
}

RM::Texture* RM::TextureView::GetTexture() const noexcept
{
    return m_texture;
}

void* RM::TextureView::GetView() const noexcept
{
    return m_view.get();
}

RM::TextureView::TextureView(Device* device, const TextureViewCreateInfo& info) : m_device(device),
    m_info(info)
{
    auto* texture = static_cast<MTL::Texture*>(info.texture->GetNativeHandle());
    m_texture = info.texture;
    m_info = info;
    if (m_info.format == Format::Unknown)
    {
        m_info.format = info.texture->GetFormat();
    }

    auto descriptor = NS::TransferPtr(MTL::TextureViewDescriptor::alloc()->init());
    descriptor->setPixelFormat(Metal::ToPixelFormat(m_info.format));
    descriptor->setLevelRange(NS::Range(info.base_mip, info.mip_count));
    descriptor->setSliceRange(NS::Range(info.base_layer, info.layer_count));

    if (info.layer_count > 1)
    {
        descriptor->setTextureType(MTL::TextureType2DArray);
    }
    else
    {
        descriptor->setTextureType(MTL::TextureType2D);
    }
    m_view = NS::TransferPtr(texture->newTextureView(descriptor.get()));
    m_resource_index = m_device->AddDescriptor(this);
}

RM::TextureView::TextureView(Device* device, MTL::Texture* drawable) : m_device(device),
    m_owns_texture(true),
    m_view(NS::TransferPtr(drawable))
{
    m_texture = new Texture(device, m_view);
    m_info = {
        .texture = m_texture,
        .view_type = TextureViewType::RenderTarget,
        .format = Metal::FromPixelFormat(drawable->pixelFormat()),
        .base_mip = 0,
        .mip_count = 1,
        .base_layer = 0,
        .layer_count = 1
    };
    m_resource_index = m_device->AddDescriptor(this);
}
