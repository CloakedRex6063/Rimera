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
        static constexpr uint32_t kConstantBufferSlotCount = 3;
        static constexpr uint32_t kRootConstantSlot = 3;
        static constexpr uint32_t kRootConstantCount = 32;

        Command(const Command&) = delete;
        Command& operator=(const Command&) = delete;
        ~Command();

        void Begin();
        void End() const;

        void BeginRenderPass(const RenderPassCreateInfo& info);
        void EndRenderPass();

        void BeginComputePass();
        void EndComputePass();

        Result<void> Draw(uint32_t vertex_count,
                          uint32_t instance_count,
                          uint32_t start_vertex,
                          uint32_t start_instance) const;
        Result<void> DrawIndexed(uint32_t index_count,
                                 uint32_t instance_count,
                                 uint32_t start_index,
                                 uint32_t start_vertex,
                                 uint32_t start_instance) const;

        Result<void> DispatchMesh(DispatchGroup dispatch_group) const;
        Result<void> Dispatch(DispatchGroup dispatch_group);

        Result<void> BindShader(const Pipeline* shader);
        Result<void> BindConstantBuffer(uint32_t slot, const Buffer* buffer) const;
        Result<void> BindRootConstants(const void* data, uint32_t size, uint32_t offset = 0) const;

        template <class T>
            requires(!std::is_pointer_v<T> && std::is_trivially_copyable_v<T>)
        Result<void> RootConstants(const T& value, uint32_t offset = 0)
        {
            return BindRootConstants(&value, sizeof(T), offset);
        }

        Result<void> BindIndexBuffer(const Buffer* buffer, Format format);

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

        Result<void> CopyTexture(const Texture* source, const Texture* dest);
        Result<void> CopyBufferToTexture(const Buffer* source,
                                         const Texture* dest,
                                         const BufferTextureCopy& region);
        Result<void> CopyBuffer(const Buffer* source,
                                const Buffer* dest,
                                uint64_t source_offset = 0,
                                uint64_t dest_offset = 0,
                                uint64_t size = UINT64_MAX);
        Result<void> Blit(const Texture* source,
                          const Texture* dest,
                          const BlitRegion& region = {});

        [[nodiscard]] void* GetNativeCommand() const;
        [[nodiscard]] void* GetNativeAllocator() const;

    private:
        Command() = default;

        struct BlitPassData
        {
            uint32_t src_index;
            uint32_t src_mip;
            uint32_t sampler_index;
            uint32_t padding;
            Float4 uv_rect;
        };

        [[nodiscard]] Result<void> Initialize(Device* device, QueueType queue_type);
        friend class CommandRing;
        friend class Device;
        Device* m_device = nullptr;

        struct IndexBufferBinding
        {
            uint64_t address = 0;
            uint32_t length = 0;
            uint32_t index_size = 0;
        };

        IndexBufferBinding m_index_buffer{};
#ifdef RIMERA_METAL
        NS::SharedPtr<MTL4::CommandBuffer> m_command_buffer;
        NS::SharedPtr<MTL4::CommandAllocator> m_command_allocator;
        NS::SharedPtr<MTL4::ArgumentTable> m_vertex_arg_table;

        MTL4::RenderCommandEncoder* m_render_encoder = nullptr;
        MTL4::ComputeCommandEncoder* m_compute_encoder = nullptr;

        void GetComputeEncoder();
        void EndTransientComputeEncoder(bool transient);
        [[nodiscard]] Result<void> CreateArgumentBuffer(uint32_t size) const;
        [[nodiscard]] Result<uint32_t> AllocateArguments(uint32_t size) const;
        [[nodiscard]] Result<void> FlushArguments() const;

        static constexpr uint32_t kArgumentAlignment = 16;
        static constexpr uint32_t kInitialArgumentBufferSize = 256 * 1024;

        NS::SharedPtr<MTL4::ArgumentTable> m_arg_table;
        mutable std::array<uint64_t, kConstantBufferSlotCount + 1> m_arguments{};
        mutable std::array<uint8_t, kRootConstantCount * sizeof(uint32_t)> m_root_constants{};
        mutable uint32_t m_root_constants_size = 0;
        mutable bool m_arguments_dirty = true;
        mutable bool m_root_constants_dirty = true;
        mutable Buffer* m_arg_buffer = nullptr;
        mutable uint8_t* m_arg_data = nullptr;
        mutable uint32_t m_arg_offset = 0;

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

#elifdef RIMERA_D3D12
        bool m_in_compute_pass = false;
        ComPtr<ID3D12GraphicsCommandList10> m_command_buffer;
        ComPtr<ID3D12CommandAllocator> m_command_allocator;
#endif
        BlitPassData* m_blit_data = nullptr;
        Sampler* m_sampler = nullptr;
        Buffer* m_blit_buffer = nullptr; // Reuse later if more custom functionality is added
    };

    class CommandRing
    {
    public:
        CommandRing(const CommandRing&) = delete;
        CommandRing& operator=(const CommandRing&) = delete;
        ~CommandRing();

        void Next() { m_current_buffer = (m_current_buffer + 1) % m_commands.size(); }
        [[nodiscard]] Command* GetCurrent() const { return m_commands[m_current_buffer]; }

    private:
        friend class Device;
        CommandRing() = default;
        [[nodiscard]] Result<void> Initialize(Device* device, QueueType queue_type);
        std::array<Command*, 3> m_commands{};
        uint32_t m_current_buffer = 0;
    };
} // namespace RM