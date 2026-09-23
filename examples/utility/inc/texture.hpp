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
struct TextureDeleter
{
    RM::Device* device{};
    void operator()(const RM::Texture* texture) const;
};
class Texture
{
public:
    Texture(RM::Device* device, const std::filesystem::path& path, bool srgb = false, TextureSampler sampler = {});
    Texture(RM::Device* device, std::span<const std::byte> encoded, bool srgb = false, TextureSampler sampler = {});
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) noexcept = default;
    Texture& operator=(Texture&&) noexcept = default;
    [[nodiscard]] RM::Texture* GetTexture() const { return m_texture.get(); }
    [[nodiscard]] uint32_t GetWidth() const { return m_width; }
    [[nodiscard]] uint32_t GetHeight() const { return m_height; }
    [[nodiscard]] const TextureSampler& GetSampler() const { return m_sampler; }
private:
    void Initialize(RM::Device* device, unsigned char* pixels, int width, int height, bool srgb);
    std::unique_ptr<RM::Texture, TextureDeleter> m_texture;
    uint32_t m_width{}, m_height{};
    TextureSampler m_sampler;
};
