#pragma once
#include "concepts"
#include "functional"
#include "string"
#include "unordered_map"
#include "variant"
#include "rimera_structs.hpp"

namespace RM
{
    class Buffer;
    class Command;
    class Device;
    class Heap;
    class Pipeline;
    class Texture;
    class RenderGraph;

    struct GraphTexture
    {
        uint32_t index = UINT32_MAX;

        [[nodiscard]] bool IsValid() const noexcept { return index != UINT32_MAX; }
    };

    struct GraphBuffer
    {
        uint32_t index = UINT32_MAX;

        [[nodiscard]] bool IsValid() const noexcept { return index != UINT32_MAX; }
    };

    struct ResourceState
    {
        BarrierLayout layout = BarrierLayout::Common;
        BarrierStage stage = BarrierStage::None;
        BarrierAccess access = BarrierAccess::NoAccess;
    };

    enum class GraphPassType
    {
        Render,
        Compute,
        Copy,
    };

    struct RenderGraphStatistics
    {
        uint32_t declared_passes = 0;
        uint32_t executed_passes = 0;
        uint32_t culled_passes = 0;
        uint32_t parallel_batches = 0;
        uint32_t barriers = 0;
        uint32_t transient_textures = 0;
        uint32_t transient_buffers = 0;
        uint64_t transient_memory = 0;
        uint64_t transient_memory_unaliased = 0;
        uint64_t heap_size = 0;
    };

    struct RenderGraphPassInfo
    {
        std::string name;
        GraphPassType type = GraphPassType::Render;
        uint32_t step = UINT32_MAX;
    };

    struct RenderGraphResourceInfo
    {
        std::string name;
        bool texture = true;
        uint32_t first_step = UINT32_MAX;
        uint32_t last_step = 0;
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    struct RenderGraphTimeline
    {
        uint32_t step_count = 0;
        std::vector<RenderGraphPassInfo> passes;
        std::vector<RenderGraphResourceInfo> resources;
    };

    class RenderGraphContext
    {
    public:
        [[nodiscard]] Command* GetCommand() const noexcept { return m_command; }
        [[nodiscard]] Texture* GetTexture(GraphTexture texture) const;
        [[nodiscard]] Buffer* GetBuffer(GraphBuffer buffer) const;
        [[nodiscard]] UInt2 GetRenderArea() const noexcept { return m_render_area; }
        [[nodiscard]] std::string_view GetPassName() const;

    private:
        friend class RenderGraph;

        RenderGraphContext(const RenderGraph* graph,
                           Command* command,
                           uint32_t pass,
                           UInt2 render_area)
            : m_graph(graph), m_command(command), m_pass(pass), m_render_area(render_area)
        {
        }

        const RenderGraph* m_graph = nullptr;
        Command* m_command = nullptr;
        uint32_t m_pass = UINT32_MAX;
        UInt2 m_render_area{};
    };

    using GraphExecuteFunction = std::function<Result<void>(RenderGraphContext& context)>;

    template <class Derived>
    class GraphPassBuilderBase;
    template <class Derived>
    class GraphShaderPassBuilderBase;
    class GraphRenderPassBuilder;
    class GraphComputePassBuilder;
    class GraphCopyPassBuilder;

    class RenderGraph
    {
    public:
        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;
        ~RenderGraph();

        [[nodiscard]] GraphTexture CreateTexture(const TextureCreateInfo& info,
                                                 std::string_view name = {});
        [[nodiscard]] GraphBuffer CreateBuffer(const BufferCreateInfo& info,
                                               std::string_view name = {});

        [[nodiscard]] GraphTexture ImportTexture(
            Texture* texture,
            std::optional<ResourceState> initial_state = std::nullopt,
            std::optional<ResourceState> final_state = std::nullopt);
        [[nodiscard]] GraphBuffer ImportBuffer(
            Buffer* buffer,
            std::optional<ResourceState> initial_state = std::nullopt,
            std::optional<ResourceState> final_state = std::nullopt);

        [[nodiscard]] GraphRenderPassBuilder AddRenderPass(std::string_view name);
        [[nodiscard]] GraphComputePassBuilder AddComputePass(std::string_view name);
        [[nodiscard]] GraphCopyPassBuilder AddCopyPass(std::string_view name);

        [[nodiscard]] Result<void> Execute(Command* command);

        [[nodiscard]] std::optional<ResourceState> GetResourceState(const Texture* texture) const;
        [[nodiscard]] std::optional<ResourceState> GetResourceState(const Buffer* buffer) const;
        void SetResourceState(const Texture* texture, const ResourceState& state);
        void SetResourceState(const Buffer* buffer, const ResourceState& state);
        void ForgetResource(const Texture* texture);
        void ForgetResource(const Buffer* buffer);

        [[nodiscard]] const RenderGraphStatistics& GetStatistics() const noexcept
        {
            return m_statistics;
        }

        [[nodiscard]] const RenderGraphTimeline& GetTimeline() const noexcept
        {
            return m_timeline;
        }

        void ReleaseTransientMemory();

    private:
        RenderGraph() = default;
        friend class Device;
        friend class RenderGraphContext;
        template <class Derived>
        friend class GraphPassBuilderBase;
        template <class Derived>
        friend class GraphShaderPassBuilderBase;
        friend class GraphRenderPassBuilder;
        friend class GraphComputePassBuilder;
        friend class GraphCopyPassBuilder;

        enum class ResourceType : uint8_t
        {
            Texture,
            Buffer,
        };

        struct ResourceUsage
        {
            uint32_t resource = UINT32_MAX;
            ResourceState state{};
            bool read = false;
            bool write = false;
            bool overwrite = false;
        };

        struct ColorTarget
        {
            uint32_t resource = UINT32_MAX;
            LoadOp load_op = LoadOp::Load;
            StoreOp store_op = StoreOp::Store;
            ClearColor clear_color{};
        };

        struct DepthTarget
        {
            uint32_t resource = UINT32_MAX;
            LoadOp load_op = LoadOp::Load;
            StoreOp store_op = StoreOp::Store;
            float clear_depth = 1.f;
            float clear_stencil = 0.f;
        };

        struct ConstantBufferBinding
        {
            uint32_t slot = 0;
            const Buffer* buffer = nullptr;
            uint32_t resource = UINT32_MAX;
        };

        struct RootConstantsBinding
        {
            uint32_t offset = 0;
            std::vector<uint8_t> data;
        };

        struct ResourceIndexBinding
        {
            uint32_t offset = 0;
            uint32_t resource = UINT32_MAX;
        };

        struct IndexBufferBinding
        {
            const Buffer* buffer = nullptr;
            uint32_t resource = UINT32_MAX;
            Format format = Format::R32_UInt;
        };

        struct DrawCommand
        {
            uint32_t vertex_count = 0;
            uint32_t instance_count = 1;
            uint32_t first_vertex = 0;
            uint32_t first_instance = 0;
        };

        struct DrawIndexedCommand
        {
            uint32_t index_count = 0;
            uint32_t instance_count = 1;
            uint32_t first_index = 0;
            uint32_t vertex_offset = 0;
            uint32_t first_instance = 0;
        };

        struct DispatchMeshCommand
        {
            DispatchGroup group{};
        };

        struct DispatchCommand
        {
            DispatchGroup group{};
        };

        struct BlitCommand
        {
            uint32_t source = UINT32_MAX;
            uint32_t destination = UINT32_MAX;
            BlitRegion region{};
        };

        struct CopyTextureCommand
        {
            uint32_t source = UINT32_MAX;
            uint32_t destination = UINT32_MAX;
        };

        struct CopyBufferCommand
        {
            uint32_t source = UINT32_MAX;
            uint32_t destination = UINT32_MAX;
            uint64_t source_offset = 0;
            uint64_t destination_offset = 0;
            uint64_t size = UINT64_MAX;
        };

        struct CopyBufferToTextureCommand
        {
            uint32_t source = UINT32_MAX;
            uint32_t destination = UINT32_MAX;
            BufferTextureCopy region{};
        };

        using PassCommand = std::variant<DrawCommand,
                                         DrawIndexedCommand,
                                         DispatchMeshCommand,
                                         DispatchCommand,
                                         BlitCommand,
                                         CopyTextureCommand,
                                         CopyBufferCommand,
                                         CopyBufferToTextureCommand>;

        struct Pass
        {
            std::string name;
            GraphPassType type = GraphPassType::Render;
            std::vector<ResourceUsage> usages;
            std::vector<ColorTarget> color_targets;
            std::optional<DepthTarget> depth_target;
            const Pipeline* pipeline = nullptr;
            std::vector<ConstantBufferBinding> constant_buffers;
            std::vector<RootConstantsBinding> root_constants;
            std::vector<ResourceIndexBinding> resource_indices;
            std::optional<IndexBufferBinding> index_buffer;
            std::optional<PrimitiveTopology> topology;
            std::optional<Viewport> viewport;
            std::optional<Rect> scissor;
            std::vector<PassCommand> commands;
            GraphExecuteFunction execute;
            bool side_effect = false;
            bool parallel = false;
            bool live = false;
        };

        struct TrackedState
        {
            ResourceState state{};
            bool write = false;
            bool initialized = false;
            bool touched = false;
        };

        struct Resource
        {
            std::string name;
            ResourceType type = ResourceType::Texture;
            bool imported = false;
            TextureCreateInfo texture_info{};
            BufferCreateInfo buffer_info{};
            Texture* texture = nullptr;
            Buffer* buffer = nullptr;
            std::optional<ResourceState> final_state;
            TrackedState tracked{};
            ResourceAllocationInfo allocation{};
            uint64_t offset = 0;
            uint32_t first_step = UINT32_MAX;
            uint32_t last_step = 0;
            std::vector<uint32_t> alias_predecessors;
        };

        struct Step
        {
            GraphPassType type = GraphPassType::Render;
            bool parallel = false;
            std::vector<uint32_t> passes;
        };

        struct CachedTexture
        {
            TextureCreateInfo info{};
            uint64_t offset = 0;
            Texture* texture = nullptr;
            uint64_t last_used = 0;
            bool in_use = false;
        };

        struct CachedBuffer
        {
            BufferCreateInfo info{};
            uint64_t offset = 0;
            Buffer* buffer = nullptr;
            uint64_t last_used = 0;
            bool in_use = false;
        };

        struct RetiredHeap
        {
            Heap* heap = nullptr;
            std::vector<Texture*> textures;
            std::vector<Buffer*> buffers;
            uint64_t last_used = 0;
        };

        [[nodiscard]] Result<void> Initialize(Device* device, const RenderGraphCreateInfo& info);
        [[nodiscard]] uint32_t AddPass(std::string_view name, GraphPassType type);
        void AddUsage(uint32_t pass, uint32_t resource, ResourceType type, ResourceUsage usage);
        void AddColorTarget(uint32_t pass, GraphTexture texture, const ColorTarget& target);
        void SetDepthTarget(uint32_t pass, GraphTexture texture, const DepthTarget& target);
        void Fail(const std::string& message);

        [[nodiscard]] Result<void> Compile();
        void CullPasses();
        void SchedulePasses();
        [[nodiscard]] bool PassesConflict(uint32_t a, uint32_t b) const;
        [[nodiscard]] Result<void> AllocateTransients();
        [[nodiscard]] Result<void> EnsureHeap(uint64_t size);
        [[nodiscard]] Result<Texture*> AcquireTexture(const Resource& resource);
        [[nodiscard]] Result<Buffer*> AcquireBuffer(const Resource& resource);
        [[nodiscard]] Result<void> Record(Command* command);
        [[nodiscard]] Result<void> RecordStep(Command* command,
                                              const Step& step,
                                              const Step* previous);
        [[nodiscard]] Result<void> RecordPass(Command* command, uint32_t pass_index, UInt2 area);
        [[nodiscard]] Result<void> Transition(const ResourceUsage& usage,
                                              std::vector<TextureBarrier>& textures,
                                              std::vector<BufferBarrier>& buffers);
        void FinishFrame();
        void CaptureTimeline();
        void ReleaseUnusedResources(bool release_all);

        Device* m_device = nullptr;
        RenderGraphCreateInfo m_info{};
        std::vector<Pass> m_passes;
        std::vector<Resource> m_resources;
        std::vector<Step> m_steps;
        std::string m_error;
        Heap* m_heap = nullptr;
        std::vector<CachedTexture> m_texture_cache;
        std::vector<CachedBuffer> m_buffer_cache;
        std::vector<RetiredHeap> m_retired_heaps;
        std::unordered_map<const void*, ResourceState> m_external_states;
        RenderGraphStatistics m_statistics{};
        RenderGraphTimeline m_timeline{};
        uint64_t m_frame = 0;
    };

    template <class Derived>
    class GraphPassBuilderBase
    {
    public:
        Derived& SideEffect()
        {
            m_graph->m_passes[m_pass].side_effect = true;
            return Self();
        }

        Derived& Execute(GraphExecuteFunction function)
        {
            m_graph->m_passes[m_pass].execute = std::move(function);
            return Self();
        }

    protected:
        GraphPassBuilderBase(RenderGraph* graph, uint32_t pass) : m_graph(graph), m_pass(pass)
        {
        }

        Derived& Self()
        {
            return static_cast<Derived&>(*this);
        }

        RenderGraph::Pass& Pass()
        {
            return m_graph->m_passes[m_pass];
        }

        void Use(GraphTexture texture,
                 BarrierLayout layout,
                 BarrierStage stage,
                 BarrierAccess access,
                 bool read,
                 bool write,
                 bool overwrite)
        {
            m_graph->AddUsage(m_pass,
                              texture.index,
                              RenderGraph::ResourceType::Texture,
                              {.state = {.layout = layout, .stage = stage, .access = access},
                               .read = read,
                               .write = write,
                               .overwrite = overwrite});
        }

        void Use(GraphBuffer buffer,
                 BarrierStage stage,
                 BarrierAccess access,
                 bool read,
                 bool write,
                 bool overwrite)
        {
            m_graph->AddUsage(m_pass,
                              buffer.index,
                              RenderGraph::ResourceType::Buffer,
                              {.state = {.layout = BarrierLayout::Common,
                                         .stage = stage,
                                         .access = access},
                               .read = read,
                               .write = write,
                               .overwrite = overwrite});
        }

        RenderGraph* m_graph = nullptr;
        uint32_t m_pass = UINT32_MAX;
    };

    template <class Derived>
    class GraphShaderPassBuilderBase : public GraphPassBuilderBase<Derived>
    {
    public:
        Derived& Read(GraphTexture texture, BarrierStage stage = BarrierStage::None)
        {
            this->Use(texture,
                      BarrierLayout::ShaderResource,
                      stage,
                      BarrierAccess::ShaderResource,
                      true,
                      false,
                      false);
            return this->Self();
        }

        Derived& Read(GraphBuffer buffer,
                      BarrierAccess access = BarrierAccess::ShaderResource,
                      BarrierStage stage = BarrierStage::None)
        {
            this->Use(buffer, stage, access, true, false, false);
            return this->Self();
        }

        Derived& Write(GraphTexture texture, BarrierStage stage = BarrierStage::None)
        {
            this->Use(texture,
                      BarrierLayout::UnorderedAccess,
                      stage,
                      BarrierAccess::UnorderedAccess,
                      false,
                      true,
                      true);
            return this->Self();
        }

        Derived& Write(GraphBuffer buffer, BarrierStage stage = BarrierStage::None)
        {
            this->Use(buffer, stage, BarrierAccess::UnorderedAccess, false, true, true);
            return this->Self();
        }

        Derived& ReadWrite(GraphTexture texture, BarrierStage stage = BarrierStage::None)
        {
            this->Use(texture,
                      BarrierLayout::UnorderedAccess,
                      stage,
                      BarrierAccess::UnorderedAccess,
                      true,
                      true,
                      false);
            return this->Self();
        }

        Derived& ReadWrite(GraphBuffer buffer, BarrierStage stage = BarrierStage::None)
        {
            this->Use(buffer, stage, BarrierAccess::UnorderedAccess, true, true, false);
            return this->Self();
        }

        Derived& Pipeline(const Pipeline* pipeline)
        {
            this->Pass().pipeline = pipeline;
            return this->Self();
        }

        Derived& ConstantBuffer(uint32_t slot, const Buffer* buffer)
        {
            this->Pass().constant_buffers.push_back({.slot = slot, .buffer = buffer});
            return this->Self();
        }

        Derived& ConstantBuffer(uint32_t slot, GraphBuffer buffer)
        {
            this->Use(buffer,
                      BarrierStage::None,
                      BarrierAccess::ConstantBuffer,
                      true,
                      false,
                      false);
            this->Pass().constant_buffers.push_back({.slot = slot, .resource = buffer.index});
            return this->Self();
        }

        Derived& RootConstants(const void* data, uint32_t size, uint32_t offset = 0)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            this->Pass().root_constants.push_back(
                {.offset = offset, .data = std::vector<uint8_t>(bytes, bytes + size)});
            return this->Self();
        }

        template <class T>
            requires(!std::is_pointer_v<T> && std::is_trivially_copyable_v<T>)
        Derived& RootConstants(const T& value, uint32_t offset = 0)
        {
            return RootConstants(&value, sizeof(T), offset);
        }

        Derived& ResourceIndex(uint32_t offset, GraphTexture texture)
        {
            this->Pass().resource_indices.push_back({.offset = offset, .resource = texture.index});
            return this->Self();
        }

        Derived& ResourceIndex(uint32_t offset, GraphBuffer buffer)
        {
            this->Pass().resource_indices.push_back({.offset = offset, .resource = buffer.index});
            return this->Self();
        }

    protected:
        GraphShaderPassBuilderBase(RenderGraph* graph, uint32_t pass)
            : GraphPassBuilderBase<Derived>(graph, pass)
        {
        }
    };

    class GraphRenderPassBuilder : public GraphShaderPassBuilderBase<GraphRenderPassBuilder>
    {
    public:
        GraphRenderPassBuilder& Color(GraphTexture texture,
                                      LoadOp load = LoadOp::Load,
                                      StoreOp store = StoreOp::Store,
                                      ClearColor clear = {0.f, 0.f, 0.f, 1.f})
        {
            m_graph->AddColorTarget(m_pass,
                                    texture,
                                    {.resource = texture.index,
                                     .load_op = load,
                                     .store_op = store,
                                     .clear_color = clear});
            return *this;
        }

        GraphRenderPassBuilder& Depth(GraphTexture texture,
                                      LoadOp load = LoadOp::Load,
                                      StoreOp store = StoreOp::Store,
                                      float clear_depth = 1.f,
                                      float clear_stencil = 0.f)
        {
            m_graph->SetDepthTarget(m_pass,
                                    texture,
                                    {.resource = texture.index,
                                     .load_op = load,
                                     .store_op = store,
                                     .clear_depth = clear_depth,
                                     .clear_stencil = clear_stencil});
            return *this;
        }

        GraphRenderPassBuilder& Viewport(const Viewport& viewport)
        {
            Pass().viewport = viewport;
            return *this;
        }

        GraphRenderPassBuilder& Scissor(const Rect& scissor)
        {
            Pass().scissor = scissor;
            return *this;
        }

        GraphRenderPassBuilder& Topology(PrimitiveTopology topology)
        {
            Pass().topology = topology;
            return *this;
        }

        GraphRenderPassBuilder& IndexBuffer(const Buffer* buffer, Format format)
        {
            Pass().index_buffer = RenderGraph::IndexBufferBinding{
                .buffer = buffer, .format = format};
            return *this;
        }

        GraphRenderPassBuilder& IndexBuffer(GraphBuffer buffer, Format format)
        {
            Use(buffer, BarrierStage::IndexInput, BarrierAccess::IndexBuffer, true, false, false);
            Pass().index_buffer =
                RenderGraph::IndexBufferBinding{.resource = buffer.index, .format = format};
            return *this;
        }

        GraphRenderPassBuilder& Draw(uint32_t vertex_count,
                                     uint32_t instance_count = 1,
                                     uint32_t first_vertex = 0,
                                     uint32_t first_instance = 0)
        {
            Pass().commands.push_back(RenderGraph::DrawCommand{.vertex_count = vertex_count,
                                                               .instance_count = instance_count,
                                                               .first_vertex = first_vertex,
                                                               .first_instance = first_instance});
            return *this;
        }

        GraphRenderPassBuilder& DrawIndexed(uint32_t index_count,
                                            uint32_t instance_count = 1,
                                            uint32_t first_index = 0,
                                            uint32_t vertex_offset = 0,
                                            uint32_t first_instance = 0)
        {
            Pass().commands.push_back(
                RenderGraph::DrawIndexedCommand{.index_count = index_count,
                                                .instance_count = instance_count,
                                                .first_index = first_index,
                                                .vertex_offset = vertex_offset,
                                                .first_instance = first_instance});
            return *this;
        }

        GraphRenderPassBuilder& DispatchMesh(DispatchGroup group)
        {
            Pass().commands.push_back(RenderGraph::DispatchMeshCommand{.group = group});
            return *this;
        }

    private:
        friend class RenderGraph;

        GraphRenderPassBuilder(RenderGraph* graph, uint32_t pass)
            : GraphShaderPassBuilderBase(graph, pass)
        {
        }
    };

    class GraphComputePassBuilder : public GraphShaderPassBuilderBase<GraphComputePassBuilder>
    {
    public:
        GraphComputePassBuilder& Parallel(bool parallel = true)
        {
            m_graph->m_passes[m_pass].parallel = parallel;
            return *this;
        }

        GraphComputePassBuilder& Dispatch(DispatchGroup group)
        {
            Pass().commands.push_back(RenderGraph::DispatchCommand{.group = group});
            return *this;
        }

    private:
        friend class RenderGraph;

        GraphComputePassBuilder(RenderGraph* graph, uint32_t pass)
            : GraphShaderPassBuilderBase(graph, pass)
        {
        }
    };

    class GraphCopyPassBuilder : public GraphPassBuilderBase<GraphCopyPassBuilder>
    {
    public:
        GraphCopyPassBuilder& CopySource(GraphTexture texture)
        {
            Use(texture,
                BarrierLayout::CopySource,
                BarrierStage::Copy,
                BarrierAccess::CopySource,
                true,
                false,
                false);
            return *this;
        }

        GraphCopyPassBuilder& CopyDestination(GraphTexture texture)
        {
            Use(texture,
                BarrierLayout::CopyDestination,
                BarrierStage::Copy,
                BarrierAccess::CopyDestination,
                false,
                true,
                true);
            return *this;
        }

        GraphCopyPassBuilder& CopySource(GraphBuffer buffer)
        {
            Use(buffer, BarrierStage::Copy, BarrierAccess::CopySource, true, false, false);
            return *this;
        }

        GraphCopyPassBuilder& CopyDestination(GraphBuffer buffer)
        {
            Use(buffer, BarrierStage::Copy, BarrierAccess::CopyDestination, false, true, false);
            return *this;
        }

        GraphCopyPassBuilder& BlitSource(GraphTexture texture)
        {
            Use(texture,
                BarrierLayout::BlitSource,
                BarrierStage::BlitSource,
                BarrierAccess::ShaderResource,
                true,
                false,
                false);
            return *this;
        }

        GraphCopyPassBuilder& BlitDestination(GraphTexture texture)
        {
            Use(texture,
                BarrierLayout::BlitDestination,
                BarrierStage::BlitDestination,
                BarrierAccess::RenderTarget,
                false,
                true,
                false);
            return *this;
        }

        GraphCopyPassBuilder& Blit(GraphTexture source,
                                   GraphTexture destination,
                                   const BlitRegion& region = {})
        {
            BlitSource(source);
            BlitDestination(destination);
            Pass().commands.push_back(RenderGraph::BlitCommand{.source = source.index,
                                                               .destination = destination.index,
                                                               .region = region});
            return *this;
        }

        GraphCopyPassBuilder& CopyTexture(GraphTexture source, GraphTexture destination)
        {
            CopySource(source);
            CopyDestination(destination);
            Pass().commands.push_back(
                RenderGraph::CopyTextureCommand{.source = source.index,
                                                .destination = destination.index});
            return *this;
        }

        GraphCopyPassBuilder& CopyBuffer(GraphBuffer source,
                                         GraphBuffer destination,
                                         uint64_t source_offset = 0,
                                         uint64_t destination_offset = 0,
                                         uint64_t size = UINT64_MAX)
        {
            CopySource(source);
            CopyDestination(destination);
            Pass().commands.push_back(
                RenderGraph::CopyBufferCommand{.source = source.index,
                                               .destination = destination.index,
                                               .source_offset = source_offset,
                                               .destination_offset = destination_offset,
                                               .size = size});
            return *this;
        }

        GraphCopyPassBuilder& CopyBufferToTexture(GraphBuffer source,
                                                  GraphTexture destination,
                                                  const BufferTextureCopy& region)
        {
            CopySource(source);
            Use(destination,
                BarrierLayout::CopyDestination,
                BarrierStage::Copy,
                BarrierAccess::CopyDestination,
                false,
                true,
                false);
            Pass().commands.push_back(
                RenderGraph::CopyBufferToTextureCommand{.source = source.index,
                                                        .destination = destination.index,
                                                        .region = region});
            return *this;
        }

    private:
        friend class RenderGraph;

        GraphCopyPassBuilder(RenderGraph* graph, uint32_t pass)
            : GraphPassBuilderBase(graph, pass)
        {
        }
    };
} // namespace RM