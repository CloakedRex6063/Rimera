#include "rimera_swapchain.hpp"
#include "rimera_d3d12.hpp"
#include "rimera_device.hpp"

static void SetSwapchainColorSpace(IDXGISwapChain4* swapchain, bool hdr)
{
    const auto color_space =
        hdr ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    UINT support = 0;
    if (SUCCEEDED(swapchain->CheckColorSpaceSupport(color_space, &support)) &&
        (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
        swapchain->SetColorSpace1(color_space);
}

RM::Result<void> RM::Swapchain::Initialize(Device* device,
                                           Queue* queue,
                                           const SwapchainCreateInfo& info)
{
    if (!device || !queue || !info.window || !info.width || !info.height)
        return std::unexpected("Invalid swapchain creation parameters");
    m_device = device;
    m_queue = queue;
    m_window = info.window;

    const bool hdr = QueryDisplayInfo().hdr;
    SelectFormat(hdr);
    DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {
        .Width = info.width,
        .Height = info.height,
        .Format = m_native_format,
        .Stereo = false,
        .SampleDesc = DXGI_SAMPLE_DESC{
            .Count = 1,
            .Quality = 0,
        },
        .BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = static_cast<UINT>(m_textures.size()),
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
        .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING,
    };
    ComPtr<IDXGISwapChain1> swapchain;
    const auto result = m_device->m_factory->CreateSwapChainForHwnd(
        static_cast<ID3D12CommandQueue*>(m_queue->GetNativeHandle()),
        static_cast<HWND>(info.window),
        &swapchain_desc,
        nullptr,
        nullptr,
        swapchain.GetAddressOf());
    if (FAILED(result))
        return std::unexpected("Failed to create swapchain, " + D3D12::HResultMessage(result));
    if (FAILED(swapchain.As(&m_swapchain)))
        return std::unexpected("Failed to obtain swapchain interface");
    SetSwapchainColorSpace(m_swapchain.Get(), hdr);
    return CreateTextures();
}

RM::Swapchain::~Swapchain()
{
    if (m_queue) static_cast<void>(m_queue->WaitIdle());
    DestroyTextures();
}

RM::Result<void> RM::Swapchain::CreateTextures()
{
    for (uint32_t i = 0; i < m_textures.size(); i++)
    {
        ID3D12Resource* swapchain_texture = nullptr;
        if (FAILED(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&swapchain_texture))))
            return std::unexpected("Failed to get swapchain buffer");
        auto texture = Device::CreateObject<Texture>(m_device,
                                                     swapchain_texture,
                                                     TextureUsage::RenderTarget,
                                                     m_format);
        swapchain_texture->Release();
        if (!texture) return std::unexpected(texture.error());
        m_textures[i] = *texture;
        m_device->GetCoreResidencySet()->AddAllocation(m_textures[i]);
    }
    return {};
}

void RM::Swapchain::DestroyTextures()
{
    for (auto*& texture : m_textures)
    {
        if (texture)
        {
            m_device->GetCoreResidencySet()->RemoveAllocation(texture);
            m_device->DestroyTexture(texture);
        }
        texture = nullptr;
    }
}

void RM::Swapchain::SelectFormat(bool hdr)
{
    m_native_format = hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
    m_format = hdr ? Format::RGBA16_Float : Format::BGRA8_UNorm;
}

RM::Result<void> RM::Swapchain::Acquire()
{
    return {};
}

RM::Result<void> RM::Swapchain::Present()
{
    if (FAILED(m_swapchain->Present(0, 0))) return std::unexpected("Failed to present swapchain");
    return {};
}

RM::Result<void> RM::Swapchain::Resize(uint32_t width, uint32_t height)
{
    if (!width || !height) return {};
    if (auto result = m_queue->WaitIdle(); !result) return std::unexpected(result.error());
    DestroyTextures();
    const bool hdr = QueryDisplayInfo().hdr;
    SelectFormat(hdr);
    if (FAILED(m_swapchain->ResizeBuffers(static_cast<UINT>(m_textures.size()),
                                          width,
                                          height,
                                          m_native_format,
                                          DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)))
        return std::unexpected("Failed to resize swapchain");
    SetSwapchainColorSpace(m_swapchain.Get(), hdr);
    return CreateTextures();
}

RM::Texture* RM::Swapchain::GetCurrentBackBuffer() const
{
    return m_textures[m_swapchain->GetCurrentBackBufferIndex()];
}

RM::DisplayInfo RM::Swapchain::GetDisplayInfo() const
{
    if (m_native_format != DXGI_FORMAT_R16G16B16A16_FLOAT) return {};
    return QueryDisplayInfo();
}

RM::DisplayInfo RM::Swapchain::QueryDisplayInfo() const
{
    const HMONITOR monitor =
        MonitorFromWindow(static_cast<HWND>(m_window), MONITOR_DEFAULTTONEAREST);
    ComPtr<IDXGIOutput6> output6;
    ComPtr<IDXGIOutput> output;
    for (UINT index = 0;
         m_device->m_adapter->EnumOutputs(index, output.ReleaseAndGetAddressOf()) !=
         DXGI_ERROR_NOT_FOUND;
         ++index)
    {
        DXGI_OUTPUT_DESC output_desc{};
        if (FAILED(output->GetDesc(&output_desc)) || output_desc.Monitor != monitor) continue;
        if (FAILED(output.As(&output6))) return {};
        break;
    }
    if (!output6) return {};

    DXGI_OUTPUT_DESC1 desc{};
    if (FAILED(output6->GetDesc1(&desc))) return {};
    if (desc.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) return {};
    const float sdr_white = D3D12::GetSdrWhiteLevel(desc.DeviceName);
    return {.hdr = true,
            .headroom = std::max(1.f, desc.MaxLuminance / sdr_white),
            .sdr_white_scale = sdr_white / 80.f};
}
