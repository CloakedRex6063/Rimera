#include "rimera_texture_view.hpp"
#include "rimera_d3d12.hpp"
#include "rimera_device.hpp"

RM::TextureView::~TextureView()
{
}

RM::Texture* RM::TextureView::GetTexture() const noexcept
{
    return m_info.texture;
}

RM::TextureView::TextureView(Device* device, const TextureViewCreateInfo& info) : m_device(device),
    m_info(info)
{
    auto* native_device = static_cast<ID3D12Device15*>(device->GetNativeHandle());
    m_resource_index = m_device->AddDescriptor(info.view_type);
    m_cpu_handle = m_device->GetCPUDescriptor(info.view_type, m_resource_index);
    m_gpu_handle = m_device->GetGPUDescriptor(info.view_type, m_resource_index);

    if (info.format == Format::Unknown)
    {
        m_info.format = info.texture->GetFormat();
    }

    switch (m_info.view_type)
    {
        case TextureViewType::RenderTarget:
        {
            D3D12_RENDER_TARGET_VIEW_DESC rtv_desc
            {
                .Format = D3D12::GetFormat(m_info.texture->GetFormat()),
                .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
                .Texture2D = {
                    .MipSlice = 0,
                    .PlaneSlice = 0,
                }
            };
            auto* resource = static_cast<ID3D12Resource*>(m_info.texture->GetNativeHandle());
            native_device->CreateRenderTargetView(resource, &rtv_desc, m_cpu_handle);
        }
        break;
        case TextureViewType::DepthStencil:
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc
            {
                .Format = D3D12::GetFormat(m_info.texture->GetFormat()),
                .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
                .Flags = D3D12_DSV_FLAG_NONE,
                .Texture2D = {
                    .MipSlice = 0,
                }
            };
            auto* resource = static_cast<ID3D12Resource*>(m_info.texture->GetNativeHandle());
            native_device->CreateDepthStencilView(resource, &dsv_desc, m_cpu_handle);
        }
        break;
        case TextureViewType::ShaderRead:
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc
            {
                .Format = D3D12::GetFormat(m_info.texture->GetFormat()),
                .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            };
            if (m_info.layer_count > 1)
            {
                srv_desc.Texture2D = {
                    .MostDetailedMip = m_info.base_mip,
                    .MipLevels = m_info.mip_count,
                    .PlaneSlice = m_info.base_layer,
                    .ResourceMinLODClamp = 0.0f
                };
            }
            else
            {
                srv_desc.Texture2DArray = {
                    .MostDetailedMip = m_info.base_mip,
                    .MipLevels = m_info.mip_count,
                    .FirstArraySlice = m_info.base_layer,
                    .ArraySize = m_info.layer_count,
                    .ResourceMinLODClamp = 0.0f
                };
            }
            auto* resource = static_cast<ID3D12Resource*>(m_info.texture->GetNativeHandle());
            native_device->CreateShaderResourceView(resource, &srv_desc, m_cpu_handle);
        }
        break;
        case TextureViewType::ShaderReadWrite:
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc
            {
                .Format = D3D12::GetFormat(m_info.texture->GetFormat()),
                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
                .Texture2D = {
                    .MipSlice = 0,
                    .PlaneSlice = 0,
                }
            };
            auto* resource = static_cast<ID3D12Resource*>(m_info.texture->GetNativeHandle());
            native_device->CreateUnorderedAccessView(resource, nullptr, &uav_desc, m_cpu_handle);
        }
        break;
    }
}