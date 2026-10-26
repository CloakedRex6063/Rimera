#include "rimera_swapchain.hpp"
#include "rimera_device.hpp"
#include "rimera_vulkan.hpp"

RM::Result<void> RM::Swapchain::Initialize(Device* device,
                                           Queue* queue,
                                           const SwapchainCreateInfo& info)
{
    if (!device || !queue || !info.window || !info.width || !info.height)
        return std::unexpected("Invalid swapchain creation parameters");
    m_device = device;
    m_queue = queue;
    m_window = info.window;

    auto surface = Vulkan::CreateSurface(device->m_instance, info);
    if (!surface) return std::unexpected(surface.error());
    m_surface = *surface;

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device->m_physical_device,
                                         queue->m_family,
                                         m_surface,
                                         &supported);
    if (!supported) return std::unexpected("The queue cannot present to this surface");
    return CreateSwapchain(info.width, info.height);
}

RM::Swapchain::~Swapchain()
{
    if (!m_device) return;
    if (m_queue) static_cast<void>(m_queue->WaitIdle());
    DestroyTextures();
    DestroySemaphores();
    if (m_swapchain) vkDestroySwapchainKHR(m_device->m_device, m_swapchain, nullptr);
    if (m_surface) vkDestroySurfaceKHR(m_device->m_instance, m_surface, nullptr);
}

RM::Result<void> RM::Swapchain::CreateSwapchain(uint32_t width, uint32_t height)
{
    if (m_swapchain)
    {
        if (auto result = m_queue->WaitIdle(); !result) return std::unexpected(result.error());
    }
    DestroyTextures();
    DestroySemaphores();
    m_acquired = false;
    m_acquire_pending = false;
    m_signal_count = 0;

    const auto physical_device = m_device->m_physical_device;
    VkSurfaceCapabilitiesKHR capabilities;
    if (const auto result =
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, m_surface, &capabilities);
        result != VK_SUCCESS)
        return std::unexpected("Failed to query surface capabilities, " +
                               Vulkan::ResultMessage(result));

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, m_surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, m_surface, &format_count, formats.data());
    const auto has_format = [&](VkFormat format, VkColorSpaceKHR color_space)
    {
        return std::ranges::any_of(formats,
                                   [&](const VkSurfaceFormatKHR& candidate)
                                   {
                                       return candidate.format == format &&
                                              candidate.colorSpace == color_space;
                                   });
    };
    const auto display = Vulkan::QueryDisplayHdr(m_window);
    m_color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    if (display.info.hdr && m_device->m_swapchain_colorspace &&
        has_format(VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT))
    {
        m_native_format = VK_FORMAT_R16G16B16A16_SFLOAT;
        m_format = Format::RGBA16_Float;
        m_color_space = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
    }
    else if (has_format(VK_FORMAT_B8G8R8A8_UNORM, m_color_space))
    {
        m_native_format = VK_FORMAT_B8G8R8A8_UNORM;
        m_format = Format::BGRA8_UNorm;
    }
    else if (has_format(VK_FORMAT_R8G8B8A8_UNORM, m_color_space))
    {
        m_native_format = VK_FORMAT_R8G8B8A8_UNORM;
        m_format = Format::RGBA8_UNorm;
    }
    else
    {
        return std::unexpected("Surface does not support a compatible swapchain format");
    }

    uint32_t mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, m_surface, &mode_count, nullptr);
    std::vector<VkPresentModeKHR> modes(mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, m_surface, &mode_count, modes.data());
    const auto present_mode = std::ranges::contains(modes, VK_PRESENT_MODE_IMMEDIATE_KHR)
                                  ? VK_PRESENT_MODE_IMMEDIATE_KHR
                                  : VK_PRESENT_MODE_FIFO_KHR;

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX)
    {
        extent.width = std::clamp(width,
                                  capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
        extent.height = std::clamp(height,
                                   capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
    }
    if (!extent.width || !extent.height)
        return std::unexpected("Swapchain surface has no area");

    uint32_t image_count = std::max(kFramesInFlight, capabilities.minImageCount);
    if (capabilities.maxImageCount) image_count = std::min(image_count, capabilities.maxImageCount);

    const VkCompositeAlphaFlagBitsKHR composite_alpha =
        capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
            : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    const VkSwapchainKHR old_swapchain = m_swapchain;
    const VkSwapchainCreateInfoKHR swapchain_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = m_surface,
        .minImageCount = image_count,
        .imageFormat = m_native_format,
        .imageColorSpace = m_color_space,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = composite_alpha,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = old_swapchain,
    };
    if (const auto result =
            vkCreateSwapchainKHR(m_device->m_device, &swapchain_info, nullptr, &m_swapchain);
        result != VK_SUCCESS)
    {
        m_swapchain = old_swapchain;
        return std::unexpected("Failed to create swapchain, " + Vulkan::ResultMessage(result));
    }
    if (old_swapchain) vkDestroySwapchainKHR(m_device->m_device, old_swapchain, nullptr);
    if (m_color_space == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT && m_device->m_hdr_metadata)
        vkSetHdrMetadataEXT(m_device->m_device, 1, &m_swapchain, &display.metadata);

    m_width = extent.width;
    m_height = extent.height;
    m_out_of_date = false;
    m_frame = 0;
    m_image_index = 0;
    if (auto result = CreateTextures(); !result) return result;
    return CreateSemaphores();
}

RM::Result<void> RM::Swapchain::CreateTextures()
{
    uint32_t image_count = 0;
    vkGetSwapchainImagesKHR(m_device->m_device, m_swapchain, &image_count, nullptr);
    std::vector<VkImage> images(image_count);
    vkGetSwapchainImagesKHR(m_device->m_device, m_swapchain, &image_count, images.data());
    for (const auto image : images)
    {
        auto texture = Device::CreateObject<Texture>(m_device,
                                                     image,
                                                     m_width,
                                                     m_height,
                                                     TextureUsage::RenderTarget,
                                                     m_format);
        if (!texture) return std::unexpected(texture.error());
        m_textures.push_back(*texture);
        m_device->GetCoreResidencySet()->AddAllocation(*texture);
    }
    return {};
}

RM::Result<void> RM::Swapchain::CreateSemaphores()
{
    const VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (auto& semaphore : m_acquire_semaphores)
    {
        if (vkCreateSemaphore(m_device->m_device, &semaphore_info, nullptr, &semaphore) !=
            VK_SUCCESS)
            return std::unexpected("Failed to create acquire semaphore");
    }
    m_present_semaphores.resize(m_textures.size());
    return {};
}

void RM::Swapchain::DestroyTextures()
{
    for (auto* texture : m_textures) m_device->DestroyImmediate(texture);
    m_textures.clear();
}

void RM::Swapchain::DestroySemaphores()
{
    for (auto& semaphore : m_acquire_semaphores)
    {
        if (semaphore) vkDestroySemaphore(m_device->m_device, semaphore, nullptr);
        semaphore = VK_NULL_HANDLE;
    }
    for (const auto& image_semaphores : m_present_semaphores)
    {
        for (const auto semaphore : image_semaphores)
            vkDestroySemaphore(m_device->m_device, semaphore, nullptr);
    }
    m_present_semaphores.clear();
}

RM::Result<void> RM::Swapchain::Acquire()
{
    if (m_out_of_date)
    {
        if (auto result = CreateSwapchain(m_width, m_height); !result) return result;
    }
    VkResult result = VK_SUCCESS;
    for (uint32_t attempt = 0; attempt < 2; ++attempt)
    {
        result = vkAcquireNextImageKHR(m_device->m_device,
                                       m_swapchain,
                                       UINT64_MAX,
                                       m_acquire_semaphores[m_frame],
                                       VK_NULL_HANDLE,
                                       &m_image_index);
        if (result != VK_ERROR_OUT_OF_DATE_KHR) break;
        if (auto recreated = CreateSwapchain(m_width, m_height); !recreated) return recreated;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        return std::unexpected("Failed to acquire swapchain image, " +
                               Vulkan::ResultMessage(result));
    m_acquired_semaphore = m_acquire_semaphores[m_frame];
    m_acquired = true;
    m_acquire_pending = true;
    m_signal_count = 0;
    m_frame = (m_frame + 1) % kFramesInFlight;
    return {};
}

RM::Result<void> RM::Swapchain::Present()
{
    if (!m_acquired) return std::unexpected("Swapchain must be acquired before presenting");
    if (!m_signal_count)
        return std::unexpected("Swapchain must be listed in a Submit between Acquire and Present");
    const VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = m_signal_count,
        .pWaitSemaphores = m_present_semaphores[m_image_index].data(),
        .swapchainCount = 1,
        .pSwapchains = &m_swapchain,
        .pImageIndices = &m_image_index,
    };
    const auto result = vkQueuePresentKHR(m_queue->m_queue, &present_info);
    m_acquired = false;
    m_signal_count = 0;
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        m_out_of_date = true;
        return {};
    }
    if (result != VK_SUCCESS)
        return std::unexpected("Failed to present swapchain, " + Vulkan::ResultMessage(result));
    return {};
}

RM::Result<void> RM::Swapchain::Resize(uint32_t width, uint32_t height)
{
    if (!width || !height) return {};
    return CreateSwapchain(width, height);
}

RM::Texture* RM::Swapchain::GetCurrentBackBuffer() const
{
    return m_textures[m_image_index];
}

RM::DisplayInfo RM::Swapchain::GetDisplayInfo() const
{
    if (m_color_space != VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT) return {};
    return Vulkan::QueryDisplayHdr(m_window).info;
}

RM::Result<void> RM::Swapchain::AddSubmitSync(std::vector<VkSemaphoreSubmitInfo>& waits,
                                              std::vector<VkSemaphoreSubmitInfo>& signals)
{
    if (!m_acquired) return std::unexpected("Swapchain must be acquired before it is submitted");
    if (m_acquire_pending)
    {
        waits.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                         .semaphore = m_acquired_semaphore,
                         .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT});
        m_acquire_pending = false;
    }
    auto& image_semaphores = m_present_semaphores[m_image_index];
    if (image_semaphores.size() <= m_signal_count)
    {
        const VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (vkCreateSemaphore(m_device->m_device, &semaphore_info, nullptr, &semaphore) !=
            VK_SUCCESS)
            return std::unexpected("Failed to create present semaphore");
        image_semaphores.push_back(semaphore);
    }
    signals.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                       .semaphore = image_semaphores[m_signal_count],
                       .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT});
    ++m_signal_count;
    return {};
}
