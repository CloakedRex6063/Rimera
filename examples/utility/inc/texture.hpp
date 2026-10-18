#pragma once
#include "rimera_texture.hpp"
#include "memory"

namespace RM { class Device; }

enum class TextureFilter : uint32_t
{
    Default = 0,
    Nearest = 9728,
    Linear = 9729,
    NearestMipmapNearest = 9984,
    LinearMipmapNearest = 9985,
    NearestMipmapLinear = 9986,
    LinearMipmapLinear = 9987,
};

enum class TextureWrap : uint32_t
{
    Repeat = 10497,
    ClampToEdge = 33071,
    MirroredRepeat = 33648,
};

struct TextureSampler
{
    TextureFilter mag_filter = TextureFilter::Default;
    TextureFilter min_filter = TextureFilter::Default;
    TextureWrap wrap_s = TextureWrap::Repeat;
    TextureWrap wrap_t = TextureWrap::Repeat;
};
class Texture
{
public:
    ~Texture();
    [[nodiscard]] static RM::Result<Texture*> Create(RM::Device* device,
                                                     const std::filesystem::path& path,
                                                     bool srgb = false,
                                                     TextureSampler sampler = {});
    [[nodiscard]] static RM::Result<Texture*> Create(RM::Device* device,
                                                     std::span<const std::byte> encoded,
                                                     bool srgb = false,
                                                     TextureSampler sampler = {});
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) = delete;
    Texture& operator=(Texture&&) = delete;
    [[nodiscard]] RM::Texture* GetTexture() const { return m_texture; }
    [[nodiscard]] uint32_t GetWidth() const { return m_width; }
    [[nodiscard]] uint32_t GetHeight() const { return m_height; }
    [[nodiscard]] const TextureSampler& GetSampler() const { return m_sampler; }
private:
    Texture() = default;
    RM::Result<void> Initialize(RM::Device* device,
                                const std::filesystem::path& path,
                                bool srgb,
                                TextureSampler sampler);
    RM::Result<void> Initialize(RM::Device* device,
                                std::span<const std::byte> encoded,
                                bool srgb,
                                TextureSampler sampler);

    RM::Result<void> Upload(RM::Device* device,
                            unsigned char* pixels,
                            int width,
                            int height,
                            bool srgb);
    RM::Device* m_device = nullptr;
    RM::Texture* m_texture = nullptr;
    uint32_t m_width{}, m_height{};
    TextureSampler m_sampler;
};
