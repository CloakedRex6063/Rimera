#include "rimera_texture.hpp"
#include "rimera_device.hpp"
#include "rimera_heap.hpp"
#include "rimera_vulkan.hpp"

RM::Texture::ImageStorage::~ImageStorage()
{
    if (!owned || !image) return;
    if (allocation)
        vmaDestroyImage(allocator, image, allocation);
    else
        vkDestroyImage(device, image, nullptr);
}

RM::Texture::~Texture()
{
    ReleaseResidency();
    if (m_device)
    {
        if (m_attachment_view) vkDestroyImageView(m_device->m_device, m_attachment_view, nullptr);
        m_device->RemoveDescriptor(this);
    }
    if (m_heap && m_heap_allocated) m_heap->Free(m_heap_offset);
    if (m_owns_image) delete m_image;
}

void* RM::Texture::GetNativeHandle() const noexcept
{
    return m_image ? Vulkan::ToNative(m_image->image) : nullptr;
}

RM::UInt2 RM::Texture::GetDimensions() const noexcept
{
    return UInt2{.x = std::max(1u, m_image->width >> m_base_mip),
                 .y = std::max(1u, m_image->height >> m_base_mip)};
}

RM::Result<void> RM::Texture::Initialize(Device* device, const TextureCreateInfo& info)
{
    if (!device) return std::unexpected("Invalid texture creation parameters");
    if (auto result = ValidateCreateInfo(info); !result) return std::unexpected(result.error());
    m_device = device;
    if (auto result = CreateImage(info, nullptr, 0); !result) return
        std::unexpected(result.error());
    if (info.initial_data) return m_device->UploadTexture(this, info.initial_data);
    return {};
}

RM::Result<void> RM::Texture::Initialize(Device* device,
                                         Heap* heap,
                                         uint64_t offset,
                                         const TextureCreateInfo& info)
{
    if (!device || !heap) return std::unexpected("Invalid texture creation parameters");
    if (auto result = ValidateCreateInfo(info); !result) return std::unexpected(result.error());
    if (info.initial_data)
        return std::unexpected("Initial data is not supported for heap allocated textures");
    m_device = device;
    m_heap = heap;
    m_heap_offset = offset;
    return CreateImage(info, heap, offset);
}

RM::Result<void> RM::Texture::Initialize(Device* device,
                                         VkImage image,
                                         uint32_t width,
                                         uint32_t height,
                                         EnumFlags<TextureUsage> usage,
                                         Format format)
{
    m_device = device;
    m_format = format;
    m_usage = usage;
    m_image = new(std::nothrow) ImageStorage();
    if (!m_image) return std::unexpected("Object allocation failed");
    m_owns_image = true;
    m_image->device = device->m_device;
    m_image->allocator = device->m_allocator;
    m_image->image = image;
    m_image->owned = false;
    m_image->width = width;
    m_image->height = height;
    return CreateViews();
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
    m_image = info.texture->m_image;
    return CreateViews();
}

RM::Result<void> RM::Texture::CreateImage(const TextureCreateInfo& info,
                                          Heap* heap,
                                          uint64_t offset)
{
    const auto format = Vulkan::GetFormat(info.format);
    if (format == VK_FORMAT_UNDEFINED)
        return std::unexpected("Texture format is not supported by Vulkan");
    m_format = info.format;
    m_usage = info.usage;
    m_mip_count = info.mip_levels;
    m_layer_count = info.array_layers;

    m_image = new(std::nothrow) ImageStorage();
    if (!m_image) return std::unexpected("Object allocation failed");
    m_owns_image = true;
    m_image->device = m_device->m_device;
    m_image->allocator = m_device->m_allocator;
    m_image->width = info.width;
    m_image->height = info.height;
    m_image->mip_levels = info.mip_levels;
    m_image->array_layers = info.array_layers;

    const auto image_info = Vulkan::GetImageCreateInfo(info,
                                                       m_device->m_unique_queue_families,
                                                       m_device->SupportsHostImageTransfer(
                                                           info.format));
    if (heap)
    {
        if (const auto result = vmaCreateAliasingImage2(m_device->m_allocator,
                                                        heap->m_allocation,
                                                        offset,
                                                        &image_info,
                                                        &m_image->image);
            result != VK_SUCCESS)
            return std::unexpected("Failed to create placed texture, " +
                                   Vulkan::ResultMessage(result));
    }
    else
    {
        const bool high_priority =
            info.usage.intersects({TextureUsage::RenderTarget, TextureUsage::ShaderWrite});
        const VmaAllocationCreateInfo allocation_info{
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .priority = high_priority ? 1.f : 0.5f,
        };
        if (const auto result = vmaCreateImage(m_device->m_allocator,
                                               &image_info,
                                               &allocation_info,
                                               &m_image->image,
                                               &m_image->allocation,
                                               nullptr);
            result != VK_SUCCESS)
            return std::unexpected("Failed to create texture, " + Vulkan::ResultMessage(result));
    }

    if (m_device->SupportsHostImageTransfer(m_format))
    {
        const VkHostImageLayoutTransitionInfo transition{
            .sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO,
            .image = m_image->image,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .subresourceRange = {Vulkan::GetAspectMask(m_format), 0, m_mip_count, 0, m_layer_count},
        };
        if (const auto result = vkTransitionImageLayout(m_device->m_device, 1, &transition);
            result != VK_SUCCESS)
            return std::unexpected("Failed to transition texture layout, " +
                                   Vulkan::ResultMessage(result));
    }
    else if (auto result = m_device->TransitionImageLayout(m_image->image,
                                                           Vulkan::GetAspectMask(m_format),
                                                           m_mip_count,
                                                           m_layer_count,
                                                           VK_IMAGE_LAYOUT_GENERAL);
        !result)
        return std::unexpected(result.error());
    return CreateViews();
}

VkImageViewCreateInfo RM::Texture::GetViewCreateInfo(TextureViewType type) const
{
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t mip_count = 1;
    switch (type)
    {
        case TextureViewType::RenderTarget:
        case TextureViewType::DepthStencil:
            aspect = Vulkan::GetAspectMask(m_format);
            break;
        case TextureViewType::ShaderRead:
            mip_count = m_mip_count;
            if (m_format == Format::Stencil8)
                aspect = VK_IMAGE_ASPECT_STENCIL_BIT;
            else if (Vulkan::IsDepthStencil(m_format))
                aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            break;
        case TextureViewType::ShaderReadWrite:
            break;
    }
    return {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_image->image,
        .viewType = m_image->array_layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
        .format = Vulkan::GetFormat(m_format),
        .subresourceRange = {aspect, m_base_mip, mip_count, m_base_layer, m_layer_count},
    };
}

RM::Result<void> RM::Texture::CreateViews()
{
    if (m_usage.contains(TextureUsage::RenderTarget))
    {
        const auto type = Vulkan::IsDepthStencil(m_format)
                              ? TextureViewType::DepthStencil
                              : TextureViewType::RenderTarget;
        const auto view_info = GetViewCreateInfo(type);
        if (const auto result = vkCreateImageView(m_device->m_device,
                                                  &view_info,
                                                  nullptr,
                                                  &m_attachment_view);
            result != VK_SUCCESS)
            return std::unexpected("Failed to create texture view, " +
                                   Vulkan::ResultMessage(result));
    }
    for (const auto type : {TextureViewType::ShaderRead, TextureViewType::ShaderReadWrite})
    {
        const auto usage = type == TextureViewType::ShaderRead
                               ? TextureUsage::ShaderRead
                               : TextureUsage::ShaderWrite;
        if (!m_usage.contains(usage)) continue;
        auto index = m_device->AddDescriptor(this, type);
        if (!index) return std::unexpected(index.error());
        m_descriptors[static_cast<uint32_t>(type)] = *index;
    }
    return {};
}