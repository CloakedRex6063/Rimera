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

        void Begin();
        void End() const;

        void BeginRenderPass(const RenderPassCreateInfo& info);
        void EndRenderPass();

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
        void Dispatch(DispatchGroup dispatch_group);

        void BindShader(const Pipeline* shader);
        void BindConstantBuffer(uint32_t slot, const Buffer* buffer) const;
        void BindIndexBuffer(const Buffer* buffer, Format format);

        void Barrier(const BarrierGroup& group);
        void Barrier(std::span<const TextureBarrier> barriers);
        void Barrier(const TextureBarrier& barrier);
        void Barrier(std::span<const BufferBarrier> barriers);
        void Barrier(const BufferBarrier& barrier);
        void Barrier(std::span<const GlobalBarrier> barriers);
        void Barrier(const GlobalBarrier& barrier);

        void SetViewport(const Viewport& viewport) const;
        void SetScissor(const Rect& rect) const;
        void SetPrimitiveTopology(PrimitiveTopology topology);

        void CopyTexture(const Texture* source, const Texture* dest);
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

        struct IndexBufferBinding
        {
            uint64_t address = 0;
            uint32_t length = 0;
            uint32_t index_size = 0;
        };

        IndexBufferBinding m_index_buffer{};
#ifdef RIMERA_MAC
        NS::SharedPtr<MTL4::CommandBuffer> m_command_buffer;
        NS::SharedPtr<MTL4::CommandAllocator> m_command_allocator;
        NS::SharedPtr<MTL4::ArgumentTable> m_vertex_arg_table;

        MTL4::RenderCommandEncoder* m_render_encoder = nullptr;
        MTL4::ComputeCommandEncoder* m_compute_encoder = nullptr;
        
        void GetComputeEncoder();
        
        DispatchGroup m_threads_per_mesh{};
        DispatchGroup m_threads_per_ampl{};
        DispatchGroup m_threads_per_compute{};
        MTL::PrimitiveType m_primitive_type = MTL::PrimitiveTypeTriangle;

        struct PendingBarrier
        {
            MTL::Stages src_stage;
            MTL::Stages dst_stage;
        };

        std::vector<PendingBarrier> m_pending_barriers;

#elifdef RIMERA_WINDOWS
        ComPtr<ID3D12GraphicsCommandList10> m_command_buffer;
        ComPtr<ID3D12CommandAllocator> m_command_allocator;
#endif
        Sampler* m_sampler = nullptr;
        Buffer* m_blit_buffer; // Reuse later if more custom functionality is added
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