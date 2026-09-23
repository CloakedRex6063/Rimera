#include "texture.hpp"
#include "rimera_device.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "stdexcept"

void TextureDeleter::operator()(const RM::Texture* texture) const { if (texture) device->DestroyTexture(texture); }
Texture::Texture(RM::Device* device, const std::filesystem::path& path, bool srgb, TextureSampler sampler) : m_sampler(sampler)
{
    int width = 0, height = 0, channels = 0;
    auto* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    Initialize(device, pixels, width, height, srgb);
}
Texture::Texture(RM::Device* device, std::span<const std::byte> encoded, bool srgb, TextureSampler sampler) : m_sampler(sampler)
{
    if (encoded.size() > INT_MAX) throw std::runtime_error("Encoded texture exceeds stb size limit");
    int width = 0, height = 0, channels = 0;
    auto* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(encoded.data()), static_cast<int>(encoded.size()), &width, &height, &channels, 4);
    Initialize(device, pixels, width, height, srgb);
}
void Texture::Initialize(RM::Device* device, unsigned char* pixels, int width, int height, bool srgb)
{
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> owned(pixels, stbi_image_free);
    if (!pixels) throw std::runtime_error(std::string("Texture decode failed: ") + stbi_failure_reason());
    if (!device || width <= 0 || height <= 0 || uint64_t(width) * 4 > UINT32_MAX)
        throw std::runtime_error("Invalid texture dimensions or device");
    m_width = static_cast<uint32_t>(width); m_height = static_cast<uint32_t>(height);
    m_texture = decltype(m_texture)(device->CreateTexture({.width = m_width, .height = m_height,
        .format = srgb ? RM::Format::RGBA8_UNorm_sRGB : RM::Format::RGBA8_UNorm,
        .usage = {RM::TextureUsage::ShaderRead}, .initial_data = pixels, .bytes_per_row = m_width * 4}), TextureDeleter{device});
}
