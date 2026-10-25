#pragma once
#include "unordered_map"
#include "unordered_set"
#include "rimera_buffer.hpp"
#include "rimera_heap.hpp"
#include "rimera_structs.hpp"
#include "rimera_queue.hpp"
#include "rimera_residency.hpp"
#include "rimera_sampler.hpp"
#include "rimera_shader.hpp"
#include "rimera_swapchain.hpp"
#include "rimera_texture.hpp"

#ifdef RIMERA_METAL
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
        void DestroyImmediate(const Fence* fence);

        [[nodiscard]] Result<Queue*> CreateQueue(QueueType type);
        void DestroyImmediate(const Queue* queue);

        [[nodiscard]] Result<Swapchain*> CreateSwapchain(Queue* queue,
                                                         const SwapchainCreateInfo& info);
        void DestroyImmediate(const Swapchain* swapchain) const;

        [[nodiscard]] Result<Command*> CreateCommand(CommandType type);
        void DestroyImmediate(const Command* command) const;

        [[nodiscard]] Result<CommandRing*> CreateCommandRing(QueueType type);
        void DestroyImmediate(const CommandRing* command_ring) const;

        [[nodiscard]] Result<ResidencySet*> CreateResidencySet();
        void DestroyImmediate(const ResidencySet* residency_set) const;

        [[nodiscard]] Result<Pipeline*> CreatePipeline(const ComputePipelineCreateInfo& info);
        [[nodiscard]] Result<Pipeline*> CreatePipeline(const GraphicsPipelineCreateInfo& info);
        [[nodiscard]] Result<Pipeline*> CreatePipeline(const MeshGraphicsPipelineCreateInfo& info);
        void DestroyImmediate(const Pipeline* pipeline) const;

        [[nodiscard]] Result<Texture*> CreateTexture(const TextureCreateInfo& info);
        void DestroyImmediate(const Texture* texture) const;

        [[nodiscard]] Result<Texture*> CreateTextureView(const TextureViewCreateInfo& info);

        [[nodiscard]] Result<Buffer*> CreateBuffer(const BufferCreateInfo& info);
        void DestroyImmediate(const Buffer* buffer) const;

        [[nodiscard]] Result<Sampler*> CreateSampler(const SamplerCreateInfo& info);
        void DestroyImmediate(const Sampler* sampler) const;

        [[nodiscard]] Result<Heap*> CreateHeap(const HeapCreateInfo& info);
        void DestroyImmediate(const Heap* heap) const;

        [[nodiscard]] Result<ResourceAllocationInfo> GetAllocationInfo(
            const TextureCreateInfo& info,
            MemoryType memory = MemoryType::DeviceLocal) const;
        [[nodiscard]] Result<ResourceAllocationInfo> GetAllocationInfo(
            const BufferCreateInfo& info,
            MemoryType memory = MemoryType::DeviceLocal) const;

        [[nodiscard]] TextureCopyFootprint GetCopyFootprint(const Texture* texture,
                                                            uint32_t mip_level = 0) const;

        [[nodiscard]] Result<RenderGraph*> CreateRenderGraph(
            const RenderGraphCreateInfo& info = {});
        void DestroyImmediate(const RenderGraph* render_graph) const;

        [[nodiscard]] void* GetNativeRootSignature() const;

        [[nodiscard]] ResidencySet* GetCoreResidencySet() const
        {
            return m_core_residency_set;
        }

        template <typename T>
        void Retire(const T* object)
        {
            Retire(object, m_frame_fence);
        }

        template <typename T>
        void Retire(const T* object, const Fence* fence)
        {
            Retire(object, fence, fence->GetSignaledValue() + 1);
        }

        template <typename T>
        void Retire(const T* object, const Fence* fence, uint64_t value)
        {
            if (!object) return;
            m_retired.push_back({.object = object,
                                 .fence = fence,
                                 .value = value,
                                 .destroy = [](const void* retired)
                                 {
                                     delete static_cast<const T*>(retired);
                                 }});
        }

        [[nodiscard]] Result<void> NewFrame();
        [[nodiscard]] FenceOp GetFrameSignal() const;

        [[nodiscard]] static constexpr BackendType GetBackendType() noexcept
        {
#ifdef RIMERA_METAL
            return BackendType::Metal;
#elifdef RIMERA_D3D12
            return BackendType::D3D12;
#elifdef RIMERA_VULKAN
            return BackendType::Vulkan;
#endif
        }
        [[nodiscard]] const Fence* GetFrameFence() const noexcept { return m_frame_fence; }

        [[nodiscard]] void* GetNativeHandle() const;

    private:
        Device() = default;
        friend Result<Device*> CreateDevice(const DeviceCreateInfo& info);
        [[nodiscard]] Result<void> Initialize(const DeviceCreateInfo& info);

        template <typename T, typename... Args>
        [[nodiscard]] static Result<T*> CreateObject(Args&&... args)
        {
            auto* object = new(std::nothrow) T();
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

        struct RetiredObject
        {
            const void* object = nullptr;
            const Fence* fence = nullptr;
            uint64_t value = 0;
            void (*destroy)(const void* object) = nullptr;
        };

        void ReleaseRetired();
        void FlushRetired(const Fence* fence);

        ResidencySet* m_core_residency_set = nullptr;
        std::vector<RetiredObject> m_retired;
        Fence* m_frame_fence = nullptr;
        std::array<uint64_t, 3> m_frame_values{};
        uint32_t m_current_frame = 0;
        uint64_t m_queue_id = 0;
#ifdef RIMERA_DEBUG
        [[nodiscard]] const Queue* FindQueue(uint64_t id) const;
        void ValidateLiveObjects() const;
        std::unordered_map<uint64_t, const Queue*> m_queues;
        std::unordered_set<const ResidencySet*> m_residency_sets;
#endif
        Queue* m_upload_queue = nullptr;
        Command* m_upload_command = nullptr;
        Fence* m_upload_fence = nullptr;
        std::unordered_map<Format, Pipeline*> m_blit_pipelines;

        [[nodiscard]] Result<void> UploadTexture(Texture* texture, const void* data);
        [[nodiscard]] Result<void> UploadBuffer(Buffer* buffer, const void* data);
        [[nodiscard]] Result<void> EnsureUploadObjects();
        [[nodiscard]] Result<void> SubmitUpload();

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
        friend class Swapchain;
        friend class Resident;
        friend class ResidencySet;

#ifdef RIMERA_METAL
        [[nodiscard]] Result<uint32_t> AddDescriptor(const Texture* texture);
        NS::SharedPtr<MTL::Device> m_device;
        NS::SharedPtr<NS::AutoreleasePool> m_frame_pool;
        NS::SharedPtr<MTL::Buffer> m_resource_descriptor_heap;
        NS::SharedPtr<MTL::Buffer> m_sampler_descriptor_heap;
        NS::SharedPtr<MTL::Buffer> m_constant_arena;
        IRRootSignature* m_root_sig = nullptr;
        uint32_t m_current_resource = 0;
        std::vector<uint32_t> m_free_resources;

        uint32_t m_current_sampler = 0;
        std::vector<uint32_t> m_free_samplers;
#elifdef RIMERA_D3D12
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
        ComPtr<D3D12MA::Allocator> m_allocator;

        ComPtr<ID3D12RootSignature> m_root_sig;
        ComPtr<ID3D12InfoQueue1> m_info_queue;
        DWORD m_callback_cookie = 0;
        bool m_callback_registered = false;
        bool m_gpu_upload_heap = false;
#elifdef RIMERA_VULKAN
        friend class Pipeline;

        struct QueueFamily
        {
            uint32_t index = VK_QUEUE_FAMILY_IGNORED;
            uint32_t count = 0;
            uint32_t next = 0;
        };

        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_debug_messenger = VK_NULL_HANDLE;
        VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        VmaAllocator m_allocator = VK_NULL_HANDLE;
        std::array<QueueFamily, 3> m_queue_families{};
        std::vector<uint32_t> m_unique_queue_families;

        struct DescriptorHeap
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
            uint8_t* data = nullptr;
            VkDeviceAddress address = 0;
            uint64_t stride = 0;
            uint64_t size = 0;
            uint64_t reserved_offset = 0;
            uint64_t reserved_size = 0;
            uint32_t count = 0;
            uint32_t index = 0;
            std::vector<uint32_t> free_descriptors;
        };

        [[nodiscard]] Result<DescriptorHeap> CreateDescriptorHeap(uint32_t count,
            uint64_t stride,
            uint64_t alignment,
            uint64_t reserved_size);
        void DestroyDescriptorHeap(DescriptorHeap& heap);
        [[nodiscard]] Result<uint32_t> AllocateDescriptor(DescriptorHeap& heap);
        [[nodiscard]] Result<uint32_t> AddDescriptor(const Texture* texture, TextureViewType type);
        void WriteSamplerDescriptor(uint32_t index, const VkSamplerCreateInfo& info);
        void FlushDescriptor(const DescriptorHeap& heap, uint32_t index) const;
        DescriptorHeap m_resource_heap;
        DescriptorHeap m_sampler_heap;
        VkPhysicalDeviceDescriptorHeapPropertiesEXT m_heap_properties{};
        std::array<VkDescriptorSetAndBindingMappingEXT, 6> m_mappings{};
        VkShaderDescriptorSetAndBindingMappingInfoEXT m_mapping_info{};
        bool m_unified_image_layouts = false;
        bool m_gpu_upload_heap = false;
        bool m_memory_priority = false;
        bool m_hdr_metadata = false;
        bool m_swapchain_colorspace = false;

        [[nodiscard]] bool SupportsHostImageTransfer(Format format) const;
        [[nodiscard]] Result<void> TransitionImageLayout(VkImage image,
                                                         VkImageAspectFlags aspect,
                                                         uint32_t mip_levels,
                                                         uint32_t layer_count,
                                                         VkImageLayout layout);
#endif
    };
} // namespace RM