#include "rimera_texture.hpp"

RM::Result<void> RM::Texture::ValidateView(const TextureViewCreateInfo& info) const
{
    if (!m_device || !info.texture || info.texture->m_device != m_device)
        return std::unexpected("Texture view requires a texture from the same device");

    const auto& texture = *info.texture;
    if (!info.mip_count || info.base_mip >= texture.m_mip_count ||
        info.mip_count > texture.m_mip_count - info.base_mip)
        return std::unexpected("Texture view mip range is out of bounds");
    if (!info.layer_count || info.base_layer >= texture.m_layer_count ||
        info.layer_count > texture.m_layer_count - info.base_layer)
        return std::unexpected("Texture view layer range is out of bounds");
    if (info.format != Format::Unknown && info.format != texture.m_format)
        return std::unexpected("Texture view format must match the source texture");

    const bool depth_stencil = texture.m_format == Format::Depth16_UNorm ||
                               texture.m_format == Format::Depth32_Float ||
                               texture.m_format == Format::Stencil8 ||
                               texture.m_format == Format::Depth24_UNorm_Stencil8 ||
                               texture.m_format == Format::Depth32_Float_Stencil8;
    switch (info.view_type)
    {
        case TextureViewType::ShaderRead:
            if (!texture.m_usage.contains(TextureUsage::ShaderRead))
                return std::unexpected("Texture does not support shader reads");
            break;
        case TextureViewType::ShaderReadWrite:
            if (!texture.m_usage.contains(TextureUsage::ShaderWrite) || depth_stencil)
                return std::unexpected("Texture does not support a shader read-write view");
            if (info.mip_count != 1)
                return std::unexpected("Shader read-write views require a single mip level");
            break;
        case TextureViewType::RenderTarget:
        case TextureViewType::DepthStencil:
            if (!texture.m_usage.contains(TextureUsage::RenderTarget) ||
                depth_stencil != (info.view_type == TextureViewType::DepthStencil))
                return std::unexpected("Texture does not support the requested attachment view");
            if (info.mip_count != 1)
                return std::unexpected("Attachment views require a single mip level");
            break;
        default:
            return std::unexpected("Invalid texture view type");
    }
    return {};
}
