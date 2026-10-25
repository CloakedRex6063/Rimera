#include "rimera_render_graph.hpp"
#include "rimera_builders.hpp"
#include "rimera_command.hpp"
#include "rimera_device.hpp"
#include "rimera_heap.hpp"
#include "rimera_shader.hpp"

namespace
{
    using StageFlags = RM::EnumFlags<RM::BarrierStage>;
    using AccessFlags = RM::EnumFlags<RM::BarrierAccess>;

    constexpr StageFlags kGraphicsShaderStages{RM::BarrierStage::VertexShading,
                                               RM::BarrierStage::PixelShading,
                                               RM::BarrierStage::AmplificationShading,
                                               RM::BarrierStage::MeshShading};

    constexpr StageFlags kAllStages{RM::BarrierStage::VertexShading,
                                    RM::BarrierStage::PixelShading,
                                    RM::BarrierStage::ComputeShading,
                                    RM::BarrierStage::Copy,
                                    RM::BarrierStage::AmplificationShading,
                                    RM::BarrierStage::MeshShading,
                                    RM::BarrierStage::RenderTarget,
                                    RM::BarrierStage::DepthStencil,
                                    RM::BarrierStage::IndexInput};

    constexpr AccessFlags kWriteAccess{RM::BarrierAccess::RenderTarget,
                                       RM::BarrierAccess::UnorderedAccess,
                                       RM::BarrierAccess::DepthStencilWrite,
                                       RM::BarrierAccess::CopyDestination,
                                       RM::BarrierAccess::ResolveDestination,
                                       RM::BarrierAccess::AccelerationStructureWrite};

    constexpr RM::BarrierStage ToStage(StageFlags flags)
    {
        return static_cast<RM::BarrierStage>(flags.value());
    }

    constexpr RM::BarrierAccess MergeAccess(RM::BarrierAccess a, RM::BarrierAccess b)
    {
        if (a == RM::BarrierAccess::NoAccess) return b;
        if (b == RM::BarrierAccess::NoAccess) return a;
        return static_cast<RM::BarrierAccess>((AccessFlags(a) | b).value());
    }

    constexpr uint64_t AlignUp(uint64_t value, uint64_t alignment)
    {
        return alignment > 1 ? (value + alignment - 1) / alignment * alignment : value;
    }

    bool SameState(const RM::ResourceState& a, const RM::ResourceState& b)
    {
        return a.layout == b.layout && a.stage == b.stage && a.access == b.access;
    }

    bool SameTexture(const RM::TextureCreateInfo& a, const RM::TextureCreateInfo& b)
    {
        return a.width == b.width && a.height == b.height && a.mip_levels == b.mip_levels &&
               a.array_layers == b.array_layers && a.format == b.format && a.usage == b.usage;
    }

    bool SameBuffer(const RM::BufferCreateInfo& a, const RM::BufferCreateInfo& b)
    {
        return a.size == b.size && a.flags == b.flags;
    }

    bool NeedsDiscard(RM::BarrierLayout layout)
    {
        return layout == RM::BarrierLayout::RenderTarget ||
               layout == RM::BarrierLayout::DepthStencilWrite ||
               layout == RM::BarrierLayout::UnorderedAccess;
    }

    RM::TextureBarrierSubresource WholeTexture(const RM::Texture* texture)
    {
        return {.first_mip = 0,
                .num_mips = texture->GetMipLevels(),
                .first_array_layer = 0,
                .num_array_layers = texture->GetArrayLayers()};
    }
}

RM::Texture* RM::RenderGraphContext::GetTexture(GraphTexture texture) const
{
    if (texture.index >= m_graph->m_resources.size()) return nullptr;
    return m_graph->m_resources[texture.index].texture;
}

RM::Buffer* RM::RenderGraphContext::GetBuffer(GraphBuffer buffer) const
{
    if (buffer.index >= m_graph->m_resources.size()) return nullptr;
    return m_graph->m_resources[buffer.index].buffer;
}

std::string_view RM::RenderGraphContext::GetPassName() const
{
    return m_graph->m_passes[m_pass].name;
}

RM::RenderGraph::~RenderGraph()
{
    ReleaseTransientMemory();
}

RM::Result<void> RM::RenderGraph::Initialize(Device* device, const RenderGraphCreateInfo& info)
{
    if (!device || !info.frames_in_flight || !info.heap_granularity)
        return std::unexpected("Invalid render graph creation parameters");
    m_device = device;
    m_info = info;
    return {};
}

RM::GraphTexture RM::RenderGraph::CreateTexture(const TextureCreateInfo& info,
                                                std::string_view name)
{
    if (info.initial_data)
    {
        Fail("Transient texture '" + std::string(name) + "' cannot have initial data");
        return {};
    }
    Resource resource{};
    resource.name = name;
    resource.type = ResourceType::Texture;
    resource.texture_info = info;
    m_resources.push_back(std::move(resource));
    return {.index = static_cast<uint32_t>(m_resources.size() - 1)};
}

RM::GraphBuffer RM::RenderGraph::CreateBuffer(const BufferCreateInfo& info, std::string_view name)
{
    if (info.data || !info.size)
    {
        Fail("Transient buffer '" + std::string(name) + "' needs a size and no initial data");
        return {};
    }
    Resource resource{};
    resource.name = name;
    resource.type = ResourceType::Buffer;
    resource.buffer_info = info;
    m_resources.push_back(std::move(resource));
    return {.index = static_cast<uint32_t>(m_resources.size() - 1)};
}

RM::GraphTexture RM::RenderGraph::ImportTexture(Texture* texture,
                                                std::optional<ResourceState> initial_state,
                                                std::optional<ResourceState> final_state)
{
    if (!texture)
    {
        Fail("Cannot import a null texture");
        return {};
    }
    Resource resource{};
    resource.name = "imported texture";
    resource.type = ResourceType::Texture;
    resource.imported = true;
    resource.texture = texture;
    resource.final_state = final_state;
    if (!initial_state) initial_state = GetResourceState(texture);
    resource.tracked = {.state = initial_state.value_or(ResourceState{}), .initialized = true};
    resource.tracked.write = AccessFlags(resource.tracked.state.access).intersects(kWriteAccess);
    m_resources.push_back(std::move(resource));
    return {.index = static_cast<uint32_t>(m_resources.size() - 1)};
}

RM::GraphBuffer RM::RenderGraph::ImportBuffer(Buffer* buffer,
                                              std::optional<ResourceState> initial_state,
                                              std::optional<ResourceState> final_state)
{
    if (!buffer)
    {
        Fail("Cannot import a null buffer");
        return {};
    }
    Resource resource{};
    resource.name = "imported buffer";
    resource.type = ResourceType::Buffer;
    resource.imported = true;
    resource.buffer = buffer;
    resource.final_state = final_state;
    if (!initial_state) initial_state = GetResourceState(buffer);
    resource.tracked = {.state = initial_state.value_or(ResourceState{}), .initialized = true};
    resource.tracked.write = AccessFlags(resource.tracked.state.access).intersects(kWriteAccess);
    m_resources.push_back(std::move(resource));
    return {.index = static_cast<uint32_t>(m_resources.size() - 1)};
}

RM::GraphRenderPassBuilder RM::RenderGraph::AddRenderPass(std::string_view name)
{
    return {this, AddPass(name, GraphPassType::Render)};
}

RM::GraphComputePassBuilder RM::RenderGraph::AddComputePass(std::string_view name)
{
    return {this, AddPass(name, GraphPassType::Compute)};
}

RM::GraphCopyPassBuilder RM::RenderGraph::AddCopyPass(std::string_view name)
{
    return {this, AddPass(name, GraphPassType::Copy)};
}

std::optional<RM::ResourceState> RM::RenderGraph::GetResourceState(const Texture* texture) const
{
    const auto it = m_external_states.find(texture);
    if (it == m_external_states.end()) return std::nullopt;
    return it->second;
}

std::optional<RM::ResourceState> RM::RenderGraph::GetResourceState(const Buffer* buffer) const
{
    const auto it = m_external_states.find(buffer);
    if (it == m_external_states.end()) return std::nullopt;
    return it->second;
}

void RM::RenderGraph::SetResourceState(const Texture* texture, const ResourceState& state)
{
    m_external_states[texture] = state;
}

void RM::RenderGraph::SetResourceState(const Buffer* buffer, const ResourceState& state)
{
    m_external_states[buffer] = state;
}

void RM::RenderGraph::ForgetResource(const Texture* texture)
{
    m_external_states.erase(texture);
}

void RM::RenderGraph::ForgetResource(const Buffer* buffer)
{
    m_external_states.erase(buffer);
}

uint32_t RM::RenderGraph::AddPass(std::string_view name, GraphPassType type)
{
    Pass pass{};
    pass.name = name;
    pass.type = type;
    m_passes.push_back(std::move(pass));
    return static_cast<uint32_t>(m_passes.size() - 1);
}

void RM::RenderGraph::Fail(const std::string& message)
{
    if (m_error.empty()) m_error = message;
}

void RM::RenderGraph::AddUsage(uint32_t pass_index,
                               uint32_t resource,
                               ResourceType type,
                               ResourceUsage usage)
{
    auto& pass = m_passes[pass_index];
    if (resource >= m_resources.size() || m_resources[resource].type != type)
    {
        Fail("Pass '" + pass.name + "' uses an invalid resource handle");
        return;
    }
    if (type == ResourceType::Buffer && !usage.write &&
        AccessFlags(usage.state.access).intersects(kWriteAccess))
    {
        Fail("Pass '" + pass.name + "' reads buffer '" + m_resources[resource].name +
             "' with a write access");
        return;
    }

    usage.resource = resource;
    if (usage.state.stage == BarrierStage::None)
    {
        if (usage.state.access == BarrierAccess::IndexBuffer)
            usage.state.stage = BarrierStage::IndexInput;
        else if (usage.state.access == BarrierAccess::VertexBuffer)
            usage.state.stage = BarrierStage::VertexShading;
        else if (pass.type == GraphPassType::Render)
            usage.state.stage = ToStage(kGraphicsShaderStages);
        else if (pass.type == GraphPassType::Compute)
            usage.state.stage = BarrierStage::ComputeShading;
        else
            usage.state.stage = BarrierStage::Copy;
    }

    const auto existing = std::ranges::find(pass.usages, resource, &ResourceUsage::resource);
    if (existing == pass.usages.end())
    {
        pass.usages.push_back(usage);
        return;
    }
    if (type == ResourceType::Texture && existing->state.layout != usage.state.layout)
    {
        Fail("Pass '" + pass.name + "' uses texture '" + m_resources[resource].name +
             "' with conflicting layouts");
        return;
    }
    existing->state.stage = ToStage(StageFlags(existing->state.stage) | usage.state.stage);
    existing->state.access = MergeAccess(existing->state.access, usage.state.access);
    existing->read = existing->read || usage.read;
    existing->write = existing->write || usage.write;
    existing->overwrite = (existing->overwrite || usage.overwrite) && !existing->read;
}

void RM::RenderGraph::AddColorTarget(uint32_t pass, GraphTexture texture, const ColorTarget& target)
{
    if (m_passes[pass].color_targets.size() >= 8)
    {
        Fail("Pass '" + m_passes[pass].name + "' has more than eight color attachments");
        return;
    }
    const bool load = target.load_op == LoadOp::Load;
    AddUsage(pass,
             texture.index,
             ResourceType::Texture,
             {.state = {.layout = BarrierLayout::RenderTarget,
                        .stage = BarrierStage::RenderTarget,
                        .access = BarrierAccess::RenderTarget},
              .read = load,
              .write = true,
              .overwrite = !load});
    m_passes[pass].color_targets.push_back(target);
}

void RM::RenderGraph::SetDepthTarget(uint32_t pass, GraphTexture texture, const DepthTarget& target)
{
    if (m_passes[pass].depth_target)
    {
        Fail("Pass '" + m_passes[pass].name + "' has more than one depth attachment");
        return;
    }
    const bool load = target.load_op == LoadOp::Load;
    AddUsage(pass,
             texture.index,
             ResourceType::Texture,
             {.state = {.layout = BarrierLayout::DepthStencilWrite,
                        .stage = BarrierStage::DepthStencil,
                        .access = BarrierAccess::DepthStencilWrite},
              .read = load,
              .write = true,
              .overwrite = !load});
    m_passes[pass].depth_target = target;
}

RM::Result<void> RM::RenderGraph::Execute(Command* command)
{
    ++m_frame;
    m_statistics = {};
    m_statistics.declared_passes = static_cast<uint32_t>(m_passes.size());

    Result<void> result{};
    if (!m_error.empty())
        result = std::unexpected("Render graph: " + m_error);
    else if (!command)
        result = std::unexpected("Render graph: Execute requires a command");
    if (result) result = Compile();
    if (result) result = AllocateTransients();
    if (result) result = Record(command);
    CaptureTimeline();
    FinishFrame();
    return result;
}

void RM::RenderGraph::CaptureTimeline()
{
    m_timeline.step_count = static_cast<uint32_t>(m_steps.size());
    m_timeline.passes.clear();
    m_timeline.resources.clear();
    for (const auto& pass : m_passes)
        m_timeline.passes.push_back({.name = pass.name, .type = pass.type});
    for (uint32_t step = 0; step < m_steps.size(); ++step)
        for (const auto pass : m_steps[step].passes) m_timeline.passes[pass].step = step;
    for (const auto& resource : m_resources)
    {
        if (resource.imported || resource.first_step == UINT32_MAX) continue;
        m_timeline.resources.push_back({.name = resource.name,
                                        .texture = resource.type == ResourceType::Texture,
                                        .first_step = resource.first_step,
                                        .last_step = resource.last_step,
                                        .offset = resource.offset,
                                        .size = resource.allocation.size});
    }
}

RM::Result<void> RM::RenderGraph::Compile()
{
    CullPasses();
    SchedulePasses();

    for (uint32_t step = 0; step < m_steps.size(); ++step)
    {
        for (const auto pass_index : m_steps[step].passes)
        {
            for (const auto& usage : m_passes[pass_index].usages)
            {
                auto& resource = m_resources[usage.resource];
                resource.first_step = std::min(resource.first_step, step);
                resource.last_step = std::max(resource.last_step, step);
                if (resource.imported) continue;

                if (resource.type == ResourceType::Texture)
                {
                    auto& texture_usage = resource.texture_info.usage;
                    switch (usage.state.layout)
                    {
                        case BarrierLayout::RenderTarget:
                        case BarrierLayout::DepthStencilWrite:
                            texture_usage |= TextureUsage::RenderTarget;
                            break;
                        case BarrierLayout::ShaderResource:
                            texture_usage |= TextureUsage::ShaderRead;
                            break;
                        case BarrierLayout::UnorderedAccess:
                            texture_usage |= TextureUsage::ShaderWrite;
                            break;
                        default:
                            break;
                    }
                }
                else
                {
                    auto& flags = resource.buffer_info.flags;
                    flags |= BufferFlags::eUnorderedAccess;
                    if (usage.state.access == BarrierAccess::ShaderResource)
                        flags |= BufferFlags::eShaderResource;
                }
            }
        }
    }
    return {};
}

void RM::RenderGraph::CullPasses()
{
    std::vector<bool> needed(m_resources.size());
    for (size_t i = 0; i < m_resources.size(); ++i) needed[i] = m_resources[i].imported;

    for (auto pass = m_passes.rbegin(); pass != m_passes.rend(); ++pass)
    {
        pass->live = pass->side_effect;
        for (const auto& usage : pass->usages)
            if (usage.write && needed[usage.resource]) pass->live = true;
        if (!pass->live)
        {
            ++m_statistics.culled_passes;
            continue;
        }
        for (const auto& usage : pass->usages)
            if (usage.overwrite) needed[usage.resource] = false;
        for (const auto& usage : pass->usages)
            if (usage.read) needed[usage.resource] = true;
    }
}

bool RM::RenderGraph::PassesConflict(uint32_t a, uint32_t b) const
{
    for (const auto& usage_a : m_passes[a].usages)
    {
        for (const auto& usage_b : m_passes[b].usages)
        {
            if (usage_a.resource == usage_b.resource && (usage_a.write || usage_b.write))
                return true;
        }
    }
    return false;
}

void RM::RenderGraph::SchedulePasses()
{
    std::vector<uint32_t> order;
    for (uint32_t i = 0; i < m_passes.size(); ++i)
        if (m_passes[i].live) order.push_back(i);

    auto is_parallel = [&](uint32_t pass)
    {
        return m_passes[pass].type == GraphPassType::Compute && m_passes[pass].parallel;
    };

    std::vector<bool> scheduled(order.size());
    for (size_t i = 0; i < order.size(); ++i)
    {
        if (scheduled[i]) continue;
        scheduled[i] = true;
        Step step{.type = m_passes[order[i]].type,
                  .parallel = is_parallel(order[i]),
                  .passes = {order[i]}};

        if (step.parallel)
        {
            for (size_t j = i + 1; j < order.size(); ++j)
            {
                if (scheduled[j] || !is_parallel(order[j])) continue;
                bool independent = true;
                for (const auto batched : step.passes)
                    independent = independent && !PassesConflict(order[j], batched);
                for (size_t k = i + 1; k < j && independent; ++k)
                    if (!scheduled[k]) independent = !PassesConflict(order[j], order[k]);
                if (!independent) continue;
                step.passes.push_back(order[j]);
                scheduled[j] = true;
            }
            if (step.passes.size() > 1) ++m_statistics.parallel_batches;
        }
        m_statistics.executed_passes += static_cast<uint32_t>(step.passes.size());
        m_steps.push_back(std::move(step));
    }
}

RM::Result<void> RM::RenderGraph::AllocateTransients()
{
    std::vector<uint32_t> transients;
    for (uint32_t i = 0; i < m_resources.size(); ++i)
    {
        auto& resource = m_resources[i];
        if (resource.imported || resource.first_step == UINT32_MAX) continue;
        auto allocation = resource.type == ResourceType::Texture
                              ? m_device->GetAllocationInfo(resource.texture_info)
                              : m_device->GetAllocationInfo(resource.buffer_info);
        if (!allocation)
            return std::unexpected("Render graph: resource '" + resource.name +
                                   "': " + allocation.error());
        resource.allocation = *allocation;
        transients.push_back(i);
        m_statistics.transient_memory_unaliased +=
            AlignUp(resource.allocation.size, resource.allocation.alignment);
        if (resource.type == ResourceType::Texture)
            ++m_statistics.transient_textures;
        else
            ++m_statistics.transient_buffers;
    }
    if (transients.empty()) return {};

    std::ranges::stable_sort(transients,
                             [&](uint32_t a, uint32_t b)
                             {
                                 return m_resources[a].allocation.size >
                                        m_resources[b].allocation.size;
                             });
    auto lifetimes_overlap = [&](const Resource& a, const Resource& b)
    {
        return a.first_step <= b.last_step && b.first_step <= a.last_step;
    };

    std::vector<uint32_t> placed;
    std::vector<std::pair<uint64_t, uint64_t>> occupied;
    uint64_t required = 0;
    for (const auto index : transients)
    {
        auto& resource = m_resources[index];
        occupied.clear();
        for (const auto other_index : placed)
        {
            const auto& other = m_resources[other_index];
            if (lifetimes_overlap(resource, other))
                occupied.emplace_back(other.offset, other.offset + other.allocation.size);
        }
        std::ranges::sort(occupied);

        uint64_t candidate = 0;
        for (const auto& [begin, end] : occupied)
        {
            if (AlignUp(candidate, resource.allocation.alignment) + resource.allocation.size <=
                begin)
                break;
            candidate = std::max(candidate, end);
        }
        resource.offset = AlignUp(candidate, resource.allocation.alignment);
        required = std::max(required, resource.offset + resource.allocation.size);
        placed.push_back(index);
    }
    m_statistics.transient_memory = required;

    for (const auto index : transients)
    {
        auto& resource = m_resources[index];
        for (const auto other_index : transients)
        {
            const auto& other = m_resources[other_index];
            if (other_index == index || other.last_step >= resource.first_step) continue;
            if (other.offset < resource.offset + resource.allocation.size &&
                resource.offset < other.offset + other.allocation.size)
                resource.alias_predecessors.push_back(other_index);
        }
    }

    if (auto result = EnsureHeap(required); !result) return result;
    m_statistics.heap_size = m_heap->GetSize();

    for (const auto index : transients)
    {
        auto& resource = m_resources[index];
        if (resource.type == ResourceType::Texture)
        {
            auto texture = AcquireTexture(resource);
            if (!texture)
                return std::unexpected("Render graph: texture '" + resource.name +
                                       "': " + texture.error());
            resource.texture = *texture;
        }
        else
        {
            auto buffer = AcquireBuffer(resource);
            if (!buffer)
                return std::unexpected("Render graph: buffer '" + resource.name +
                                       "': " + buffer.error());
            resource.buffer = *buffer;
        }
    }
    return {};
}

RM::Result<void> RM::RenderGraph::EnsureHeap(uint64_t size)
{
    if (m_heap && m_heap->GetSize() >= size) return {};

    ReleaseTransientMemory();

    auto heap = m_device->CreateHeap({.size = AlignUp(size, m_info.heap_granularity),
                                      .memory = MemoryType::DeviceLocal});
    if (!heap) return std::unexpected("Render graph: " + heap.error());
    m_heap = *heap;
    return {};
}

RM::Result<RM::Texture*> RM::RenderGraph::AcquireTexture(const Resource& resource)
{
    for (auto& cached : m_texture_cache)
    {
        if (cached.in_use || cached.offset != resource.offset ||
            !SameTexture(cached.info, resource.texture_info))
            continue;
        cached.in_use = true;
        cached.last_used = m_frame;
        return cached.texture;
    }
    auto texture = m_heap->CreatePlacedTexture(resource.texture_info, resource.offset);
    if (!texture) return std::unexpected(texture.error());
    m_texture_cache.push_back({.info = resource.texture_info,
                               .offset = resource.offset,
                               .texture = *texture,
                               .last_used = m_frame,
                               .in_use = true});
    return *texture;
}

RM::Result<RM::Buffer*> RM::RenderGraph::AcquireBuffer(const Resource& resource)
{
    for (auto& cached : m_buffer_cache)
    {
        if (cached.in_use || cached.offset != resource.offset ||
            !SameBuffer(cached.info, resource.buffer_info))
            continue;
        cached.in_use = true;
        cached.last_used = m_frame;
        return cached.buffer;
    }
    auto buffer = m_heap->CreatePlacedBuffer(resource.buffer_info, resource.offset);
    if (!buffer) return std::unexpected(buffer.error());
    m_buffer_cache.push_back({.info = resource.buffer_info,
                              .offset = resource.offset,
                              .buffer = *buffer,
                              .last_used = m_frame,
                              .in_use = true});
    return *buffer;
}

RM::Result<void> RM::RenderGraph::Transition(const ResourceUsage& usage,
                                             std::vector<TextureBarrier>& textures,
                                             std::vector<BufferBarrier>& buffers)
{
    auto& resource = m_resources[usage.resource];
    auto& tracked = resource.tracked;
    const bool is_texture = resource.type == ResourceType::Texture;

    if (usage.read && !tracked.initialized)
        return std::unexpected("reads '" + resource.name + "' before anything wrote it");

    ResourceState before = tracked.state;
    ResourceState after = usage.state;
    bool discard = false;
    if (!resource.imported && !tracked.touched)
    {
        StageFlags wait{};
        for (const auto predecessor : resource.alias_predecessors)
            wait |= m_resources[predecessor].tracked.state.stage;
        if (resource.alias_predecessors.empty()) wait = kAllStages;
        before = {.layout = BarrierLayout::Undefined,
                  .stage = ToStage(wait),
                  .access = BarrierAccess::NoAccess};
        discard = is_texture && NeedsDiscard(after.layout);
    }
    else if ((!is_texture || before.layout == after.layout) && !tracked.write && !usage.write)
    {
        if (AccessFlags(before.access).contains(after.access) &&
            StageFlags(before.stage).contains(after.stage))
            return {};
        after.stage = ToStage(StageFlags(before.stage) | after.stage);
        after.access = MergeAccess(before.access, after.access);
    }

    if (is_texture)
    {
        textures.push_back(TextureBarrierBuilder(resource.texture)
                           .BeforeLayout(before.layout)
                           .AfterLayout(after.layout)
                           .BeforeStage(before.stage)
                           .AfterStage(after.stage)
                           .BeforeAccess(before.access)
                           .AfterAccess(after.access)
                           .Subresources(WholeTexture(resource.texture))
                           .Discard(discard)
                           .Build());
    }
    else
    {
        buffers.push_back(BufferBarrierBuilder(resource.buffer)
                          .BeforeStage(before.stage)
                          .AfterStage(after.stage)
                          .BeforeAccess(before.access)
                          .AfterAccess(after.access)
                          .Build());
    }
    tracked.state = after;
    tracked.write = usage.write;
    tracked.touched = true;
    if (!is_texture) tracked.state.layout = BarrierLayout::Common;
    return {};
}

RM::Result<void> RM::RenderGraph::Record(Command* command)
{
    for (size_t i = 0; i < m_steps.size(); ++i)
    {
        if (auto result = RecordStep(command, m_steps[i], i ? &m_steps[i - 1] : nullptr); !result)
            return result;
    }

    std::vector<TextureBarrier> textures;
    std::vector<BufferBarrier> buffers;
    for (auto& resource : m_resources)
    {
        if (!resource.imported) continue;
        const void* key = resource.type == ResourceType::Texture
                              ? static_cast<const void*>(resource.texture)
                              : static_cast<const void*>(resource.buffer);
        auto& tracked = resource.tracked;
        if (resource.final_state && !SameState(*resource.final_state, tracked.state))
        {
            const auto& final_state = *resource.final_state;
            if (resource.type == ResourceType::Texture)
            {
                textures.push_back(TextureBarrierBuilder(resource.texture)
                                   .BeforeLayout(tracked.state.layout)
                                   .AfterLayout(final_state.layout)
                                   .BeforeStage(tracked.state.stage)
                                   .AfterStage(final_state.stage)
                                   .BeforeAccess(tracked.state.access)
                                   .AfterAccess(final_state.access)
                                   .Subresources(WholeTexture(resource.texture))
                                   .Build());
            }
            else
            {
                buffers.push_back(BufferBarrierBuilder(resource.buffer)
                                  .BeforeStage(tracked.state.stage)
                                  .AfterStage(final_state.stage)
                                  .BeforeAccess(tracked.state.access)
                                  .AfterAccess(final_state.access)
                                  .Build());
            }
            tracked.state = final_state;
        }
        m_external_states[key] = tracked.state;
    }
    if (!textures.empty() || !buffers.empty())
    {
        command->Barrier(BarrierGroup{.texture_barriers = textures, .buffer_barriers = buffers});
        m_statistics.barriers += static_cast<uint32_t>(textures.size() + buffers.size());
    }
    return {};
}

RM::Result<void> RM::RenderGraph::RecordStep(Command* command,
                                             const Step& step,
                                             const Step* previous)
{
    auto fail = [&](uint32_t pass, const std::string& message) -> Result<void>
    {
        return std::unexpected("Render graph pass '" + m_passes[pass].name + "': " + message);
    };

    std::vector<ResourceUsage> usages;
    for (const auto pass_index : step.passes)
    {
        for (const auto& usage : m_passes[pass_index].usages)
        {
            const auto existing =
                std::ranges::find(usages, usage.resource, &ResourceUsage::resource);
            if (existing == usages.end())
            {
                usages.push_back(usage);
                continue;
            }
            existing->state.stage = ToStage(StageFlags(existing->state.stage) | usage.state.stage);
            existing->state.access = MergeAccess(existing->state.access, usage.state.access);
            existing->read = existing->read || usage.read;
        }
    }

    std::vector<GlobalBarrier> globals;
    std::vector<TextureBarrier> textures;
    std::vector<BufferBarrier> buffers;
    for (const auto& usage : usages)
    {
        if (auto result = Transition(usage, textures, buffers); !result)
            return fail(step.passes.front(), result.error());
    }
    if (step.type == GraphPassType::Compute && !step.parallel && previous &&
        previous->type == GraphPassType::Compute)
    {
        globals.push_back(GlobalBarrierBuilder()
                          .BeforeStage(BarrierStage::ComputeShading)
                          .AfterStage(BarrierStage::ComputeShading)
                          .BeforeAccess(BarrierAccess::UnorderedAccess)
                          .AfterAccess(BarrierAccess::UnorderedAccess)
                          .Build());
    }
    if (!globals.empty() || !textures.empty() || !buffers.empty())
    {
        command->Barrier(BarrierGroup{.global_barriers = globals,
                                      .texture_barriers = textures,
                                      .buffer_barriers = buffers});
        m_statistics.barriers +=
            static_cast<uint32_t>(globals.size() + textures.size() + buffers.size());
    }
    for (const auto& usage : usages)
        if (usage.write) m_resources[usage.resource].tracked.initialized = true;

    switch (step.type)
    {
        case GraphPassType::Render:
        {
            const auto pass_index = step.passes.front();
            const auto& pass = m_passes[pass_index];
            if (pass.color_targets.empty() && !pass.depth_target)
                return fail(pass_index, "render passes need at least one attachment");

            const Texture* first = pass.color_targets.empty()
                                       ? m_resources[pass.depth_target->resource].texture
                                       : m_resources[pass.color_targets.front().resource].texture;
            const UInt2 size = first->GetDimensions();
            RenderPassBuilder builder(size);
            for (const auto& target : pass.color_targets)
            {
                const auto* texture = m_resources[target.resource].texture;
                const auto dimensions = texture->GetDimensions();
                if (dimensions.x != size.x || dimensions.y != size.y)
                    return fail(pass_index, "attachments must have the same size");
                builder.Color(texture, target.load_op, target.store_op, target.clear_color);
            }
            if (pass.depth_target)
            {
                const auto& target = *pass.depth_target;
                const auto* texture = m_resources[target.resource].texture;
                const auto dimensions = texture->GetDimensions();
                if (dimensions.x != size.x || dimensions.y != size.y)
                    return fail(pass_index, "attachments must have the same size");
                builder.Depth(texture,
                              target.load_op,
                              target.store_op,
                              target.clear_depth,
                              target.clear_stencil);
            }

            command->BeginRenderPass(builder.Build());
            command->SetViewport(pass.viewport.value_or(
                Viewport{.offset = {}, .size = size, .depth_range = {.x = 0.f, .y = 1.f}}));
            command->SetScissor(pass.scissor.value_or(Rect{.offset = {}, .size = size}));
            auto result = RecordPass(command, pass_index, size);
            command->EndRenderPass();
            if (!result) return fail(pass_index, result.error());
            break;
        }
        case GraphPassType::Compute:
        {
            command->BeginComputePass();
            for (const auto pass_index : step.passes)
            {
                if (auto result = RecordPass(command, pass_index, {}); !result)
                {
                    command->EndComputePass();
                    return fail(pass_index, result.error());
                }
            }
            command->EndComputePass();
            break;
        }
        case GraphPassType::Copy:
        {
            const auto pass_index = step.passes.front();
            if (auto result = RecordPass(command, pass_index, {}); !result)
                return fail(pass_index, result.error());
            break;
        }
    }
    return {};
}

RM::Result<void> RM::RenderGraph::RecordPass(Command* command, uint32_t pass_index, UInt2 area)
{
    const auto& pass = m_passes[pass_index];
    if (pass.pipeline)
    {
        if (auto result = command->BindShader(pass.pipeline); !result) return result;
    }
    for (const auto& binding : pass.constant_buffers)
    {
        const Buffer* buffer = binding.resource == UINT32_MAX
                                   ? binding.buffer
                                   : m_resources[binding.resource].buffer;
        if (auto result = command->BindConstantBuffer(binding.slot, buffer); !result)
            return result;
    }
    for (const auto& binding : pass.root_constants)
    {
        if (auto result = command->BindRootConstants(binding.data.data(),
                                                     static_cast<uint32_t>(binding.data.size()),
                                                     binding.offset);
            !result)
            return result;
    }
    for (const auto& binding : pass.resource_indices)
    {
        if (binding.resource >= m_resources.size())
            return std::unexpected("uses an invalid resource handle for a resource index");
        const auto& resource = m_resources[binding.resource];
        uint32_t index = UINT32_MAX;
        if (resource.type == ResourceType::Buffer)
        {
            index = resource.buffer->GetResourceIndex();
        }
        else
        {
            const auto usage =
                std::ranges::find(pass.usages, binding.resource, &ResourceUsage::resource);
            if (usage == pass.usages.end())
                return std::unexpected("needs a resource index of texture '" + resource.name +
                                       "' which the pass does not use");
            switch (usage->state.layout)
            {
                case BarrierLayout::ShaderResource:
                    index = resource.texture->GetResourceIndex(TextureViewType::ShaderRead);
                    break;
                case BarrierLayout::UnorderedAccess:
                    index = resource.texture->GetResourceIndex(TextureViewType::ShaderReadWrite);
                    break;
                default:
                    return std::unexpected("needs a resource index of texture '" + resource.name +
                                           "' which the pass does not read or write in a shader");
            }
        }
        if (auto result = command->BindRootConstants(&index, sizeof(index), binding.offset);
            !result)
            return result;
    }
    if (pass.index_buffer)
    {
        const Buffer* buffer = pass.index_buffer->resource == UINT32_MAX
                                   ? pass.index_buffer->buffer
                                   : m_resources[pass.index_buffer->resource].buffer;
        if (auto result = command->BindIndexBuffer(buffer, pass.index_buffer->format); !result)
            return result;
    }
    bool draws = false;
    for (const auto& pass_command : pass.commands)
        draws = draws || std::holds_alternative<DrawCommand>(pass_command) ||
                std::holds_alternative<DrawIndexedCommand>(pass_command);
    if (pass.topology || draws)
        command->SetPrimitiveTopology(pass.topology.value_or(PrimitiveTopology::TriangleList));

    if (pass.execute)
    {
        RenderGraphContext context(this, command, pass_index, area);
        if (auto result = pass.execute(context); !result) return result;
    }

    for (const auto& pass_command : pass.commands)
    {
        auto result = std::visit(
            [&](const auto& recorded) -> Result<void>
            {
                using T = std::decay_t<decltype(recorded)>;
                if constexpr (std::is_same_v<T, DrawCommand>)
                    return command->Draw(recorded.vertex_count,
                                         recorded.instance_count,
                                         recorded.first_vertex,
                                         recorded.first_instance);
                else if constexpr (std::is_same_v<T, DrawIndexedCommand>)
                    return command->DrawIndexed(recorded.index_count,
                                                recorded.instance_count,
                                                recorded.first_index,
                                                recorded.vertex_offset,
                                                recorded.first_instance);
                else if constexpr (std::is_same_v<T, DispatchMeshCommand>)
                    return command->DispatchMesh(recorded.group);
                else if constexpr (std::is_same_v<T, DispatchCommand>)
                    return command->Dispatch(recorded.group);
                else if constexpr (std::is_same_v<T, BlitCommand>)
                    return command->Blit(m_resources[recorded.source].texture,
                                         m_resources[recorded.destination].texture,
                                         recorded.region);
                else if constexpr (std::is_same_v<T, CopyTextureCommand>)
                    return command->CopyTexture(m_resources[recorded.source].texture,
                                                m_resources[recorded.destination].texture);
                else if constexpr (std::is_same_v<T, CopyBufferCommand>)
                    return command->CopyBuffer(m_resources[recorded.source].buffer,
                                               m_resources[recorded.destination].buffer,
                                               recorded.source_offset,
                                               recorded.destination_offset,
                                               recorded.size);
                else
                    return command->CopyBufferToTexture(m_resources[recorded.source].buffer,
                                                        m_resources[recorded.destination].texture,
                                                        recorded.region);
            },
            pass_command);
        if (!result) return result;
    }
    return {};
}

void RM::RenderGraph::FinishFrame()
{
    m_passes.clear();
    m_resources.clear();
    m_steps.clear();
    m_error.clear();
    for (auto& cached : m_texture_cache) cached.in_use = false;
    for (auto& cached : m_buffer_cache) cached.in_use = false;
    ReleaseUnusedResources(false);
}

void RM::RenderGraph::ReleaseUnusedResources(bool release_all)
{
    auto expired = [&](uint64_t last_used)
    {
        return release_all || m_frame - last_used >= m_info.frames_in_flight;
    };

    std::erase_if(m_texture_cache,
                  [&](const CachedTexture& cached)
                  {
                      if (!expired(cached.last_used)) return false;
                      m_device->Retire(cached.texture);
                      return true;
                  });
    std::erase_if(m_buffer_cache,
                  [&](const CachedBuffer& cached)
                  {
                      if (!expired(cached.last_used)) return false;
                      m_device->Retire(cached.buffer);
                      return true;
                  });
}

void RM::RenderGraph::ReleaseTransientMemory()
{
    if (!m_device) return;
    ReleaseUnusedResources(true);
    m_device->Retire(m_heap);
    m_heap = nullptr;
}