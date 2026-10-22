#include "rimera_swapchain.hpp"
#include "rimera_device.hpp"
#include "rimera_metal.hpp"

RM::Result<void> RM::Swapchain::Initialize(Device* device,
                                           Queue* queue,
                                           const SwapchainCreateInfo& info)
{
    if (!device || !queue || !info.window || !info.width || !info.height)
        return std::unexpected("Invalid swapchain creation parameters");
    m_device = device;
    m_queue = queue;
    m_window = info.window;
    m_swap_layer = Metal::CreateSwapLayer(device->m_device, info);
    if (!m_swap_layer) return std::unexpected("Failed to create swapchain layer");
    m_queue->AddResidencySet(m_swap_layer->residencySet());
    return {};
}

RM::Swapchain::~Swapchain()
{
    if (!m_swap_layer) return;
    static_cast<void>(m_queue->WaitIdle());
    DestroyTextures();
    m_queue->RemoveResidencySet(m_swap_layer->residencySet());
}

void RM::Swapchain::DestroyTextures()
{
    for (auto [id, texture] : m_textures) m_device->DestroyImmediate(texture);
    m_textures.clear();
    m_drawable = nullptr;
}

RM::Result<void> RM::Swapchain::Acquire()
{
    m_drawable = m_swap_layer->nextDrawable();
    if (!m_drawable) return std::unexpected("Failed to acquire a swapchain drawable");

    auto* texture = m_drawable->texture();
    const auto resource_id = texture->gpuResourceID()._impl;
    if (!m_textures.contains(resource_id))
    {
        auto result = Device::CreateObject<Texture>(m_device, texture);
        if (!result) return std::unexpected(result.error());
        m_textures.emplace(resource_id, *result);
    }

    auto* native_queue = static_cast<MTL4::CommandQueue*>(m_queue->GetNativeHandle());
    native_queue->wait(m_drawable);
    return {};
}

RM::Result<void> RM::Swapchain::Present()
{
    if (!m_drawable) return std::unexpected("No drawable is available");
    auto* native_queue = static_cast<MTL4::CommandQueue*>(m_queue->GetNativeHandle());
    native_queue->signalDrawable(m_drawable);
    m_drawable->present();
    m_drawable = nullptr;
    return {};
}

RM::Result<void> RM::Swapchain::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) return {};

    const auto current_size = m_swap_layer->drawableSize();
    if (current_size.width == width && current_size.height == height) return {};

    if (auto result = m_queue->WaitIdle(); !result) return std::unexpected(result.error());
    DestroyTextures();

    m_queue->RemoveResidencySet(m_swap_layer->residencySet());
    Metal::ConfigureSwapLayer(m_swap_layer.get(), Metal::SupportsHDR(m_window));
    Metal::UpdateSwapLayerScale(m_swap_layer.get(), m_window);
    m_swap_layer->setDrawableSize(CGSizeMake(width, height));
    m_queue->AddResidencySet(m_swap_layer->residencySet());
    return {};
}

RM::Texture* RM::Swapchain::GetCurrentBackBuffer() const
{
    if (!m_drawable) return nullptr;
    const auto it = m_textures.find(m_drawable->texture()->gpuResourceID()._impl);
    return it == m_textures.end() ? nullptr : it->second;
}

RM::DisplayInfo RM::Swapchain::GetDisplayInfo() const
{
    if (m_swap_layer->pixelFormat() != MTL::PixelFormat::PixelFormatRGBA16Float) return {};
    return Metal::GetDisplayInfo(m_window);
}
