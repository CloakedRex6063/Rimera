#include "rimera_command.hpp"
#include "rimera_buffer.hpp"
#include "rimera_device.hpp"
#include "rimera_helpers.hpp"
#include "rimera_metal.hpp"
#include "rimera_texture.hpp"
#include "rimera_builders.hpp"
#define IR_RUNTIME_METALCPP
#include "metal_irconverter_runtime/metal_irconverter_runtime.h"

RM::Result<void> RM::Command::Initialize(Device* device, QueueType)
{
    m_device = device;
    auto* native_device = static_cast<MTL::Device*>(device->GetNativeHandle());
    m_command_buffer = NS::TransferPtr(native_device->newCommandBuffer());
    m_command_allocator = NS::TransferPtr(native_device->newCommandAllocator());
    if (!m_command_buffer || !m_command_allocator)
        return std::unexpected("Failed to create command buffer or allocator");

    auto blit_buffer_result = device->CreateBuffer({.size = sizeof(BlitPassData), .data = nullptr});
    if (!blit_buffer_result) return std::unexpected(blit_buffer_result.error());
    m_blit_buffer = std::move(*blit_buffer_result);
    auto mapped = m_blit_buffer->Map();
    if (!mapped) return std::unexpected(mapped.error());
    m_blit_data = static_cast<BlitPassData*>(*mapped);

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
    auto sampler_result = device->CreateSampler(create_info);
    if (!sampler_result) return std::unexpected(sampler_result.error());
    m_sampler = std::move(*sampler_result);

    auto arg_table_desc = NS::TransferPtr(MTL4::ArgumentTableDescriptor::alloc()->init());
    arg_table_desc->setMaxBufferBindCount(3);
    NS::Error* arg_error = nullptr;
    m_arg_table = NS::TransferPtr(native_device->newArgumentTable(arg_table_desc.get(), &arg_error));
    if (!m_arg_table) return std::unexpected("Failed to create argument table");
    m_arg_table->setAddress(m_device->m_resource_descriptor_heap->gpuAddress(),
                            kIRDescriptorHeapBindPoint);
    m_arg_table->setAddress(m_device->m_sampler_descriptor_heap->gpuAddress(),
                            kIRSamplerHeapBindPoint);
    if (auto result = CreateArgumentBuffer(kInitialArgumentBufferSize); !result) return result;

    m_device->GetCoreResidencySet()->AddAllocation(m_blit_buffer);
    m_device->GetCoreResidencySet()->Commit();
    m_device->GetCoreResidencySet()->RequestResidency();
    return {};
}

RM::Command::~Command()
{
    m_command_buffer.reset();
    m_command_allocator.reset();
    m_device->DestroyImmediate(m_blit_buffer);
    m_device->DestroyImmediate(m_sampler);
    m_device->DestroyImmediate(m_arg_buffer);
}

void RM::Command::Begin()
{
    m_index_buffer = {};
    m_arguments.fill(0);
    m_root_constants.fill(0);
    m_root_constants_size = 0;
    m_arguments_dirty = true;
    m_root_constants_dirty = true;
    m_arg_offset = 0;
    m_command_allocator->reset();
    // Debug layer crashes with reusing with stale resources, creating a new one is cheap so just gonna use this
    auto* native_device = static_cast<MTL::Device*>(m_device->GetNativeHandle());
    m_command_buffer = NS::TransferPtr(native_device->newCommandBuffer());
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
        attachment->setTexture(static_cast<MTL::Texture*>(info_attach.texture->GetNativeHandle()));
        attachment->setClearColor(MTL::ClearColor(r, g, b, a));
        attachment->setLoadAction(static_cast<MTL::LoadAction>(info_attach.load_op));
        attachment->setStoreAction(static_cast<MTL::StoreAction>(info_attach.store_op));
    }

    if (info.depth_attachment.has_value())
    {
        auto info_attach = *info.depth_attachment;
        auto* attachment = rpd->depthAttachment();
        attachment->setTexture(static_cast<MTL::Texture*>(info_attach.texture->GetNativeHandle()));
        attachment->setLoadAction(static_cast<MTL::LoadAction>(info_attach.load_op));
        attachment->setStoreAction(static_cast<MTL::StoreAction>(info_attach.store_op));
    }

    m_render_encoder = m_command_buffer->renderCommandEncoder(rpd.get());
    m_stencil_reference = 0;
    
    for (auto& barrier : m_pending_barriers)
    {
        m_render_encoder->barrierAfterQueueStages(barrier.src_stage,
                                                   barrier.dst_stage,
                                                   MTL4::VisibilityOptionDevice);
    }
    m_pending_barriers.clear();
    
    m_render_encoder->setArgumentTable(m_arg_table.get(),
                                       MTL::RenderStageVertex | MTL::RenderStageFragment |
                                           MTL::RenderStageObject | MTL::RenderStageMesh);
}

void RM::Command::EndRenderPass()
{
    m_render_encoder->endEncoding();
    m_render_encoder = nullptr;
}

void RM::Command::BeginComputePass()
{
    if (m_compute_encoder) return;
    GetComputeEncoder();
}

void RM::Command::EndComputePass()
{
    if (!m_compute_encoder) return;
    m_compute_encoder->endEncoding();
    m_compute_encoder = nullptr;
}

RM::Result<void> RM::Command::Draw(uint32_t vertex_count,
                                   uint32_t instance_count,
                                   uint32_t start_vertex,
                                   uint32_t start_instance) const
{
#ifdef RIMERA_DEBUG
    if (!m_render_encoder)
        return std::unexpected("BeginRenderPass is required before drawing primitives");
#endif
    if (auto result = FlushArguments(); !result) return result;
    m_render_encoder->drawPrimitives(m_primitive_type,
                                     start_vertex,
                                     vertex_count,
                                     instance_count,
                                     start_instance);
    return {};
}

RM::Result<void> RM::Command::DrawIndexed(uint32_t index_count,
                                          uint32_t instance_count,
                                          uint32_t start_index,
                                          uint32_t start_vertex,
                                          uint32_t start_instance) const
{
#ifdef RIMERA_DEBUG
    if (!m_render_encoder)
        return std::unexpected("BeginRenderPass is required before drawing indexed primitives");
    if (!m_index_buffer.index_size)
        return std::unexpected("BindIndexBuffer is required before drawing indexed primitives");
#endif

    const uint64_t offset = uint64_t{start_index} * m_index_buffer.index_size;
#ifdef RIMERA_DEBUG
    const uint64_t size = uint64_t{index_count} * m_index_buffer.index_size;
    if (offset > m_index_buffer.length || size > m_index_buffer.length - offset)
        return std::unexpected("Indexed draw exceeds the bound index buffer");
#endif
    if (auto result = FlushArguments(); !result) return result;

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
    return {};
}

RM::Result<void> RM::Command::BindIndexBuffer(const Buffer* buffer, Format format)
{
#ifdef RIMERA_DEBUG
    if (!buffer) return std::unexpected("Cannot bind a null index buffer");
    if (format != Format::R16_UInt && format != Format::R32_UInt)
        return std::unexpected("Index buffers require R16_UInt or R32_UInt format");
#endif

    m_index_buffer = {
        .address = buffer->GetGPUAddress(),
        .length = buffer->GetLength(),
        .index_size = format == Format::R16_UInt ? 2u : 4u,
    };
    return {};
}

RM::Result<void> RM::Command::BindShader(const Pipeline* shader)
{
#ifdef RIMERA_DEBUG
    if (!shader) return std::unexpected("Cannot bind a null pipeline");
    if (shader->m_device != m_device)
        return std::unexpected("Pipeline belongs to a different device");
#endif

    switch (shader->GetShaderType())
    {
        case PipelineType::Compute:
#ifdef RIMERA_DEBUG
            if (!m_compute_encoder)
                return std::unexpected(
                    "BeginComputePass is required before binding a compute pipeline");
#endif
            m_compute_encoder->setComputePipelineState(shader->m_compute_pso.get());
            m_threads_per_compute = shader->GetComputeThreadsPerThreadgroup();
            break;
        case PipelineType::MeshGraphics:
            m_threads_per_ampl = shader->GetAmplThreadsPerThreadgroup();
            m_threads_per_mesh = shader->GetMeshThreadsPerThreadgroup();
            [[fallthrough]];
        case PipelineType::Graphics:
        {
#ifdef RIMERA_DEBUG
            if (!m_render_encoder)
                return std::unexpected(
                    "BeginRenderPass is required before binding a graphics pipeline");
#endif
            const auto& rasterizer = shader->m_rasterizer_state;
            m_render_encoder->setRenderPipelineState(shader->m_render_pso.get());
            m_render_encoder->setDepthStencilState(shader->m_depth_stencil_state.get());
            m_render_encoder->setCullMode(shader->m_cull_mode);
            m_render_encoder->setFrontFacingWinding(shader->m_winding);
            m_render_encoder->setTriangleFillMode(shader->m_fill_mode);
            m_render_encoder->setDepthBias(static_cast<float>(rasterizer.depth_bias),
                                           rasterizer.slope_scaled_depth_bias,
                                           rasterizer.depth_bias_clamp);
            m_render_encoder->setDepthClipMode(rasterizer.depth_clip_enable
                                                   ? MTL::DepthClipModeClip
                                                   : MTL::DepthClipModeClamp);
            m_render_encoder->setStencilReferenceValue(m_stencil_reference);
            m_render_encoder->setBlendColor(0, 0, 0, 0);
            break;
        }
    }
    return {};
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

            if (m_compute_encoder)
            {
                m_compute_encoder->barrierAfterEncoderStages(src,
                                                             dst,
                                                             MTL4::VisibilityOptionDevice);
                continue;
            }

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

RM::Result<void> RM::Command::CopyTexture(const Texture* source, const Texture* dest)
{
    const bool transient = !m_compute_encoder;
    if (transient) GetComputeEncoder();
    const auto* src = static_cast<MTL::Texture*>(source->GetNativeHandle());
    const auto* dst = static_cast<MTL::Texture*>(dest->GetNativeHandle());

    m_compute_encoder->copyFromTexture(src, dst);

    EndTransientComputeEncoder(transient);
    return {};
}

RM::Result<void> RM::Command::CopyBufferToTexture(const Buffer* source,
                                                  const Texture* dest,
                                                  const BufferTextureCopy& region)
{
#ifdef RIMERA_DEBUG
    if (!source || !dest) return std::unexpected("Cannot copy between null resources");
    if (region.mip_level >= dest->GetMipLevels() || region.array_layer >= dest->GetArrayLayers())
        return std::unexpected("Buffer to texture copy subresource is out of bounds");
#endif
    const auto footprint = m_device->GetCopyFootprint(dest, region.mip_level);
    if (!footprint.size) return std::unexpected("Texture format does not support buffer copies");
    const auto size = dest->GetDimensions();
    const uint32_t width = std::max(1u, size.x >> region.mip_level);
    const uint32_t height = std::max(1u, size.y >> region.mip_level);
    const bool transient = !m_compute_encoder;
    if (transient) GetComputeEncoder();
    m_compute_encoder->copyFromBuffer(static_cast<MTL::Buffer*>(source->GetNativeHandle()),
                                      region.buffer_offset,
                                      footprint.bytes_per_row,
                                      footprint.size,
                                      MTL::Size(width, height, 1),
                                      static_cast<MTL::Texture*>(dest->GetNativeHandle()),
                                      region.array_layer,
                                      region.mip_level,
                                      MTL::Origin(0, 0, 0));
    EndTransientComputeEncoder(transient);
    return {};
}

RM::Result<void> RM::Command::CopyBuffer(const Buffer* source,
                                         const Buffer* dest,
                                         uint64_t source_offset,
                                         uint64_t dest_offset,
                                         uint64_t size)
{
#ifdef RIMERA_DEBUG
    if (!source || !dest) return std::unexpected("Cannot copy a null buffer");
#endif
    if (size == UINT64_MAX)
    {
        if (source_offset > source->GetLength() || dest_offset > dest->GetLength())
            return std::unexpected("Buffer copy offset is out of bounds");
        size = std::min(source->GetLength() - source_offset, dest->GetLength() - dest_offset);
    }
#ifdef RIMERA_DEBUG
    if (source_offset > source->GetLength() || size > source->GetLength() - source_offset ||
        dest_offset > dest->GetLength() || size > dest->GetLength() - dest_offset)
        return std::unexpected("Buffer copy range is out of bounds");
#endif
    const bool transient = !m_compute_encoder;
    if (transient) GetComputeEncoder();
    m_compute_encoder->copyFromBuffer(static_cast<MTL::Buffer*>(source->GetNativeHandle()),
                                      source_offset,
                                      static_cast<MTL::Buffer*>(dest->GetNativeHandle()),
                                      dest_offset,
                                      size);
    EndTransientComputeEncoder(transient);
    return {};
}

RM::Result<void> RM::Command::Blit(const Texture* source,
                                   const Texture* dest,
                                   const BlitRegion& region)
{
    auto src_dim = source->GetDimensions();
    auto dst_dim = dest->GetDimensions();

    if (!m_device->m_blit_pipelines.contains(dest->GetFormat()))
    {
        auto pipeline = Helpers::CreateBlitShader(m_device, dest->GetFormat());
#ifdef RIMERA_DEBUG
        if (!pipeline) return std::unexpected(pipeline.error());
#endif
        m_device->m_blit_pipelines[dest->GetFormat()] = *pipeline;
    }

    uint32_t src_w = std::max(1u, src_dim.x >> region.src_mip);
    uint32_t src_h = std::max(1u, src_dim.y >> region.src_mip);

    Rect src = Helpers::IsEmpty(region.src_rect)
                   ? Rect{.offset = {}, .size = {.x = src_w, .y = src_h}}
                   : region.src_rect;
    Rect dst = Helpers::IsEmpty(region.dst_rect)
                   ? Rect{.offset = {}, .size = {.x = dst_dim.x, .y = dst_dim.y}}
                   : region.dst_rect;

    if (src.size.x == 0 || src.size.y == 0 || dst.size.x == 0 || dst.size.y == 0) return {};

    auto* pass_data = m_blit_data;
    *pass_data = {
        .src_index = source->GetResourceIndex(),
        .src_mip = region.src_mip,
        .sampler_index = m_sampler->GetSamplerIndex(),
        .uv_rect = {
            .x = static_cast<float>(src.offset.x) / static_cast<float>(src_w),
            .y = static_cast<float>(src.offset.y) / static_cast<float>(src_h),
            .z = static_cast<float>(src.size.x) / static_cast<float>(src_w),
            .w = static_cast<float>(src.size.y) / static_cast<float>(src_h),
        }
    };

    BeginRenderPass(RenderPassBuilder(dst_dim).Color(dest).Build());

#ifdef RIMERA_DEBUG
    if (auto result = BindShader(m_device->m_blit_pipelines[dest->GetFormat()]); !result)
        return std::unexpected(result.error());
#else
    static_cast<void>(BindShader(m_device->m_blit_pipelines[dest->GetFormat()]));
#endif

#ifdef RIMERA_DEBUG
    if (auto result = BindConstantBuffer(1, m_blit_buffer); !result)
        return std::unexpected(result.error());
#else
    static_cast<void>(BindConstantBuffer(1, m_blit_buffer));
#endif

    SetViewport(Viewport{
        .offset = {.x = dst.offset.x, .y = dst.offset.y},
        .size = {.x = dst.size.x, .y = dst.size.y},
        .depth_range = {.x = 0.0f, .y = 1.0f},
    });
    SetScissor(dst);
    SetPrimitiveTopology(PrimitiveTopology::TriangleList);

#ifdef RIMERA_DEBUG
    if (auto result = Draw(3, 1, 0, 0); !result) return std::unexpected(result.error());
#else
    Draw(3, 1, 0, 0);
#endif
    EndRenderPass();
    return {};
}

RM::Result<void> RM::Command::DispatchMesh(DispatchGroup dispatch_group) const
{
#ifdef RIMERA_DEBUG
    if (!m_render_encoder)
        return std::unexpected("BeginRenderPass is required before dispatching mesh threadgroups");
#endif
    if (auto result = FlushArguments(); !result) return result;
    m_render_encoder->drawMeshThreadgroups(
        MTL::Size(dispatch_group.x, dispatch_group.y, dispatch_group.z),
        MTL::Size(m_threads_per_ampl.x, m_threads_per_ampl.y, m_threads_per_ampl.z),
        MTL::Size(m_threads_per_mesh.x, m_threads_per_mesh.y, m_threads_per_mesh.z));
    return {};
}

RM::Result<void> RM::Command::Dispatch(DispatchGroup dispatch_group)
{
    if (auto result = FlushArguments(); !result) return result;
    const bool transient = !m_compute_encoder;
    if (transient) GetComputeEncoder();
    m_compute_encoder->dispatchThreadgroups(
        MTL::Size(dispatch_group.x, dispatch_group.y, dispatch_group.z),
        MTL::Size(m_threads_per_compute.x, m_threads_per_compute.y, m_threads_per_compute.z));
    EndTransientComputeEncoder(transient);
    return {};
}

RM::Result<void> RM::Command::BindConstantBuffer(const uint32_t slot, const Buffer* buffer) const
{
#ifdef RIMERA_DEBUG
    if (!buffer || slot >= kConstantBufferSlotCount)
        return std::unexpected("Invalid constant buffer binding");
#endif
    m_arguments[slot] = buffer->GetGPUAddress();
    m_arguments_dirty = true;
    return {};
}

RM::Result<void> RM::Command::BindRootConstants(const void* data,
                                                uint32_t size,
                                                uint32_t offset) const
{
#ifdef RIMERA_DEBUG
    if (!data || !size || size % 4 || offset % 4 || offset > kRootConstantCount * 4 ||
        size > kRootConstantCount * 4 - offset)
        return std::unexpected("Invalid root constants binding");
#endif
    std::memcpy(m_root_constants.data() + offset, data, size);
    m_root_constants_size = std::max(m_root_constants_size, offset + size);
    m_root_constants_dirty = true;
    return {};
}

RM::Result<void> RM::Command::CreateArgumentBuffer(uint32_t size) const
{
    auto buffer = m_device->CreateBuffer({.size = size, .data = nullptr});
    if (!buffer) return std::unexpected(buffer.error());
    auto mapped = (*buffer)->Map();
    if (!mapped)
    {
        m_device->DestroyImmediate(*buffer);
        return std::unexpected(mapped.error());
    }
    m_device->Retire(m_arg_buffer);
    m_arg_buffer = *buffer;
    m_arg_data = static_cast<uint8_t*>(*mapped);
    m_arg_offset = 0;
    m_device->GetCoreResidencySet()->AddAllocation(m_arg_buffer);
    m_device->GetCoreResidencySet()->Commit();
    return {};
}

RM::Result<uint32_t> RM::Command::AllocateArguments(uint32_t size) const
{
    uint32_t offset = (m_arg_offset + kArgumentAlignment - 1) & ~(kArgumentAlignment - 1);
    if (offset + size > m_arg_buffer->GetLength())
    {
        if (auto result = CreateArgumentBuffer(std::max(m_arg_buffer->GetLength() * 2, size));
            !result)
            return std::unexpected(result.error());
        offset = 0;
    }
    m_arg_offset = offset + size;
    return offset;
}

RM::Result<void> RM::Command::FlushArguments() const
{
    if (m_root_constants_dirty && m_root_constants_size)
    {
        auto offset = AllocateArguments(m_root_constants_size);
        if (!offset) return std::unexpected(offset.error());
        std::memcpy(m_arg_data + *offset, m_root_constants.data(), m_root_constants_size);
        m_arguments[kRootConstantSlot] = m_arg_buffer->GetGPUAddress() + *offset;
        m_arguments_dirty = true;
    }
    m_root_constants_dirty = false;
    if (!m_arguments_dirty) return {};
    auto offset = AllocateArguments(sizeof(m_arguments));
    if (!offset) return std::unexpected(offset.error());
    std::memcpy(m_arg_data + *offset, m_arguments.data(), sizeof(m_arguments));
    m_arg_table->setAddress(m_arg_buffer->GetGPUAddress() + *offset, kIRArgumentBufferBindPoint);
    m_arguments_dirty = false;
    return {};
}

void* RM::Command::GetNativeCommand() const { return m_command_buffer.get(); }

void* RM::Command::GetNativeAllocator() const { return m_command_allocator.get(); }

void RM::Command::GetComputeEncoder()
{
    m_compute_encoder = m_command_buffer->computeCommandEncoder();
    m_compute_encoder->setArgumentTable(m_arg_table.get());

    for (auto& barrier : m_pending_barriers)
    {
        m_compute_encoder->barrierAfterQueueStages(barrier.src_stage,
                                                   barrier.dst_stage,
                                                   MTL4::VisibilityOptionDevice);
    }
    m_pending_barriers.clear();
}

void RM::Command::EndTransientComputeEncoder(bool transient)
{
    if (!transient) return;
    m_compute_encoder->endEncoding();
    m_compute_encoder = nullptr;
}

RM::Result<void> RM::CommandRing::Initialize(Device* device, QueueType type)
{
    for (auto& command : m_commands)
    {
        auto result = device->CreateCommand(type);
        if (!result) return std::unexpected(result.error());
        command = *result;
    }
    return {};
}

RM::CommandRing::~CommandRing()
{
    for (const auto& command : m_commands)
    {
        delete command;
    }
}

void RM::Command::SetStencilReference(uint32_t reference)
{
    m_stencil_reference = reference;
    if (m_render_encoder) m_render_encoder->setStencilReferenceValue(reference);
}
