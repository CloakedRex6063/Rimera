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
#elifdef RIMERA_VULKAN
        static constexpr uint32_t kFramesInFlight = 3;

        [[nodiscard]] Result<void> CreateSwapchain(uint32_t width, uint32_t height);
        [[nodiscard]] Result<void> CreateTextures();
        [[nodiscard]] Result<void> CreateSemaphores();
        [[nodiscard]] Result<void> AddSubmitSync(std::vector<VkSemaphoreSubmitInfo>& waits,
                                                 std::vector<VkSemaphoreSubmitInfo>& signals);
        void DestroyTextures();
        void DestroySemaphores();
        friend class Queue;
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
        VkFormat m_native_format = VK_FORMAT_B8G8R8A8_UNORM;
        VkColorSpaceKHR m_color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        Format m_format = Format::BGRA8_UNorm;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        uint32_t m_image_index = 0;
        uint32_t m_frame = 0;
        bool m_out_of_date = false;
        bool m_acquired = false;
        bool m_acquire_pending = false;
        uint32_t m_signal_count = 0;
        VkSemaphore m_acquired_semaphore = VK_NULL_HANDLE;
        std::vector<Texture*> m_textures;
        std::array<VkSemaphore, kFramesInFlight> m_acquire_semaphores{};
        std::vector<std::vector<VkSemaphore>> m_present_semaphores;
#endif
    };
} // namespace RM
