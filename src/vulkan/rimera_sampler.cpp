#include "rimera_sampler.hpp"
#include "rimera_device.hpp"
#include "rimera_vulkan.hpp"

RM::Sampler::~Sampler()
{
    if (m_device && m_index != UINT32_MAX) m_device->RemoveDescriptor(this);
}

void* RM::Sampler::GetNativeHandle() const
{
    return nullptr;
}

RM::Result<void> RM::Sampler::Initialize(Device* device, const SamplerCreateInfo& info)
{
    m_device = device;
    const auto is_linear = [](Filter filter)
    {
        return filter == Filter::Linear || filter == Filter::LinearMipNearest ||
               filter == Filter::LinearMipLinear;
    };
    const auto is_mip_linear = [](Filter filter)
    {
        return filter == Filter::NearestMipLinear || filter == Filter::LinearMipLinear;
    };

    VkSamplerReductionModeCreateInfo reduction_info{
        VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO};
    switch (info.reduction_type)
    {
        case ReductionType::Minimum:
            reduction_info.reductionMode = VK_SAMPLER_REDUCTION_MODE_MIN;
            break;
        case ReductionType::Maximum:
            reduction_info.reductionMode = VK_SAMPLER_REDUCTION_MODE_MAX;
            break;
        default:
            reduction_info.reductionMode = VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
            break;
    }

    VkBorderColor border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    if (info.border_color == BorderColor::White) border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (info.border_color == BorderColor::TransparentBlack)
        border_color = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

    const VkSamplerCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = &reduction_info,
        .magFilter = is_linear(info.mag_filter) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
        .minFilter = is_linear(info.min_filter) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
        .mipmapMode = is_mip_linear(info.min_filter) ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                     : VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = Vulkan::GetAddressMode(info.wrap_u),
        .addressModeV = Vulkan::GetAddressMode(info.wrap_v),
        .addressModeW = Vulkan::GetAddressMode(info.wrap_w),
        .anisotropyEnable = info.max_anisotropy > 1 ? VK_TRUE : VK_FALSE,
        .maxAnisotropy = static_cast<float>(std::max(1u, info.max_anisotropy)),
        .compareEnable = info.reduction_type == ReductionType::Comparison ? VK_TRUE : VK_FALSE,
        .compareOp = Vulkan::GetCompareOp(info.comparison_func),
        .minLod = info.min_lod,
        .maxLod = info.max_lod,
        .borderColor = border_color,
    };

    auto index_result = m_device->AddDescriptor(this);
    if (!index_result) return std::unexpected(index_result.error());
    m_index = *index_result;
    m_device->WriteSamplerDescriptor(m_index, create_info);
    return {};
}
