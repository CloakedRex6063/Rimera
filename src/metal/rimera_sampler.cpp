#include "rimera_sampler.hpp"
#include "rimera_device.hpp"
#include "rimera_metal.hpp"

RM::Sampler::~Sampler()
{
    if (m_device && m_index != UINT32_MAX) m_device->RemoveDescriptor(this);
}

void* RM::Sampler::GetNativeHandle() const
{
    return m_sampler.get();
}

RM::Result<void> RM::Sampler::Initialize(Device* device, const SamplerCreateInfo& info)
{
    m_device = device;
    auto* native_device = static_cast<MTL::Device*>(device->GetNativeHandle());
    auto descriptor = NS::TransferPtr(MTL::SamplerDescriptor::alloc()->init());
    descriptor->setMinFilter(Metal::ToSamplerMinMagFilter(info.min_filter));
    descriptor->setMagFilter(Metal::ToSamplerMinMagFilter(info.mag_filter));
    descriptor->setMipFilter(Metal::ToSamplerMipFilter(info.min_filter));
    descriptor->setSAddressMode(Metal::ToSamplerAddressMode(info.wrap_u));
    descriptor->setTAddressMode(Metal::ToSamplerAddressMode(info.wrap_v));
    descriptor->setRAddressMode(Metal::ToSamplerAddressMode(info.wrap_w));
    descriptor->setLodMinClamp(info.min_lod);
    descriptor->setLodMaxClamp(info.max_lod);
    descriptor->setMaxAnisotropy(info.max_anisotropy == 0 ? 1 : info.max_anisotropy);
    descriptor->setBorderColor(Metal::ToSamplerBorderColor(info.border_color));
    descriptor->setCompareFunction(info.reduction_type == ReductionType::Comparison
                                       ? Metal::ToCompareFunction(info.comparison_func)
                                       : MTL::CompareFunctionNever);
    descriptor->setReductionMode(Metal::ToSamplerReductionMode(info.reduction_type));
    m_sampler = NS::TransferPtr(native_device->newSamplerState(descriptor.get()));
    if (!m_sampler) return std::unexpected("Failed to create sampler");
    auto index_result = m_device->AddDescriptor(this);
    if (!index_result) return std::unexpected(index_result.error());
    m_index = std::move(*index_result);
    return {};
}