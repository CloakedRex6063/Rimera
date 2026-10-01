#include "rimera_command.hpp"
#include "rimera_buffer.hpp"
#include "rimera_device.hpp"
#include "rimera_helpers.hpp"
#include "rimera_metal.hpp"
#include "rimera_texture.hpp"
#include "stdexcept"
#include "rimera_builders.hpp"

RM::Command::Command(Device* device, QueueType) : m_device(device)
{
    auto* native_device = static_cast<MTL::Device*>(device->GetNativeHandle());
    m_command_buffer = NS::TransferPtr(native_device->newCommandBuffer());
    m_command_allocator = NS::TransferPtr(native_device->newCommandAllocator());

    m_blit_buffer =
        device->CreateBuffer({.size = sizeof(BlitPassData), .data = nullptr});

    SamplerCreateInfo create_info
    {
        .min_filter = Filter::Nearest,
        .mag_filter = Filter::Nearest,
        .wrap_u = Wrap::ClampToEdge,
        .wrap_v = Wrap::ClampToEdge,
        .wrap_w = Wrap::ClampToEdge,
        .min_lod = 0.f,
        .max_lod = 0.f,
        .max_anisotropy = 0,
        .border_color = {},
        .comparison_func = ComparisonFunc::Never,
        .reduction_type = ReductionType::Standard,
    };
    m_sampler = device->CreateSampler(create_info);

    m_device->GetCoreResidencySet()->AddAllocation(m_blit_buffer);
    m_device->GetCoreResidencySet()->Commit();
    m_device->GetCoreResidencySet()->RequestResidency();
}

RM::Command::~Command()
{
    m_command_buffer.reset();
    m_command_allocator.reset();
}

void RM::Command::Begin()
{
    m_index_buffer = {};
    m_command_allocator->reset();
    m_command_buffer->beginCommandBuffer(m_command_allocator.get());
}

void RM::Command::End() const { m_command_buffer->endCommandBuffer(); }

void RM::Command::BeginRenderPass(const RenderPassCreateInfo& info)
{
    auto rpd = NS::TransferPtr(MTL4::RenderPassDescriptor::alloc()->init());

    for (uint32_t i = 0; i < info.color_attachments.size(); i++)
    {
        auto info_attach = info.color_attachments[i];
        auto* attachment = rpd->colorAttachments()->object(i);
        auto [r, g, b, a] = info_attach.clear_color;
        attachment->setTexture(static_cast<MTL::Texture*>(info_attach.texture->GetView()));
        attachment->setClearColor(MTL::ClearColor(r, g, b, a));
        attachment->setLoadAction(static_cast<MTL::LoadAction>(info_attach.load_op));
        attachment->setStoreAction(static_cast<MTL::StoreAction>(info_attach.store_op));
    }

    if (info.depth_attachment.has_value())
    {
        auto info_attach = info.depth_attachment.value();
        auto* attachment = rpd->depthAttachment();
        attachment->setTexture(static_cast<MTL::Texture*>(info_attach.texture->GetView()));
        attachment->setLoadAction(static_cast<MTL::LoadAction>(info_attach.load_op));
        attachment->setStoreAction(static_cast<MTL::StoreAction>(info_attach.store_op));
    }

    m_render_encoder = m_command_buffer->renderCommandEncoder(rpd.get());
    
    for (auto& barrier : m_pending_barriers)
    {
        m_render_encoder->barrierAfterQueueStages(barrier.src_stage,
                                                   barrier.dst_stage,
                                                   MTL4::VisibilityOptionDevice);
    }
    m_pending_barriers.clear();
    
    m_render_encoder->setArgumentTable(m_device->m_arg_table.get(),
                                       MTL::RenderStageVertex | MTL::RenderStageFragment);
}

void RM::Command::EndRenderPass()
{
    m_render_encoder->endEncoding();
    m_render_encoder = nullptr;
}

void RM::Command::Draw(uint32_t vertex_count,
                       uint32_t instance_count,
                       uint32_t start_vertex,
                       uint32_t start_instance) const
{
    m_render_encoder->drawPrimitives(m_primitive_type,
                                     start_vertex,
                                     vertex_count,
                                     instance_count,
                                     start_instance);
}

void RM::Command::DrawIndexed(uint32_t index_count,
                              uint32_t instance_count,
                              uint32_t start_index,
                              uint32_t start_vertex,
                              uint32_t start_instance) const
{
    if (!m_render_encoder)
        throw std::runtime_error("BeginRenderPass is required before drawing indexed primitives");
    if (!m_index_buffer.index_size)
        throw std::runtime_error("BindIndexBuffer is required before drawing indexed primitives");

    const uint64_t offset = uint64_t{start_index} * m_index_buffer.index_size;
    const uint64_t size = uint64_t{index_count} * m_index_buffer.index_size;
    if (offset > m_index_buffer.length || size > m_index_buffer.length - offset)
        throw std::out_of_range("Indexed draw exceeds the bound index buffer");

    m_render_encoder->drawIndexedPrimitives(m_primitive_type,
                                            index_count,
                                            m_index_buffer.index_size == 2
                                                ? MTL::IndexTypeUInt16
                                                : MTL::IndexTypeUInt32,
                                            m_index_buffer.address + offset,
                                            m_index_buffer.length - offset,
                                            instance_count,
                                            start_vertex,
                                            start_instance);
}

void RM::Command::BindIndexBuffer(const Buffer* buffer, Format format)
{
    if (!buffer) throw std::invalid_argument("Cannot bind a null index buffer");
    if (format != Format::R16_UInt && format != Format::R32_UInt)
        throw std::invalid_argument("Index buffers require R16_UInt or R32_UInt format");

    m_index_buffer = {
        .address = buffer->GetGPUAddress(),
        .length = buffer->GetLength(),
        .index_size = format == Format::R16_UInt ? 2u : 4u,
    };
}

void RM::Command::BindShader(const Pipeline* shader)
{
    if (!shader) throw std::invalid_argument("Cannot bind a null pipeline");
    if (shader->m_device != m_device)
        throw std::invalid_argument("Pipeline belongs to a different device");

    switch (shader->GetShaderType())
    {
        case PipelineType::Compute:
            if (!m_compute_encoder)
                throw std::runtime_error(
                    "BeginComputePass is required before binding a compute pipeline");
            m_compute_encoder->setComputePipelineState(shader->m_compute_pso.get());
            m_threads_per_compute = shader->GetComputeThreadsPerThreadgroup();
            break;
        case PipelineType::Graphics:
        {
            if (!m_render_encoder)
                throw std::runtime_error(
                    "BeginRenderPass is required before binding a graphics pipeline");
            const auto& rasterizer = shader->m_rasterizer_state;
            m_render_encoder->setRenderPipelineState(shader->m_render_pso.get());
            m_render_encoder->setDepthStencilState(shader->m_depth_stencil_state.get());
            m_render_encoder->setCullMode(Metal::ToCullMode(rasterizer.cull_mode));
            m_render_encoder->setFrontFacingWinding(Metal::ToWinding(rasterizer.front_face));
            m_render_encoder->setTriangleFillMode(Metal::ToTriangleFillMode(rasterizer.fill_mode));
            m_render_encoder->setDepthBias(static_cast<float>(rasterizer.depth_bias),
                                           rasterizer.slope_scaled_depth_bias,
                                           rasterizer.depth_bias_clamp);
            m_render_encoder->setDepthClipMode(rasterizer.depth_clip_enable
                                                   ? MTL::DepthClipModeClip
                                                   : MTL::DepthClipModeClamp);
            m_render_encoder->setStencilReferenceValue(0);
            m_render_encoder->setBlendColor(0, 0, 0, 0);
            break;
        }
    }
}

void RM::Command::Barrier(const BarrierGroup& group)
{
    auto accumulate = [&](const auto& barriers)
    {
        for (const auto& barrier : barriers)
        {
            const auto src = Metal::ToBarrierStages(barrier.scope.sync_before);
            const auto dst = Metal::ToBarrierStages(barrier.scope.sync_after);

            if (!src || !dst)
                continue;

            auto it = std::ranges::find_if(
                m_pending_barriers,
                [&](const PendingBarrier& pending)
                {
                    return pending.src_stage == src ||
                           pending.dst_stage == dst;
                });

            if (it != m_pending_barriers.end())
            {
                if (it->src_stage == src)
                {
                    it->dst_stage |= dst;
                }
                else
                {
                    it->src_stage |= src;
                }

                continue;
            }

            m_pending_barriers.push_back({src, dst});
        }
    };

    accumulate(group.global_barriers);
    accumulate(group.texture_barriers);
    accumulate(group.buffer_barriers);
}

void RM::Command::Barrier(std::span<const TextureBarrier> barriers)
{
    Barrier(BarrierGroup{.texture_barriers = barriers});
}

void RM::Command::Barrier(const TextureBarrier& barrier)
{
    Barrier(std::span{&barrier, 1});
}

void RM::Command::Barrier(std::span<const BufferBarrier> barriers)
{
    Barrier(BarrierGroup{.buffer_barriers = barriers});
}

void RM::Command::Barrier(const BufferBarrier& barrier)
{
    Barrier(std::span{&barrier, 1});
}

void RM::Command::Barrier(std::span<const GlobalBarrier> barriers)
{
    Barrier(BarrierGroup{.global_barriers = barriers});
}

void RM::Command::Barrier(const GlobalBarrier& barrier)
{
    Barrier(std::span{&barrier, 1});
}

void RM::Command::SetViewport(const Viewport& viewport) const
{
    MTL::Viewport mtl_viewport
    {
        .originX = static_cast<double>(viewport.offset.x),
        .originY = static_cast<double>(viewport.offset.y),
        .width = static_cast<double>(viewport.size.x),
        .height = static_cast<double>(viewport.size.y),
        .znear = static_cast<double>(viewport.depth_range.x),
        .zfar = static_cast<double>(viewport.depth_range.y)
    };
    m_render_encoder->setViewport(mtl_viewport);
}

void RM::Command::SetScissor(const Rect& rect) const
{
    MTL::ScissorRect scissor
    {
        .x = rect.offset.x,
        .y = rect.offset.y,
        .width = rect.size.x,
        .height = rect.size.y
    };
    m_render_encoder->setScissorRect(scissor);
}

void RM::Command::SetPrimitiveTopology(PrimitiveTopology topology)
{
    m_primitive_type = Metal::ToPrimitiveType(topology);
}

void RM::Command::CopyTexture(const Texture* source, const Texture* dest)
{
    GetComputeEncoder();
    const auto* src = static_cast<MTL::Texture*>(source->GetNativeHandle());
    const auto* dst = static_cast<MTL::Texture*>(dest->GetNativeHandle());

    m_compute_encoder->copyFromTexture(src, dst);

    m_compute_encoder->endEncoding();
    m_compute_encoder = nullptr;
}

void RM::Command::Blit(const TextureView* source, const TextureView* dest, const BlitRegion& region)
{
    auto src_dim = source->GetTexture()->GetDimensions();
    auto dst_dim = dest->GetTexture()->GetDimensions();

    if (!m_device->m_blit_pipelines.contains(dest->GetFormat()))
    {
        m_device->m_blit_pipelines[dest->GetFormat()] = Helpers::CreateBlitShader(
            m_device,
            dest->GetFormat());
    }

    uint32_t src_w = std::max(1u, src_dim.x >> region.src_mip);
    uint32_t src_h = std::max(1u, src_dim.y >> region.src_mip);

    Rect src = Helpers::IsEmpty(region.src_rect)
                   ? Rect{.offset = {}, .size = {.x = src_w, .y = src_h}}
                   : region.src_rect;
    Rect dst = Helpers::IsEmpty(region.dst_rect)
                   ? Rect{.offset = {}, .size = {.x = dst_dim.x, .y = dst_dim.y}}
                   : region.dst_rect;

    if (src.size.x == 0 || src.size.y == 0 || dst.size.x == 0 || dst.size.y == 0)
        return;

    auto* pass_data = static_cast<BlitPassData*>(m_blit_buffer->Map());
    *pass_data = {
        .src_index = source->GetResourceIndex(),
        .sampler_index = m_sampler->GetSamplerIndex(),
        .src_mip = region.src_mip,
        .uv_rect = {
            .x = static_cast<float>(src.offset.x) / static_cast<float>(src_w),
            .y = static_cast<float>(src.offset.y) / static_cast<float>(src_h),
            .z = static_cast<float>(src.size.x) / static_cast<float>(src_w),
            .w = static_cast<float>(src.size.y) / static_cast<float>(src_h),
        }
    };

    BeginRenderPass(RenderPassBuilder(dst_dim).Color(dest).Build());

    BindShader(m_device->m_blit_pipelines[dest->GetFormat()]);
    BindConstantBuffer(1, m_blit_buffer);

    SetViewport(Viewport{
        .offset = {.x = dst.offset.x, .y = dst.offset.y},
        .size = {.x = dst.size.x, .y = dst.size.y},
        .depth_range = {.x = 0.0f, .y = 1.0f},
    });
    SetScissor(dst);
    SetPrimitiveTopology(PrimitiveTopology::TriangleList);

    Draw(3, 1, 0, 0);
    EndRenderPass();
}

void RM::Command::DispatchMesh(DispatchGroup dispatch_group) const
{
    m_render_encoder->drawMeshThreadgroups(
        MTL::Size(dispatch_group.x, dispatch_group.y, dispatch_group.z),
        MTL::Size(m_threads_per_ampl.x, m_threads_per_ampl.y, m_threads_per_ampl.z),
        MTL::Size(m_threads_per_mesh.x, m_threads_per_mesh.y, m_threads_per_mesh.z));
}

void RM::Command::Dispatch(DispatchGroup dispatch_group)
{
    GetComputeEncoder();
    m_compute_encoder->dispatchThreads(
        MTL::Size(dispatch_group.x, dispatch_group.y, dispatch_group.z),
        MTL::Size(m_threads_per_compute.x, m_threads_per_compute.y, m_threads_per_compute.z));
    m_compute_encoder->endEncoding();
    m_compute_encoder = nullptr;
}

void RM::Command::BindConstantBuffer(const uint32_t slot, const Buffer* buffer) const
{
    if (slot > 1) throw std::runtime_error("Only slot 0 and 1 are bindable");
    auto* args = static_cast<uint8_t*>(m_device->GetArgBuffer()->contents());
    uint64_t address = buffer->GetGPUAddress();
    std::memcpy(args + slot * sizeof(uint64_t), &address, sizeof(uint64_t));
}

void* RM::Command::GetNativeCommand() const { return m_command_buffer.get(); }

void* RM::Command::GetNativeAllocator() const { return m_command_allocator.get(); }

void RM::Command::GetComputeEncoder()
{
    m_compute_encoder = m_command_buffer->computeCommandEncoder();

    for (auto& barrier : m_pending_barriers)
    {
        m_compute_encoder->barrierAfterQueueStages(barrier.src_stage,
                                                   barrier.dst_stage,
                                                   MTL4::VisibilityOptionDevice);
    }
    m_pending_barriers.clear();
}

RM::CommandRing::CommandRing(Device* device, QueueType type)
{
    for (auto& command : m_commands)
    {
        command = new Command(device, type);
    }
}

RM::CommandRing::~CommandRing()
{
    for (const auto& command : m_commands)
    {
        delete command;
    }
}