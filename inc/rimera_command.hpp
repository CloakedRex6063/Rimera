#pragma once
#include "rimera_shader.hpp"
#include "rimera_structs.hpp"

namespace RM
{
    class Sampler;
    class Buffer;
    class Device;

    class Command
    {
    public:
        ~Command();

        void Begin() const;
        void End() const;

        void BeginRenderPass(const RenderPassCreateInfo& info) const;
        void EndRenderPass();

        void BeginComputePass();
        void EndComputePass();

        void Draw(uint32_t vertex_count,
                  uint32_t instance_count,
                  uint32_t start_vertex,
                  uint32_t start_instance) const;
        void DrawIndexed(uint32_t index_count,
                         uint32_t instance_count,
                         uint32_t start_index,
                         uint32_t start_vertex,
                         uint32_t start_instance) const;

        void DispatchMesh(DispatchGroup dispatch_group) const;
        void Dispatch(DispatchGroup dispatch_group) const;

        void BindShader(const Pipeline* shader) const;
        void BindConstantBuffer(uint32_t slot, const Buffer* buffer) const;

        void Barrier(const std::span<const BarrierGroup>& groups) const;
        void Barrier(const BarrierGroup& group) const;
        void Barrier(std::span<const TextureBarrier> barriers) const;
        void Barrier(const TextureBarrier& barrier) const;
        void Barrier(std::span<const BufferBarrier> barriers) const;
        void Barrier(const BufferBarrier& barrier) const;
        void Barrier(std::span<const GlobalBarrier> barriers) const;
        void Barrier(const GlobalBarrier& barrier) const;

        void SetViewport(const Viewport& viewport) const;
        void SetScissor(const Rect& rect) const;
        void SetPrimitiveTopology(PrimitiveTopology topology) const;

        void CopyTexture(const Texture* source, const Texture* dest) const;
        void Blit(const TextureView* source, const TextureView* dest, const BlitRegion& region);

        [[nodiscard]] void* GetNativeCommand() const;
        [[nodiscard]] void* GetNativeAllocator() const;

    private:
        struct BlitPassData
        {
            uint32_t src_index;
            uint32_t sampler_index;
            uint32_t src_mip;
            uint32_t padding;
            Float4 uv_rect;
        };

        Command(Device* device, QueueType queue_type);
        friend class CommandRing;
        friend class Device;
        Device* m_device;
#ifdef RIMERA_MAC
        NS::SharedPtr<MTL4::CommandBuffer> m_command_buffer;
        NS::SharedPtr<MTL4::CommandAllocator> m_command_allocator;

        MTL4::RenderCommandEncoder* m_render_encoder = nullptr;
        MTL4::ComputeCommandEncoder* m_compute_encoder = nullptr;
        DispatchGroup m_threads_per_mesh{};
        DispatchGroup m_threads_per_ampl{};
        DispatchGroup m_threads_per_compute{};
#elifdef RIMERA_WINDOWS
        ComPtr<ID3D12GraphicsCommandList10> m_command_buffer;
        ComPtr<ID3D12CommandAllocator> m_command_allocator;
        Buffer* m_blit_buffer; // Reuse later if more custom functionality is added
        Pipeline* m_blit_shader;
#endif
        Sampler* m_sampler = nullptr;
    };

    class CommandRing
    {
    public:
        CommandRing(Device* device, QueueType queue_type);
        ~CommandRing();

        void Next() { m_current_buffer = (m_current_buffer + 1) % m_commands.size(); }
        [[nodiscard]] Command* GetCurrent() const { return m_commands[m_current_buffer]; }

    private:
        std::array<Command*, 3> m_commands{};
        uint32_t m_current_buffer = 0;
    };
} // namespace RM