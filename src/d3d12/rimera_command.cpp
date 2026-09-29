#include "rimera_command.hpp"
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
}

void RM::Command::Begin() const
{
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

void RM::Command::BeginRenderPass(const RenderPassCreateInfo& info) const
{
    auto heap = m_device->m_rtv_heap;
    std::array<D3D12_RENDER_PASS_RENDER_TARGET_DESC, 8> render_target_descriptors;
    for (int i = 0; i < info.color_attachments.size(); i++)
    {
        auto [render_target, clear_color, store_op, load_op] = info.color_attachments[i];
        render_target_descriptors[i] = D3D12_RENDER_PASS_RENDER_TARGET_DESC{
            .cpuDescriptor = heap.GetCPUHandle(render_target->GetResourceIndex()),
            .BeginningAccess = D3D12::ToBeginAccess(render_target->GetFormat(),
                                                    load_op,
                                                    clear_color),
            .EndingAccess = D3D12::ToEndAccess(store_op),
        };
    }
    const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC* depth_desc = nullptr;
    if (info.depth_attachment.has_value())
    {
        auto [depth_stencil, clear_depth, clear_stencil, load_op, store_op] = info.depth_attachment.
            value();
        const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC depth_stencil_desc{
            .cpuDescriptor = heap.GetCPUHandle(depth_stencil->GetResourceIndex()),
            .DepthBeginningAccess =
            D3D12::ToBeginAccess(depth_stencil->GetFormat(),
                                 load_op,
                                 clear_depth,
                                 clear_stencil),
            .StencilBeginningAccess =
            {D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS},
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
}

void RM::Command::EndComputePass()
{
}

void RM::Command::Draw(const uint32_t vertex_count,
                       const uint32_t instance_count,
                       const uint32_t start_vertex,
                       const uint32_t start_instance) const
{
    m_command_buffer->DrawInstanced(vertex_count, instance_count, start_vertex, start_instance);
}

void RM::Command::DrawIndexed(const uint32_t index_count,
                              const uint32_t instance_count,
                              const uint32_t start_index,
                              const uint32_t start_vertex,
                              const uint32_t start_instance) const
{
    m_command_buffer->DrawIndexedInstanced(index_count,
                                           instance_count,
                                           start_index,
                                           start_vertex,
                                           start_instance);
}

void RM::Command::DispatchMesh(const DispatchGroup dispatch_group) const
{
    m_command_buffer->DispatchMesh(dispatch_group.x, dispatch_group.y, dispatch_group.z);
}

void RM::Command::Dispatch(const DispatchGroup dispatch_group) const
{
    m_command_buffer->Dispatch(dispatch_group.x, dispatch_group.y, dispatch_group.z);
}

void RM::Command::BindShader(const Pipeline* shader) const
{
    auto* pipeline = static_cast<ID3D12PipelineState*>(shader->GetNativeHandle());
    m_command_buffer->SetPipelineState(pipeline);
}

void RM::Command::BindConstantBuffer(uint32_t slot, const Buffer* buffer) const
{
    m_command_buffer->SetGraphicsRootConstantBufferView(slot, buffer->GetGPUAddress());
}

void RM::Command::Barrier(const std::span<const BarrierGroup>& groups) const
{
    size_t global_count = 0;
    size_t texture_count = 0;
    size_t buffer_count = 0;

    for (const auto& group : groups)
    {
        global_count += group.global_barriers.size();
        texture_count += group.texture_barriers.size();
        buffer_count += group.buffer_barriers.size();
    }

    std::vector<D3D12_GLOBAL_BARRIER> globals;
    std::vector<D3D12_TEXTURE_BARRIER> textures;
    std::vector<D3D12_BUFFER_BARRIER> buffers;
    std::vector<D3D12_BARRIER_GROUP> native_groups;

    globals.reserve(global_count);
    textures.reserve(texture_count);
    buffers.reserve(buffer_count);
    native_groups.reserve(groups.size() * 3);

    for (const auto& group : groups)
    {
        if (!group.global_barriers.empty())
        {
            const size_t start = globals.size();

            for (const auto& barrier : group.global_barriers)
            {
                globals.push_back(D3D12::ConvertBarrier(barrier));
            }

            D3D12_BARRIER_GROUP native{};
            native.Type = D3D12_BARRIER_TYPE_GLOBAL;
            native.NumBarriers = group.global_barriers.size();
            native.pGlobalBarriers = globals.data() + start;
            native_groups.push_back(native);
        }

        if (!group.texture_barriers.empty())
        {
            const size_t start = textures.size();

            for (const auto& barrier : group.texture_barriers)
                textures.push_back(D3D12::ConvertBarrier(barrier));

            D3D12_BARRIER_GROUP native{};
            native.Type = D3D12_BARRIER_TYPE_TEXTURE;
            native.NumBarriers = group.texture_barriers.size();
            native.pTextureBarriers = textures.data() + start;
            native_groups.push_back(native);
        }

        if (!group.buffer_barriers.empty())
        {
            const size_t start = buffers.size();

            for (const auto& barrier : group.buffer_barriers)
                buffers.push_back(D3D12::ConvertBarrier(barrier));

            D3D12_BARRIER_GROUP native{};
            native.Type = D3D12_BARRIER_TYPE_BUFFER;
            native.NumBarriers = group.buffer_barriers.size();
            native.pBufferBarriers = buffers.data() + start;
            native_groups.push_back(native);
        }
    }

    if (!native_groups.empty())
    {
        m_command_buffer->Barrier(
            native_groups.size(),
            native_groups.data());
    }
}

void RM::Command::Barrier(const BarrierGroup& group) const
{
    Barrier(std::span{&group, 1});
}

void RM::Command::Barrier(const std::span<const TextureBarrier> barriers) const
{
    BarrierGroup group;
    group.texture_barriers = barriers;
    Barrier(std::span{&group, 1});
}

void RM::Command::Barrier(const TextureBarrier& barrier) const
{
    Barrier(std::span{&barrier, 1});
}

void RM::Command::Barrier(const std::span<const BufferBarrier> barriers) const
{
    BarrierGroup group;
    group.buffer_barriers = barriers;
    Barrier(std::span{&group, 1});
}

void RM::Command::Barrier(const BufferBarrier& barrier) const
{
    Barrier(std::span{&barrier, 1});
}

void RM::Command::Barrier(const std::span<const GlobalBarrier> barriers) const
{
    BarrierGroup group;
    group.global_barriers = barriers;
    Barrier(std::span{&group, 1});
}

void RM::Command::Barrier(const GlobalBarrier& barrier) const
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

void RM::Command::SetPrimitiveTopology(const PrimitiveTopology topology) const
{
    m_command_buffer->IASetPrimitiveTopology(D3D12::GetPrimitiveTopology(topology));
}

void RM::Command::CopyTexture(const Texture* source, const Texture* dest) const
{
    auto* src_res = static_cast<ID3D12Resource*>(source->GetNativeHandle());
    auto* dest_res = static_cast<ID3D12Resource*>(dest->GetNativeHandle());
    m_command_buffer->CopyResource(dest_res, src_res);
}

static bool IsEmpty(const RM::Rect& r)
{
    return r.offset.x == 0 && r.offset.y == 0 && r.size.x == 0 && r.size.y == 0;
}

void RM::Command::Blit(const TextureView* source, const TextureView* dest, const BlitRegion& region)
{
    auto src_dim = source->GetTexture()->GetDimensions();
    auto dst_dim = dest->GetTexture()->GetDimensions();

    uint32_t src_w = std::max(1u, src_dim.x >> region.src_mip);
    uint32_t src_h = std::max(1u, src_dim.y >> region.src_mip);

    Rect src = IsEmpty(region.src_rect)
                   ? Rect{.offset = {}, .size = {.x = src_w, .y = src_h}}
                   : region.src_rect;
    Rect dst = IsEmpty(region.dst_rect)
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

    BindShader(m_blit_shader);
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

void* RM::Command::GetNativeCommand() const
{
    return m_command_buffer.Get();
}

void* RM::Command::GetNativeAllocator() const
{
    return m_command_allocator.Get();
}

RM::Command::Command(Device* device, QueueType queue_type) : m_device(device)
{
    auto* native_device = static_cast<ID3D12Device15*>(device->GetNativeHandle());
    native_device->CreateCommandAllocator(D3D12::GetQueueType(queue_type),
                                          IID_PPV_ARGS(m_command_allocator.GetAddressOf()));
    native_device->CreateCommandList(0,
                                     D3D12::GetQueueType(queue_type),
                                     m_command_allocator.Get(),
                                     nullptr,
                                     IID_PPV_ARGS(m_command_buffer.GetAddressOf()));
    m_command_buffer->Close();

    m_blit_shader = Helpers::CreateBlitShader(device);
    m_blit_buffer =
        device->CreateBuffer({.size = sizeof(BlitPassData), .data = nullptr,
                              .flags = BufferFlags::eUnorderedAccess,});

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
}

RM::CommandRing::CommandRing(Device* device, QueueType queue_type)
{
    for (auto& command : m_commands)
    {
        command = new Command(device, queue_type);
    }
}

RM::CommandRing::~CommandRing()
{
    for (const auto& command : m_commands)
    {
        delete command;
    }
}