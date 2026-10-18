#include "texture.hpp"
#include "rimera_device.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

RM::Result<Texture*> Texture::Create(RM::Device* device,
                                     const std::filesystem::path& path,
                                     bool srgb,
                                     TextureSampler sampler)
{
    auto* object = new (std::nothrow) Texture();
    if (!object) return std::unexpected("Object allocation failed");
    auto result = object->Initialize(device, path, srgb, sampler);
    if (!result)
    {
        auto error = std::move(result.error());
        delete object;
        return std::unexpected(std::move(error));
    }
    return object;
}

RM::Result<void> Texture::Initialize(RM::Device* device,
                                     const std::filesystem::path& path,
                                     bool srgb,
                                     TextureSampler sampler)
{
    m_sampler = sampler;
    int width = 0, height = 0, channels = 0;
    auto* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    return Upload(device, pixels, width, height, srgb);
}

RM::Result<Texture*> Texture::Create(RM::Device* device,
                                     std::span<const std::byte> encoded,
                                     bool srgb,
                                     TextureSampler sampler)
{
    auto* object = new (std::nothrow) Texture();
    if (!object) return std::unexpected("Object allocation failed");
    auto result = object->Initialize(device, encoded, srgb, sampler);
    if (!result)
    {
        auto error = std::move(result.error());
        delete object;
        return std::unexpected(std::move(error));
    }
    return object;
}

RM::Result<void> Texture::Initialize(RM::Device* device,
                                     std::span<const std::byte> encoded,
                                     bool srgb,
                                     TextureSampler sampler)
{
    m_sampler = sampler;
    if (encoded.size() > INT_MAX) return std::unexpected("Encoded texture exceeds stb size limit");
    int width = 0, height = 0, channels = 0;
    auto* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(encoded.data()), static_cast<int>(encoded.size()), &width, &height, &channels, 4);
    return Upload(device, pixels, width, height, srgb);
}

RM::Result<void> Texture::Upload(RM::Device* device,
                                 unsigned char* pixels,
                                 int width,
                                 int height,
                                 bool srgb)
{
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> owned(pixels, stbi_image_free);
    if (!pixels)
        return std::unexpected(std::string("Texture decode failed: ") + stbi_failure_reason());
    if (!device || width <= 0 || height <= 0 || uint64_t(width) * 4 > UINT32_MAX)
        return std::unexpected("Invalid texture dimensions or device");
    m_width = static_cast<uint32_t>(width); m_height = static_cast<uint32_t>(height);
    auto texture = device->CreateTexture(
        {.width = m_width,
         .height = m_height,
         .format = srgb ? RM::Format::RGBA8_UNorm_sRGB : RM::Format::RGBA8_UNorm,
         .usage = {RM::TextureUsage::ShaderRead},
         .initial_data = pixels,
         .bytes_per_row = m_width * 4});
    if (!texture) return std::unexpected(texture.error());
    m_device = device;
    m_texture = *texture;
    return {};
}

Texture::~Texture()
{
    if (m_texture) m_device->DestroyTexture(m_texture);
}
