#include "rimera_command.hpp"
#include "rimera_buffer.hpp"
#include "rimera_d3d12.hpp"
#include "rimera_device.hpp"
#include "assert.h"
#include "rimera_builders.hpp"
#include "rimera_helpers.hpp"

RM::Command::~Command()
{
    m_command_allocator.Reset();
    m_command_buffer.Reset();
    m_device->DestroyBuffer(m_blit_buffer);
    m_device->DestroyPipeline(m_blit_shader);
    m_device->DestroySampler(m_sampler);
}

void RM::Command::Begin()
{
#ifdef RIMERA_DEBUG
    m_index_buffer = {};
#endif
    m_in_compute_pass = false;
    m_command_allocator->Reset();

    m_command_buffer->Reset(m_command_allocator.Get(), nullptr);

    if (m_command_buffer->GetType() != D3D12_COMMAND_LIST_TYPE_COPY)
    {
        const auto descriptor_heaps = std::array{m_device->m_srv_heap.heap.Get(),
                                                 m_device->m_sampler_heap.heap.Get()};
        m_command_buffer->SetDescriptorHeaps(2, descriptor_heaps.data());
        m_command_buffer->SetGraphicsRootSignature(m_device->m_root_sig.Get());
        m_command_buffer->SetComputeRootSignature(m_device->m_root_sig.Get());
    }
}

void RM::Command::End() const
{
    m_command_buffer->Close();
}

void RM::Command::BeginRenderPass(const RenderPassCreateInfo& info) 
{
    auto heap = m_device->m_rtv_heap;
    std::array<D3D12_RENDER_PASS_RENDER_TARGET_DESC, 8> render_target_descriptors;
    for (int i = 0; i < info.color_attachments.size(); i++)
    {
        auto [render_target, clear_color, store_op, load_op] = info.color_attachments[i];
        render_target_descriptors[i] = D3D12_RENDER_PASS_RENDER_TARGET_DESC{
            .cpuDescriptor =
                heap.GetCPUHandle(render_target->GetResourceIndex(TextureViewType::RenderTarget)),
            .BeginningAccess =
                D3D12::ToBeginAccess(render_target->GetFormat(), load_op, clear_color),
            .EndingAccess = D3D12::ToEndAccess(store_op),
        };
    }
    const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC* depth_desc = nullptr;
    D3D12_RENDER_PASS_DEPTH_STENCIL_DESC depth_stencil_desc{};
    if (info.depth_attachment.has_value())
    {
        auto [depth_stencil, clear_depth, clear_stencil, load_op, store_op] = info.depth_attachment.
            value();
        depth_stencil_desc = {
            .cpuDescriptor = m_device->m_dsv_heap.GetCPUHandle(
                depth_stencil->GetResourceIndex(TextureViewType::DepthStencil)),
            .DepthBeginningAccess = D3D12::ToBeginAccess(depth_stencil->GetFormat(),
                                                         load_op,
                                                         clear_depth,
                                                         clear_stencil),
            .StencilBeginningAccess = {D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS},
            // TODO: handle stencil based on format
            .DepthEndingAccess = D3D12::ToEndAccess(store_op),
            .StencilEndingAccess = {.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS},
            // TODO: handle stencil based on format
        };
        depth_desc = &depth_stencil_desc;
    }
    m_command_buffer->BeginRenderPass(info.color_attachments.size(),
                                      render_target_descriptors.data(),
                                      depth_desc,
                                      D3D12_RENDER_PASS_FLAG_NONE);
}

void RM::Command::EndRenderPass()
{
    m_command_buffer->EndRenderPass();
}

void RM::Command::BeginComputePass()
{
    m_in_compute_pass = true;
}

void RM::Command::EndComputePass()
{
    m_in_compute_pass = false;
}

RM::Result<void> RM::Command::Draw(const uint32_t vertex_count,
                                   const uint32_t instance_count,
                                   const uint32_t start_vertex,
                                   const uint32_t start_instance) const
{
    m_command_buffer->DrawInstanced(vertex_count, instance_count, start_vertex, start_instance);
    return {};
}

RM::Result<void> RM::Command::DrawIndexed(const uint32_t index_count,
                                          const uint32_t instance_count,
                                          const uint32_t start_index,
                                          const uint32_t start_vertex,
                                          const uint32_t start_instance) const
{
#ifdef RIMERA_DEBUG
    if (!m_index_buffer.index_size)
        return std::unexpected("BindIndexBuffer is required before drawing indexed primitives");

    const uint64_t offset = uint64_t{start_index} * m_index_buffer.index_size;
    const uint64_t size = uint64_t{index_count} * m_index_buffer.index_size;
    if (offset > m_index_buffer.length || size > m_index_buffer.length - offset)
        return std::unexpected("Indexed draw exceeds the bound index buffer");
#endif

    m_command_buffer->DrawIndexedInstanced(index_count,
                                           instance_count,
                                           start_index,
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

    const D3D12_INDEX_BUFFER_VIEW view{
        .BufferLocation = buffer->GetGPUAddress(),
        .SizeInBytes = buffer->GetLength(),
        .Format = format == Format::R16_UInt ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT,
    };
    m_command_buffer->IASetIndexBuffer(&view);
#ifdef RIMERA_DEBUG
    m_index_buffer = {
        .address = view.BufferLocation,
        .length = view.SizeInBytes,
        .index_size = format == Format::R16_UInt ? 2u : 4u,
    };
#endif
    return {};
}

RM::Result<void> RM::Command::DispatchMesh(const DispatchGroup dispatch_group) const
{
    m_command_buffer->DispatchMesh(dispatch_group.x, dispatch_group.y, dispatch_group.z);
    return {};
}

RM::Result<void> RM::Command::Dispatch(const DispatchGroup dispatch_group)
{
    m_command_buffer->Dispatch(dispatch_group.x, dispatch_group.y, dispatch_group.z);
    return {};
}

RM::Result<void> RM::Command::BindShader(const Pipeline* shader)
{
#ifdef RIMERA_DEBUG
    if (!shader) return std::unexpected("Cannot bind a null pipeline");
#endif
    auto* pipeline = static_cast<ID3D12PipelineState*>(shader->GetNativeHandle());
    m_command_buffer->SetPipelineState(pipeline);
    return {};
}

RM::Result<void> RM::Command::BindConstantBuffer(uint32_t slot, const Buffer* buffer) const
{
#ifdef RIMERA_DEBUG
    if (!buffer || slot >= kConstantBufferSlotCount)
        return std::unexpected("Invalid constant buffer binding");
#endif
    if (m_in_compute_pass)
        m_command_buffer->SetComputeRootConstantBufferView(slot, buffer->GetGPUAddress());
    else
        m_command_buffer->SetGraphicsRootConstantBufferView(slot, buffer->GetGPUAddress());
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
    if (m_in_compute_pass)
        m_command_buffer->SetComputeRoot32BitConstants(kRootConstantSlot,
                                                       size / 4,
                                                       data,
                                                       offset / 4);
    else
        m_command_buffer->SetGraphicsRoot32BitConstants(kRootConstantSlot,
                                                        size / 4,
                                                        data,
                                                        offset / 4);
    return {};
}

void RM::Command::Barrier(const BarrierGroup& group)
{
    std::vector<D3D12_GLOBAL_BARRIER> globals;
    std::vector<D3D12_TEXTURE_BARRIER> textures;
    std::vector<D3D12_BUFFER_BARRIER> buffers;
    std::vector<D3D12_BARRIER_GROUP> native_groups;

    globals.reserve(group.global_barriers.size());
    textures.reserve(group.texture_barriers.size());
    buffers.reserve(group.buffer_barriers.size());
    native_groups.reserve(3);

    if (!group.global_barriers.empty())
    {
        for (const auto& barrier : group.global_barriers)
            globals.push_back(D3D12::ConvertBarrier(barrier));

        D3D12_BARRIER_GROUP native{};
        native.Type = D3D12_BARRIER_TYPE_GLOBAL;
        native.NumBarriers = static_cast<UINT>(globals.size());
        native.pGlobalBarriers = globals.data();
        native_groups.push_back(native);
    }

    if (!group.texture_barriers.empty())
    {
        for (const auto& barrier : group.texture_barriers)
            textures.push_back(D3D12::ConvertBarrier(barrier));

        D3D12_BARRIER_GROUP native{};
        native.Type = D3D12_BARRIER_TYPE_TEXTURE;
        native.NumBarriers = static_cast<UINT>(textures.size());
        native.pTextureBarriers = textures.data();
        native_groups.push_back(native);
    }

    if (!group.buffer_barriers.empty())
    {
        for (const auto& barrier : group.buffer_barriers)
            buffers.push_back(D3D12::ConvertBarrier(barrier));

        D3D12_BARRIER_GROUP native{};
        native.Type = D3D12_BARRIER_TYPE_BUFFER;
        native.NumBarriers = static_cast<UINT>(buffers.size());
        native.pBufferBarriers = buffers.data();
        native_groups.push_back(native);
    }

    if (!native_groups.empty())
    {
        m_command_buffer->Barrier(
            static_cast<UINT>(native_groups.size()),
            native_groups.data());
    }
}

void RM::Command::Barrier(const std::span<const TextureBarrier> barriers)
{
    BarrierGroup group;
    group.texture_barriers = barriers;
    Barrier(group);
}

void RM::Command::Barrier(const TextureBarrier& barrier)
{
    Barrier(std::span{&barrier, 1});
}

void RM::Command::Barrier(const std::span<const BufferBarrier> barriers)
{
    BarrierGroup group;
    group.buffer_barriers = barriers;
    Barrier(group);
}

void RM::Command::Barrier(const BufferBarrier& barrier)
{
    Barrier(std::span{&barrier, 1});
}

void RM::Command::Barrier(const std::span<const GlobalBarrier> barriers)
{
    BarrierGroup group;
    group.global_barriers = barriers;
    Barrier(group);
}

void RM::Command::Barrier(const GlobalBarrier& barrier)
{
    Barrier(std::span{&barrier, 1});
}

void RM::Command::SetViewport(const Viewport& viewport) const
{
    D3D12_VIEWPORT dx_viewport
    {
        .TopLeftX = static_cast<float>(viewport.offset.x),
        .TopLeftY = static_cast<float>(viewport.offset.y),
        .Width = static_cast<float>(viewport.size.x),
        .Height = static_cast<float>(viewport.size.y),
        .MinDepth = viewport.depth_range.x,
        .MaxDepth = viewport.depth_range.y,
    };
    m_command_buffer->RSSetViewports(1, &dx_viewport);
}

void RM::Command::SetScissor(const Rect& rect) const
{
    D3D12_RECT scissor
    {
        .left = static_cast<int>(rect.offset.x),
        .top = static_cast<int>(rect.offset.y),
        .right = static_cast<int>(rect.size.x),
        .bottom = static_cast<int>(rect.size.y),
    };
    m_command_buffer->RSSetScissorRects(1, &scissor);
}

void RM::Command::SetPrimitiveTopology(const PrimitiveTopology topology)
{
    m_command_buffer->IASetPrimitiveTopology(D3D12::GetPrimitiveTopology(topology));
}

RM::Result<void> RM::Command::CopyTexture(const Texture* source, const Texture* dest)
{
    auto* src_res = static_cast<ID3D12Resource*>(source->GetNativeHandle());
    auto* dest_res = static_cast<ID3D12Resource*>(dest->GetNativeHandle());
#ifdef RIMERA_DEBUG
    if (source->GetDimensions().x != dest->GetDimensions().x ||
        source->GetDimensions().y != dest->GetDimensions().y ||
        source->GetMipLevels() != dest->GetMipLevels() ||
        source->GetArrayLayers() != dest->GetArrayLayers())
        return std::unexpected("Texture copy ranges must match");
#endif
    for (uint32_t layer = 0; layer < source->GetArrayLayers(); ++layer)
    {
        for (uint32_t mip = 0; mip < source->GetMipLevels(); ++mip)
        {
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = src_res;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = source->GetBaseMip() + mip +
                                   (source->GetBaseLayer() + layer) * src_res->GetDesc().MipLevels;
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = dest_res;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = dest->GetBaseMip() + mip +
                                   (dest->GetBaseLayer() + layer) * dest_res->GetDesc().MipLevels;
            m_command_buffer->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }
    }
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
    m_command_buffer->CopyBufferRegion(static_cast<ID3D12Resource*>(dest->GetNativeHandle()),
                                       dest_offset,
                                       static_cast<ID3D12Resource*>(source->GetNativeHandle()),
                                       source_offset,
                                       size);
    return {};
}

RM::Result<void> RM::Command::Blit(const Texture* source,
                                   const Texture* dest,
                                   const BlitRegion& region)
{
    auto src_dim = source->GetDimensions();
    auto dst_dim = dest->GetDimensions();

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
    if (auto result = BindShader(m_blit_shader); !result) return std::unexpected(result.error());
#else
    static_cast<void>(BindShader(m_blit_shader));
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

    if (auto result = Draw(3, 1, 0, 0); !result) return std::unexpected(result.error());
    EndRenderPass();
    return {};
}

void* RM::Command::GetNativeCommand() const
{
    return m_command_buffer.Get();
}

void* RM::Command::GetNativeAllocator() const
{
    return m_command_allocator.Get();
}

RM::Result<void> RM::Command::Initialize(Device* device, QueueType queue_type)
{
    m_device = device;
    auto* native_device = static_cast<ID3D12Device15*>(device->GetNativeHandle());
    if (FAILED(native_device->CreateCommandAllocator(
            D3D12::GetQueueType(queue_type),
            IID_PPV_ARGS(m_command_allocator.GetAddressOf()))))
        return std::unexpected("Failed to create command allocator");
    if (FAILED(native_device->CreateCommandList(0,
                                                D3D12::GetQueueType(queue_type),
                                                m_command_allocator.Get(),
                                                nullptr,
                                                IID_PPV_ARGS(m_command_buffer.GetAddressOf()))))
        return std::unexpected("Failed to create command list");
    if (FAILED(m_command_buffer->Close())) return std::unexpected("Failed to close command list");

    auto blit_shader_result = Helpers::CreateBlitShader(device, Format::RGBA8_UNorm);
    if (!blit_shader_result) return std::unexpected(blit_shader_result.error());
    m_blit_shader = std::move(*blit_shader_result);
    auto blit_buffer_result = device->CreateBuffer({
        .size = sizeof(BlitPassData),
        .data = nullptr,
        .flags = BufferFlags::eUnorderedAccess,
    });
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
    return {};
}

RM::Result<void> RM::CommandRing::Initialize(Device* device, QueueType queue_type)
{
    for (auto& command : m_commands)
    {
        auto result = device->CreateCommand(queue_type);
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
