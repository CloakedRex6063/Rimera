#include "rimera_texture.hpp"
#include "rimera_d3d12.hpp"
#include "rimera_device.hpp"

RM::Texture::~Texture()
{
}

void* RM::Texture::GetNativeHandle() const noexcept
{
    return m_texture.Get();
}

RM::UInt2 RM::Texture::GetDimensions() const noexcept
{
    auto desc = m_texture->GetDesc();
    return UInt2{.x = static_cast<uint32_t>(desc.Width), .y = desc.Height};
}

uint32_t RM::Texture::GetMipLevels() const
{
    return m_texture->GetDesc().MipLevels;
}

RM::Texture::Texture(Device* device, const TextureCreateInfo& info) : m_format(info.format),
    m_usage(info.usage), m_device(device)
{
    D3D12_RESOURCE_FLAGS flags{};

    if (info.usage.contains(TextureUsage::RenderTarget))
    {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (info.usage.contains(TextureUsage::ShaderWrite))
    {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    const D3D12_RESOURCE_DESC desc{
        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
        .Width = info.width,
        .Height = info.height,
        .DepthOrArraySize = static_cast<uint16_t>(info.array_layers),
        .MipLevels = static_cast<uint16_t>(info.mip_levels),
        .Format = D3D12::GetFormat(info.format),
        .SampleDesc = DXGI_SAMPLE_DESC{
            .Count = 1,
            .Quality = 0
        },
        .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags = flags,
    };

    constexpr D3D12MA::ALLOCATION_DESC alloc_desc = {
        .HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD,
    };
    m_device->m_allocator->CreateResource(&alloc_desc,
                                          &desc,
                                          D3D12_RESOURCE_STATE_COMMON,
                                          nullptr,
                                          &m_allocation,
                                          IID_PPV_ARGS(&m_texture));
}

RM::Texture::Texture(Device* device, ID3D12Resource* resource, EnumFlags<TextureUsage> usage) :
    m_format(D3D12::GetFormat(resource->GetDesc().Format)),
    m_usage(usage),
    m_device(device),
    m_texture(resource)
{
}