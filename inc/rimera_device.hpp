#pragma once
#include "unordered_map"
#include "rimera_buffer.hpp"
#include "rimera_structs.hpp"
#include "rimera_queue.hpp"
#include "rimera_residency.hpp"
#include "rimera_sampler.hpp"
#include "rimera_shader.hpp"
#include "rimera_texture.hpp"
#include "rimera_texture_view.hpp"

#ifdef RIMERA_MAC
struct IRRootSignature;
#endif

namespace RM
{
    class Device;
    void DestroyDevice(const Device* device);

    class Device
    {
    public:
        Device(const DeviceCreateInfo& info);
        ~Device();

        [[nodiscard]] Fence* CreateFence();
        void DestroyFence(const Fence* fence);

        [[nodiscard]] Queue* CreateQueue(QueueType type);
        void DestroyQueue(const Queue* queue);

        [[nodiscard]] Command* CreateCommand(CommandType type);
        void DestroyCommand(const Command* command) const;

        [[nodiscard]] CommandRing* CreateCommandRing(QueueType type);
        void DestroyCommandRing(const CommandRing* command_ring) const;

        [[nodiscard]] ResidencySet* CreateResidencySet();
        void DestroyResidencySet(const ResidencySet* residency_set) const;

        [[nodiscard]] Pipeline* CreatePipeline(const ComputePipelineCreateInfo& info);
        [[nodiscard]] Pipeline* CreatePipeline(const GraphicsPipelineCreateInfo& info);
        void DestroyPipeline(const Pipeline* pipeline);

        [[nodiscard]] Texture* CreateTexture(const TextureCreateInfo& info);
        void DestroyTexture(const Texture* texture) const;

        [[nodiscard]] TextureView* CreateTextureView(const TextureViewCreateInfo& info);
        void DestroyTextureView(const TextureView* texture_view) const;

        [[nodiscard]] Buffer* CreateBuffer(const BufferCreateInfo& info);
        void DestroyBuffer(const Buffer* buffer);

        [[nodiscard]] Sampler* CreateSampler(const SamplerCreateInfo& info);
        void DestroySampler(const Sampler* sampler);

        [[nodiscard]] void* GetNativeRootSignature() const;

        [[nodiscard]] ResidencySet* GetCoreResidencySet() const
        {
            return m_core_residency_set;
        }

        Queue* GetGraphicsQueue() const
        {
            return m_graphics_queue;
        }

        TextureView* GetCurrentBackBuffer() const;

        void NewFrame(Queue* queue);

        void Resize(uint32_t width, uint32_t height);

        [[nodiscard]] void* GetNativeHandle() const;

    private:
        ResidencySet* m_core_residency_set;
        Queue* m_graphics_queue;

        [[nodiscard]] uint32_t AddDescriptor(const Buffer* buffer);
        [[nodiscard]] uint32_t AddDescriptor(TextureViewType view_type);
        void AddDescriptor(Sampler* sampler);
        void RemoveDescriptor(const Buffer* buffer);
        void RemoveDescriptor(const TextureView* texture);
        void RemoveDescriptor(const Sampler* sampler);

        void CreateRootSignature();
        friend class Command;
        friend class Queue;
        friend class Texture;
        friend class TextureView;
        friend class Buffer;
        friend class Sampler;

#ifdef RIMERA_MAC
        [[nodiscard]] uint32_t AddDescriptor(MTL::Texture* texture);
        [[nodiscard]] NS::SharedPtr<MTL::Buffer> GetArgBuffer() { return m_arg_buffer; }
        [[nodiscard]] NS::SharedPtr<MTL4::ArgumentTable> GetArgTable() { return m_arg_table; }
        NS::SharedPtr<MTL::Device> m_device;
        NS::SharedPtr<NS::AutoreleasePool> m_frame_pool;
        NS::SharedPtr<MTL::Buffer> m_resource_descriptor_heap;
        NS::SharedPtr<MTL::Buffer> m_sampler_descriptor_heap;
        NS::SharedPtr<MTL::Buffer> m_arg_buffer;
        NS::SharedPtr<MTL4::ArgumentTable> m_arg_table;
        NS::SharedPtr<MTL::Buffer> m_constant_arena;
        NS::SharedPtr<CA::MetalLayer> m_swap_layer;
        CA::MetalDrawable* m_drawable = nullptr;
        IRRootSignature* m_root_sig = nullptr;
        std::unordered_map<uint64_t, uint32_t> m_swapchain_descriptors{};
        uint32_t m_current_descriptor = 0;
        std::vector<uint32_t> m_free_list;
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

        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptor(
            TextureViewType type,
            uint32_t index);
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptor(
            TextureViewType type,
            uint32_t index);
        DescriptorHeap CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
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
            TextureView* swapchain_rtv;
            Fence* swapchain_fence;
            uint64_t fence_value;
        };

        std::array<FrameData, 3> m_frame_datas;
        uint32_t m_current_frame = 0;
        ComPtr<ID3D12RootSignature> m_root_sig;
        ComPtr<ID3D12InfoQueue1> m_info_queue;
        DWORD m_callback_cookie = 0;
        bool m_callback_registered = false;
#endif
    };
} // namespace RM