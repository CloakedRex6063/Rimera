#pragma once
#include "unordered_map"
#include "rimera_buffer.hpp"
#include "rimera_heap.hpp"
#include "rimera_structs.hpp"
#include "rimera_queue.hpp"
#include "rimera_residency.hpp"
#include "rimera_sampler.hpp"
#include "rimera_shader.hpp"
#include "rimera_texture.hpp"

#ifdef RIMERA_MAC
struct IRRootSignature;
#endif

namespace RM
{
    class Device;
    class RenderGraph;
    [[nodiscard]] Result<Device*> CreateDevice(const DeviceCreateInfo& info);
    void DestroyDevice(const Device* device);

    class Device
    {
    public:
        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;
        ~Device();

        [[nodiscard]] Result<Fence*> CreateFence();
        void DestroyFence(const Fence* fence);

        [[nodiscard]] Result<Queue*> CreateQueue(QueueType type);
        void DestroyQueue(const Queue* queue);

        [[nodiscard]] Result<Command*> CreateCommand(CommandType type);
        void DestroyCommand(const Command* command) const;

        [[nodiscard]] Result<CommandRing*> CreateCommandRing(QueueType type);
        void DestroyCommandRing(const CommandRing* command_ring) const;

        [[nodiscard]] Result<ResidencySet*> CreateResidencySet();
        void DestroyResidencySet(const ResidencySet* residency_set) const;

        [[nodiscard]] Result<Pipeline*> CreatePipeline(const ComputePipelineCreateInfo& info);
        [[nodiscard]] Result<Pipeline*> CreatePipeline(const GraphicsPipelineCreateInfo& info);
        [[nodiscard]] Result<Pipeline*> CreatePipeline(const MeshGraphicsPipelineCreateInfo& info);
        void DestroyPipeline(const Pipeline* pipeline);

        [[nodiscard]] Result<Texture*> CreateTexture(const TextureCreateInfo& info);
        void DestroyTexture(const Texture* texture) const;

        [[nodiscard]] Result<Texture*> CreateTextureView(const TextureViewCreateInfo& info);

        [[nodiscard]] Result<Buffer*> CreateBuffer(const BufferCreateInfo& info);
        void DestroyBuffer(const Buffer* buffer);

        [[nodiscard]] Result<Sampler*> CreateSampler(const SamplerCreateInfo& info);
        void DestroySampler(const Sampler* sampler);

        [[nodiscard]] Result<Heap*> CreateHeap(const HeapCreateInfo& info);
        void DestroyHeap(const Heap* heap) const;

        [[nodiscard]] Result<ResourceAllocationInfo> GetAllocationInfo(
            const TextureCreateInfo& info,
            HeapMemory memory = HeapMemory::DeviceLocal) const;
        [[nodiscard]] Result<ResourceAllocationInfo> GetAllocationInfo(
            const BufferCreateInfo& info,
            HeapMemory memory = HeapMemory::DeviceLocal) const;

        [[nodiscard]] TextureCopyFootprint GetCopyFootprint(const Texture* texture,
                                                            uint32_t mip_level = 0) const;

        [[nodiscard]] Result<RenderGraph*> CreateRenderGraph(
            const RenderGraphCreateInfo& info = {});
        void DestroyRenderGraph(const RenderGraph* render_graph) const;

        [[nodiscard]] void* GetNativeRootSignature() const;

        [[nodiscard]] ResidencySet* GetCoreResidencySet() const
        {
            return m_core_residency_set;
        }

        Queue* GetGraphicsQueue() const
        {
            return m_graphics_queue;
        }

        Texture* GetCurrentBackBuffer();

        [[nodiscard]] Result<void> NewFrame(Queue* queue);

        [[nodiscard]] Result<void> Resize(uint32_t width, uint32_t height);

        [[nodiscard]] void* GetNativeHandle() const;

    private:
        Device() = default;
        friend Result<Device*> CreateDevice(const DeviceCreateInfo& info);
        [[nodiscard]] Result<void> Initialize(const DeviceCreateInfo& info);
        template <typename T, typename... Args>
        [[nodiscard]] static Result<T*> CreateObject(Args&&... args)
        {
            auto* object = new (std::nothrow) T();
            if (!object) return std::unexpected("Object allocation failed");
            auto result = object->Initialize(std::forward<Args>(args)...);
            if (!result)
            {
                auto error = std::move(result.error());
                delete object;
                return std::unexpected(std::move(error));
            }
            return object;
        }
        ResidencySet* m_core_residency_set = nullptr;
        Queue* m_graphics_queue = nullptr;
        Command* m_upload_command = nullptr;
        Fence* m_upload_fence = nullptr;
        uint64_t m_upload_value = 0;

        [[nodiscard]] Result<void> UploadTexture(Texture* texture, const void* data);

        [[nodiscard]] Result<uint32_t> AddDescriptor(const Buffer* buffer);
        [[nodiscard]] Result<uint32_t> AddDescriptor(Sampler* sampler);
        void RemoveDescriptor(const Buffer* buffer);
        void RemoveDescriptor(const Texture* texture);
        void RemoveDescriptor(const Sampler* sampler);

        [[nodiscard]] Result<void> CreateRootSignature();
        friend class Command;
        friend class Queue;
        friend class Texture;
        friend class Buffer;
        friend class Sampler;
        friend class Heap;

#ifdef RIMERA_MAC
        [[nodiscard]] Result<uint32_t> AddDescriptor(const Texture* texture);
        NS::SharedPtr<MTL::Device> m_device;
        NS::SharedPtr<NS::AutoreleasePool> m_frame_pool;
        NS::SharedPtr<MTL::Buffer> m_resource_descriptor_heap;
        NS::SharedPtr<MTL::Buffer> m_sampler_descriptor_heap;
        NS::SharedPtr<MTL::Buffer> m_constant_arena;
        NS::SharedPtr<CA::MetalLayer> m_swap_layer;
        CA::MetalDrawable* m_drawable = nullptr;
        IRRootSignature* m_root_sig = nullptr;
        std::unordered_map<uint64_t, Texture*> m_swapchain_textures{};
        Fence* m_frame_fence = nullptr;
        std::array<uint64_t, 3> m_frame_values{};
        uint64_t m_frame_value = 0;
        uint32_t m_current_frame = 0;
        uint32_t m_current_resource = 0;
        std::vector<uint32_t> m_free_resources;

        std::unordered_map<Format, Pipeline*> m_blit_pipelines;

        uint32_t m_current_sampler = 0;
        std::vector<uint32_t> m_free_samplers;
#elifdef RIMERA_WINDOWS
        struct DescriptorHeap
        {
            ComPtr<ID3D12DescriptorHeap> heap;
            uint32_t stride = 0;
            uint32_t index = 0;
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_base{};
            D3D12_GPU_DESCRIPTOR_HANDLE gpu_base{};
            std::vector<uint32_t> free_descriptors;

            D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(uint32_t index) const
            {
                return D3D12_CPU_DESCRIPTOR_HANDLE{.ptr = cpu_base.ptr + index * stride};
            }

            D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(uint32_t index) const
            {
                return D3D12_GPU_DESCRIPTOR_HANDLE{.ptr = gpu_base.ptr + index * stride};
            }
        };

        [[nodiscard]] Result<uint32_t> AddDescriptor(TextureViewType type);
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptor(
            TextureViewType type,
            uint32_t index);
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptor(
            TextureViewType type,
            uint32_t index);
        [[nodiscard]] Result<DescriptorHeap> CreateDescriptorHeap(
            D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
            uint32_t count) const;
        ComPtr<ID3D12Device15> m_device;
        ComPtr<IDXGIAdapter4> m_adapter;
        ComPtr<IDXGIFactory7> m_factory;
        DescriptorHeap m_rtv_heap;
        DescriptorHeap m_dsv_heap;
        DescriptorHeap m_srv_heap;
        DescriptorHeap m_sampler_heap;
        ComPtr<IDXGISwapChain4> m_swapchain;
        ComPtr<D3D12MA::Allocator> m_allocator;

        struct FrameData
        {
            Texture* swapchain_texture;
            Fence* swapchain_fence;
            uint64_t fence_value;
        };

        std::array<FrameData, 3> m_frame_datas{};
        uint32_t m_current_frame = 0;
        ComPtr<ID3D12RootSignature> m_root_sig;
        ComPtr<ID3D12InfoQueue1> m_info_queue;
        DWORD m_callback_cookie = 0;
        bool m_callback_registered = false;
#endif
    };
} // namespace RM
