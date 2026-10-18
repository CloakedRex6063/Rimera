#include <bit>
#include "rimera_texture.hpp"
#include "rimera_d3d12.hpp"
#include "rimera_device.hpp"

RM::Texture::~Texture()
{
    if (m_device) m_device->RemoveDescriptor(this);
}

void* RM::Texture::GetNativeHandle() const noexcept
{
    return m_texture.Get();
}

RM::UInt2 RM::Texture::GetDimensions() const noexcept
{
    auto desc = m_texture->GetDesc();
    return UInt2{.x = std::max(1u, static_cast<uint32_t>(desc.Width) >> m_base_mip),
                 .y = std::max(1u, desc.Height >> m_base_mip)};
}

RM::Result<void> RM::Texture::Initialize(Device* device, const TextureCreateInfo& info)
{
    if (!device || !info.width || !info.height || !info.mip_levels || !info.array_layers ||
        info.mip_levels > std::bit_width(std::max(info.width, info.height)) ||
        info.array_layers > UINT16_MAX || info.format == Format::Unknown)
        return std::unexpected("Invalid texture creation parameters");

    m_format = info.format;
    m_usage = info.usage;
    m_mip_count = info.mip_levels;
    m_layer_count = info.array_layers;
    m_device = device;
    D3D12_RESOURCE_FLAGS flags{};

    if (info.usage.contains(TextureUsage::RenderTarget))
    {
        flags |= info.format == Format::Depth16_UNorm || info.format == Format::Depth32_Float ||
                         info.format == Format::Depth24_UNorm_Stencil8 ||
                         info.format == Format::Depth32_Float_Stencil8
                     ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                     : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (info.usage.contains(TextureUsage::ShaderWrite))
    {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    auto resource_format = D3D12::GetFormat(info.format);
    switch (info.format)
    {
        case Format::Depth16_UNorm:
            resource_format = DXGI_FORMAT_R16_TYPELESS;
            break;
        case Format::Depth32_Float:
            resource_format = DXGI_FORMAT_R32_TYPELESS;
            break;
        case Format::Depth24_UNorm_Stencil8:
            resource_format = DXGI_FORMAT_R24G8_TYPELESS;
            break;
        case Format::Depth32_Float_Stencil8:
            resource_format = DXGI_FORMAT_R32G8X24_TYPELESS;
            break;
        default:
            break;
    }
    const D3D12_RESOURCE_DESC desc{
        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
        .Width = info.width,
        .Height = info.height,
        .DepthOrArraySize = static_cast<uint16_t>(info.array_layers),
        .MipLevels = static_cast<uint16_t>(info.mip_levels),
        .Format = resource_format,
        .SampleDesc = DXGI_SAMPLE_DESC{.Count = 1, .Quality = 0},
        .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags = flags,
    };

    constexpr D3D12MA::ALLOCATION_DESC alloc_desc = {
        .HeapType = D3D12_HEAP_TYPE_DEFAULT,
    };
    const auto result = m_device->m_allocator->CreateResource(&alloc_desc,
                                                              &desc,
                                                              D3D12_RESOURCE_STATE_COMMON,
                                                              nullptr,
                                                              &m_allocation,
                                                              IID_PPV_ARGS(&m_texture));
    if (FAILED(result)) return std::unexpected("D3D12 texture allocation failed");
    if (auto result = CreateDescriptors(); !result) return std::unexpected(result.error());
    return {};
}

RM::Result<void> RM::Texture::Initialize(Device* device,
                                         ID3D12Resource* resource,
                                         EnumFlags<TextureUsage> usage)
{
    m_format = D3D12::GetFormat(resource->GetDesc().Format);
    m_usage = usage;
    m_mip_count = resource->GetDesc().MipLevels;
    m_layer_count = resource->GetDesc().DepthOrArraySize;
    m_device = device;
    m_texture = resource;
    if (auto result = CreateDescriptors(); !result) return std::unexpected(result.error());
    return {};
}

RM::Result<void> RM::Texture::Initialize(Device* device, const TextureViewCreateInfo& info)
{
    m_device = device;
    if (auto result = ValidateView(info); !result) return std::unexpected(result.error());
    m_view_type = info.view_type;
    m_format = info.format == Format::Unknown ? info.texture->GetFormat() : info.format;
    m_usage = info.texture->GetUsage();
    m_base_mip = info.texture->m_base_mip + info.base_mip;
    m_mip_count = info.mip_count;
    m_base_layer = info.texture->m_base_layer + info.base_layer;
    m_layer_count = info.layer_count;
    m_allocation = info.texture->m_allocation;
    m_texture = info.texture->m_texture;
    if (auto result = CreateDescriptors(); !result) return std::unexpected(result.error());
    return {};
}

RM::Result<void> RM::Texture::CreateDescriptors()
{
    auto* device = static_cast<ID3D12Device15*>(m_device->GetNativeHandle());
    const bool array = m_texture->GetDesc().DepthOrArraySize > 1;
    const auto format = D3D12::GetFormat(m_format);
    auto allocate = [&](TextureViewType type) -> Result<D3D12_CPU_DESCRIPTOR_HANDLE>
    {
        auto result_index = m_device->AddDescriptor(type);
        if (!result_index) return std::unexpected(result_index.error());
        auto index = std::move(*result_index);
        m_descriptors[static_cast<uint32_t>(type)] = index;
        return m_device->GetCPUDescriptor(type, index);
    };

    if (m_usage.contains(TextureUsage::RenderTarget))
    {
        if (m_texture->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC desc{};
            desc.Format = format;
            if (array)
            {
                desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                desc.Texture2DArray.MipSlice = m_base_mip;
                desc.Texture2DArray.FirstArraySlice = m_base_layer;
                desc.Texture2DArray.ArraySize = m_layer_count;
            }
            else
            {
                desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice = m_base_mip;
            }
            auto handle = allocate(TextureViewType::DepthStencil);
            if (!handle) return std::unexpected(handle.error());
            device->CreateDepthStencilView(m_texture.Get(), &desc, *handle);
        }
        else
        {
            D3D12_RENDER_TARGET_VIEW_DESC desc{};
            desc.Format = format;
            if (array)
            {
                desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                desc.Texture2DArray.MipSlice = m_base_mip;
                desc.Texture2DArray.FirstArraySlice = m_base_layer;
                desc.Texture2DArray.ArraySize = m_layer_count;
            }
            else
            {
                desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice = m_base_mip;
            }
            auto handle = allocate(TextureViewType::RenderTarget);
            if (!handle) return std::unexpected(handle.error());
            device->CreateRenderTargetView(m_texture.Get(), &desc, *handle);
        }
    }
    if (m_usage.contains(TextureUsage::ShaderRead))
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = format;
        switch (m_format)
        {
            case Format::Depth16_UNorm:
                desc.Format = DXGI_FORMAT_R16_UNORM;
                break;
            case Format::Depth32_Float:
                desc.Format = DXGI_FORMAT_R32_FLOAT;
                break;
            case Format::Depth24_UNorm_Stencil8:
                desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
                break;
            case Format::Depth32_Float_Stencil8:
                desc.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
                break;
            default:
                break;
        }
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (array)
        {
            desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MostDetailedMip = m_base_mip;
            desc.Texture2DArray.MipLevels = m_mip_count;
            desc.Texture2DArray.FirstArraySlice = m_base_layer;
            desc.Texture2DArray.ArraySize = m_layer_count;
        }
        else
        {
            desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MostDetailedMip = m_base_mip;
            desc.Texture2D.MipLevels = m_mip_count;
        }
        auto handle = allocate(TextureViewType::ShaderRead);
        if (!handle) return std::unexpected(handle.error());
        device->CreateShaderResourceView(m_texture.Get(), &desc, *handle);
    }
    if (m_usage.contains(TextureUsage::ShaderWrite))
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.Format = format;
        if (array)
        {
            desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MipSlice = m_base_mip;
            desc.Texture2DArray.FirstArraySlice = m_base_layer;
            desc.Texture2DArray.ArraySize = m_layer_count;
        }
        else
        {
            desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MipSlice = m_base_mip;
        }
        auto handle = allocate(TextureViewType::ShaderReadWrite);
        if (!handle) return std::unexpected(handle.error());
        device->CreateUnorderedAccessView(m_texture.Get(), nullptr, &desc, *handle);
    }
    return {};
}
