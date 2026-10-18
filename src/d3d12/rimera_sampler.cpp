#include "rimera_sampler.hpp"
#include "rimera_device.hpp"
#include "rimera_d3d12.hpp"

RM::Sampler::~Sampler()
{
    if (m_device && m_index != UINT32_MAX) m_device->RemoveDescriptor(this);
}

RM::Result<void> RM::Sampler::Initialize(Device* device, const SamplerCreateInfo& info)
{
    m_device = device;
    auto* native_device = static_cast<ID3D12Device15*>(device->GetNativeHandle());
    D3D12_SAMPLER_DESC desc{
        .Filter = D3D12::ToFilter(info.min_filter, info.mag_filter, info.reduction_type),
        .AddressU = D3D12::ToWrap(info.wrap_u),
        .AddressV = D3D12::ToWrap(info.wrap_v),
        .AddressW = D3D12::ToWrap(info.wrap_w),
        .MipLODBias = 0.0,
        .MaxAnisotropy = std::max(1u, info.max_anisotropy),
        .ComparisonFunc = D3D12::GetComparisonFunc(info.comparison_func),
        .MinLOD = info.min_lod,
        .MaxLOD = info.max_lod,
    };
    desc.BorderColor[0] = desc.BorderColor[1] = desc.BorderColor[2] =
        info.border_color == BorderColor::White ? 1.0f : 0.0f;
    desc.BorderColor[3] = info.border_color == BorderColor::TransparentBlack ? 0.0f : 1.0f;

    auto index_result = m_device->AddDescriptor(this);
    if (!index_result) return std::unexpected(index_result.error());
    m_index = std::move(*index_result);

    native_device->CreateSampler(&desc, m_cpu_handle);
    return {};
}
void* RM::Sampler::GetNativeHandle() const { return reinterpret_cast<void*>(m_gpu_handle.ptr); }
