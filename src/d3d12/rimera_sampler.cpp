#include "rimera_sampler.hpp"
#include "rimera_device.hpp"
#include "rimera_d3d12.hpp"

RM::Sampler::~Sampler()
{
    m_device->RemoveDescriptor(this);
}

RM::Sampler::Sampler(Device* device, const SamplerCreateInfo& info) : m_device(device)
{
    auto* native_device = static_cast<ID3D12Device15*>(device->GetNativeHandle());
    D3D12_SAMPLER_DESC desc{
        .Filter = D3D12::ToFilter(info.min_filter, info.mag_filter, info.reduction_type),
        .AddressU = D3D12::ToWrap(info.wrap_u),
        .AddressV = D3D12::ToWrap(info.wrap_v),
        .AddressW = D3D12::ToWrap(info.wrap_w),
        .MipLODBias = 0.0,
        .MaxAnisotropy = info.max_anisotropy,
        .ComparisonFunc = D3D12::GetComparisonFunc(info.comparison_func),
        .MinLOD = info.min_lod,
        .MaxLOD = info.min_lod,
    };
    desc.BorderColor[0] = info.border_color.x;
    desc.BorderColor[1] = info.border_color.y;
    desc.BorderColor[2] = info.border_color.z;
    desc.BorderColor[3] = info.border_color.w;

    m_device->AddDescriptor(this);

    native_device->CreateSampler(&desc, m_cpu_handle);
}