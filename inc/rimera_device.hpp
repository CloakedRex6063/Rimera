#pragma once
#include "rimera_buffer.hpp"
#include "rimera_structs.hpp"
#include "rimera_queue.hpp"
#include "rimera_residency.hpp"
#include "rimera_shader.hpp"
#include "rimera_texture.hpp"

struct IRRootSignature;

namespace RM
{
    class Device
    {
    public:
        Device(const DeviceCreateInfo& info);
        ~Device();

        [[nodiscard]] Fence* CreateFence() const;
        void DestroyFence(const Fence* fence);

        [[nodiscard]] Queue* CreateQueue(QueueType type) const;
        void DestroyQueue(const Queue* queue);

        [[nodiscard]] Command* CreateCommand(CommandType type);
        void DestroyCommand(const Command* command) const;

        [[nodiscard]] CommandRing* CreateCommandRing(QueueType type);
        void DestroyCommandRing(const CommandRing* command_ring) const;

        [[nodiscard]] ResidencySet* CreateResidencySet() const;
        void DestroyResidencySet(const ResidencySet* residency_set) const;

        [[nodiscard]] Shader* CreateShader(ComputeShaderCreateInfo info);
        void DestroyShader(const Shader* shader);

        [[nodiscard]] Texture* CreateTexture(const TextureCreateInfo& info);
        void DestroyTexture(const Texture* texture) const;

        [[nodiscard]] Buffer* CreateBuffer(const BufferCreateInfo& info);
        void DestroyBuffer(const Buffer* buffer);

        [[nodiscard]] uint32_t AddDescriptor(const Buffer* buffer);
        [[nodiscard]] uint32_t AddDescriptor(const Texture* texture);
        void RemoveDescriptor(uint32_t descriptor);

        [[nodiscard]] void* GetNativeRootSignature() const;

        [[nodiscard]] void* GetCurrentPresentable() const;
        [[nodiscard]] uint32_t GetCurrentPresentableDescriptor() const;
        [[nodiscard]] ResidencySet* GetCoreResidencySet() const
        {
            return m_core_residency_set.get();
        }

        void NewFrame(const Queue* queue);
        
        void Resize(uint32_t width, uint32_t height);

        [[nodiscard]] void* GetNativeHandle() const;

    private:
        uint32_t m_current_descriptor = 0;
        std::vector<uint32_t> m_free_list;
        std::unordered_map<uint64_t, uint32_t> m_swapchain_descriptors{};
        std::shared_ptr<ResidencySet> m_core_residency_set;

        void CreateRootSignature();
#ifdef RIMERA_MAC
        friend class Command;
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
#endif
    };
}  // namespace RM
