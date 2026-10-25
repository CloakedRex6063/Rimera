#pragma once
#include "unordered_map"
#include "rimera_structs.hpp"

namespace RM
{
    class Device;
    class Queue;
    class Texture;

    class Swapchain
    {
    public:
        Swapchain(const Swapchain&) = delete;
        Swapchain& operator=(const Swapchain&) = delete;
        ~Swapchain();

        [[nodiscard]] Result<void> Acquire();
        [[nodiscard]] Result<void> Present();
        [[nodiscard]] Result<void> Resize(uint32_t width, uint32_t height);
        [[nodiscard]] Texture* GetCurrentBackBuffer() const;
        [[nodiscard]] DisplayInfo GetDisplayInfo() const;
        [[nodiscard]] Queue* GetQueue() const noexcept { return m_queue; }

    private:
        Swapchain() = default;
        friend class Device;
        [[nodiscard]] Result<void> Initialize(Device* device,
                                              Queue* queue,
                                              const SwapchainCreateInfo& info);
        Device* m_device = nullptr;
        Queue* m_queue = nullptr;
        void* m_window = nullptr;
#ifdef RIMERA_METAL
        void DestroyTextures();
        NS::SharedPtr<CA::MetalLayer> m_swap_layer;
        CA::MetalDrawable* m_drawable = nullptr;
        std::unordered_map<uint64_t, Texture*> m_textures;
#elifdef RIMERA_D3D12
        [[nodiscard]] DisplayInfo QueryDisplayInfo() const;
        [[nodiscard]] Result<void> CreateTextures();
        void DestroyTextures();
        void SelectFormat(bool hdr);
        ComPtr<IDXGISwapChain4> m_swapchain;
        DXGI_FORMAT m_native_format = DXGI_FORMAT_B8G8R8A8_UNORM;
        Format m_format = Format::BGRA8_UNorm;
        std::array<Texture*, 3> m_textures{};
#endif
    };
} // namespace RM
