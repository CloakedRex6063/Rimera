#include "rimera_command.hpp"
#include "rimera_buffer.hpp"
#include "rimera_builders.hpp"
#include "rimera_device.hpp"
#include "rimera_helpers.hpp"
#include "rimera_sampler.hpp"
#include "rimera_vulkan.hpp"

RM::Command::~Command()
{
    if (m_device)
    {
        m_device->DestroyImmediate(m_blit_buffer);
        m_device->DestroyImmediate(m_sampler);
        if (m_command_pool)
            vkDestroyCommandPool(static_cast<VkDevice>(m_device->GetNativeHandle()),
                                 m_command_pool,
                                 nullptr);
    }
}

void RM::Command::Begin()
{
#ifdef RIMERA_DEBUG
    m_index_buffer = {};
#endif
    const auto device = static_cast<VkDevice>(m_device->GetNativeHandle());
    vkResetCommandPool(device, m_command_pool, 0);
    constexpr VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(m_command_buffer, &begin_info);
    if (m_queue_type == QueueType::Transfer) return;

    const auto& resource_heap = m_device->m_resource_heap;
    const VkBindHeapInfoEXT resource_info{
        .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
        .heapRange = {resource_heap.address, resource_heap.size},
        .reservedRangeOffset = resource_heap.reserved_offset,
        .reservedRangeSize = resource_heap.reserved_size,
    };
    vkCmdBindResourceHeapEXT(m_command_buffer, &resource_info);
    const auto& sampler_heap = m_device->m_sampler_heap;
    const VkBindHeapInfoEXT sampler_info{
        .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
        .heapRange = {sampler_heap.address, sampler_heap.size},
        .reservedRangeOffset = sampler_heap.reserved_offset,
        .reservedRangeSize = sampler_heap.reserved_size,
    };
    vkCmdBindSamplerHeapEXT(m_command_buffer, &sampler_info);
}

void RM::Command::End() const
{
    vkEndCommandBuffer(m_command_buffer);
}

void RM::Command::BeginRenderPass(const RenderPassCreateInfo& info)
{
    std::vector<VkRenderingAttachmentInfo> colors;
    colors.reserve(info.color_attachments.size());
    for (const auto& [texture, clear_color, store_op, load_op] : info.color_attachments)
    {
        VkRenderingAttachmentInfo attachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = texture->m_attachment_view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = Vulkan::GetLoadOp(load_op),
            .storeOp = Vulkan::GetStoreOp(store_op),
        };
        attachment.clearValue.color = Vulkan::GetClearColor(texture->GetFormat(), clear_color);
        colors.push_back(attachment);
    }

    VkRenderingAttachmentInfo depth{};
    if (info.depth_attachment)
    {
        const auto& [texture, clear_depth, clear_stencil, load_op, store_op] = *info.
            depth_attachment;
        depth = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = texture->m_attachment_view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = Vulkan::GetLoadOp(load_op),
            .storeOp = Vulkan::GetStoreOp(store_op),
        };
        depth.clearValue.depthStencil = {clear_depth, static_cast<uint32_t>(clear_stencil)};
    }

    const VkRenderingInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, {info.size.x, info.size.y}},
        .layerCount = 1,
        .colorAttachmentCount = static_cast<uint32_t>(colors.size()),
        .pColorAttachments = colors.data(),
        .pDepthAttachment = info.depth_attachment ? &depth : nullptr,
    };
    vkCmdBeginRendering(m_command_buffer, &rendering_info);
    vkCmdSetStencilReference(m_command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
}

void RM::Command::EndRenderPass()
{
    vkCmdEndRendering(m_command_buffer);
}

void RM::Command::BeginComputePass()
{
}

void RM::Command::EndComputePass()
{
}

RM::Result<void> RM::Command::Draw(const uint32_t vertex_count,
                                   const uint32_t instance_count,
                                   const uint32_t start_vertex,
                                   const uint32_t start_instance) const
{
    vkCmdDraw(m_command_buffer, vertex_count, instance_count, start_vertex, start_instance);
    return {};
}

RM::Result<void> RM::Command::DrawIndirect(const Buffer* buffer, const uint64_t offset) const
{
#ifdef RIMERA_DEBUG
    if (!buffer) return std::unexpected("Cannot draw from a null indirect buffer");
    if (offset % 4 || offset + sizeof(DrawIndirectArguments) > buffer->GetLength())
        return std::unexpected("Invalid indirect draw offset");
#endif
    vkCmdDrawIndirect(m_command_buffer,
                      Vulkan::FromNative<VkBuffer>(buffer->GetNativeHandle()),
                      offset,
                      1,
                      sizeof(DrawIndirectArguments));
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
    vkCmdDrawIndexed(m_command_buffer,
                     index_count,
                     instance_count,
                     start_index,
                     static_cast<int32_t>(start_vertex),
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
    vkCmdBindIndexBuffer2(m_command_buffer,
                          Vulkan::FromNative<VkBuffer>(buffer->GetNativeHandle()),
                          0,
                          VK_WHOLE_SIZE,
                          format == Format::R16_UInt ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
#ifdef RIMERA_DEBUG
    m_index_buffer = {
        .address = buffer->GetGPUAddress(),
        .length = buffer->GetLength(),
        .index_size = format == Format::R16_UInt ? 2u : 4u,
    };
#endif
    return {};
}

RM::Result<void> RM::Command::DispatchMesh(const DispatchGroup dispatch_group) const
{
    vkCmdDrawMeshTasksEXT(m_command_buffer, dispatch_group.x, dispatch_group.y, dispatch_group.z);
    return {};
}

RM::Result<void> RM::Command::DispatchMeshIndirect(const Buffer* buffer,
                                                   const uint64_t offset) const
{
#ifdef RIMERA_DEBUG
    if (!buffer) return std::unexpected("Cannot dispatch mesh threadgroups from a null indirect buffer");
    if (offset % 4 || offset + sizeof(DispatchGroup) > buffer->GetLength())
        return std::unexpected("Invalid indirect mesh dispatch offset");
#endif
    vkCmdDrawMeshTasksIndirectEXT(m_command_buffer,
                                  Vulkan::FromNative<VkBuffer>(buffer->GetNativeHandle()),
                                  offset,
                                  1,
                                  sizeof(DispatchGroup));
    return {};
}

RM::Result<void> RM::Command::Dispatch(const DispatchGroup dispatch_group)
{
    vkCmdDispatch(m_command_buffer, dispatch_group.x, dispatch_group.y, dispatch_group.z);
    return {};
}

RM::Result<void> RM::Command::DispatchIndirect(const Buffer* buffer, const uint64_t offset)
{
#ifdef RIMERA_DEBUG
    if (!buffer) return std::unexpected("Cannot dispatch from a null indirect buffer");
    if (offset % 4 || offset + sizeof(DispatchGroup) > buffer->GetLength())
        return std::unexpected("Invalid indirect dispatch offset");
#endif
    vkCmdDispatchIndirect(m_command_buffer,
                          Vulkan::FromNative<VkBuffer>(buffer->GetNativeHandle()),
                          offset);
    return {};
}

RM::Result<void> RM::Command::BindShader(const Pipeline* shader)
{
#ifdef RIMERA_DEBUG
    if (!shader) return std::unexpected("Cannot bind a null pipeline");
#endif
    vkCmdBindPipeline(m_command_buffer,
                      shader->GetShaderType() == PipelineType::Compute
                          ? VK_PIPELINE_BIND_POINT_COMPUTE
                          : VK_PIPELINE_BIND_POINT_GRAPHICS,
                      Vulkan::FromNative<VkPipeline>(shader->GetNativeHandle()));
    return {};
}

RM::Result<void> RM::Command::BindConstantBuffer(uint32_t slot, const Buffer* buffer) const
{
#ifdef RIMERA_DEBUG
    if (!buffer || slot >= kConstantBufferSlotCount)
        return std::unexpected("Invalid constant buffer binding");
#endif
    const uint64_t address = buffer->GetGPUAddress();
    const VkPushDataInfoEXT push_info{
        .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT,
        .offset = kRootConstantCount * static_cast<uint32_t>(sizeof(uint32_t)) + slot * static_cast<
                      uint32_t>(sizeof(uint64_t)),
        .data = {&address, sizeof(address)},
    };
    vkCmdPushDataEXT(m_command_buffer, &push_info);
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
    const VkPushDataInfoEXT push_info{
        .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT,
        .offset = offset,
        .data = {data, size},
    };
    vkCmdPushDataEXT(m_command_buffer, &push_info);
    return {};
}

void RM::Command::Barrier(const BarrierGroup& group)
{
    std::vector<VkMemoryBarrier2> memory;
    std::vector<VkImageMemoryBarrier2> images;
    std::vector<VkBufferMemoryBarrier2> buffers;

    const auto memory_barrier = [](const BarrierScope& scope)
    {
        return VkMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = Vulkan::GetPipelineStage(scope.sync_before),
            .srcAccessMask = Vulkan::GetAccess(scope.access_before),
            .dstStageMask = Vulkan::GetPipelineStage(scope.sync_after),
            .dstAccessMask = Vulkan::GetAccess(scope.access_after),
        };
    };

    for (const auto& barrier : group.global_barriers) memory.push_back(
        memory_barrier(barrier.scope));

    for (const auto& barrier : group.texture_barriers)
    {
        const auto old_layout = barrier.discard
                                    ? VK_IMAGE_LAYOUT_UNDEFINED
                                    : Vulkan::GetImageLayout(barrier.layout_before, true);
        const auto new_layout = Vulkan::GetImageLayout(barrier.layout_after, false);
        if (old_layout == VK_IMAGE_LAYOUT_GENERAL && new_layout == VK_IMAGE_LAYOUT_GENERAL)
        {
            memory.push_back(memory_barrier(barrier.scope));
            continue;
        }
        const auto* texture = barrier.texture;
        images.push_back({
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = Vulkan::GetPipelineStage(barrier.scope.sync_before),
            .srcAccessMask = Vulkan::GetAccess(barrier.scope.access_before),
            .dstStageMask = Vulkan::GetPipelineStage(barrier.scope.sync_after),
            .dstAccessMask = Vulkan::GetAccess(barrier.scope.access_after),
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = Vulkan::FromNative<VkImage>(texture->GetNativeHandle()),
            .subresourceRange = {Vulkan::GetAspectMask(texture->GetFormat()),
                                 texture->GetBaseMip() + barrier.subresource.first_mip,
                                 barrier.subresource.num_mips,
                                 texture->GetBaseLayer() + barrier.subresource.first_array_layer,
                                 barrier.subresource.num_array_layers},
        });
    }

    for (const auto& barrier : group.buffer_barriers)
    {
        buffers.push_back({
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = Vulkan::GetPipelineStage(barrier.scope.sync_before),
            .srcAccessMask = Vulkan::GetAccess(barrier.scope.access_before),
            .dstStageMask = Vulkan::GetPipelineStage(barrier.scope.sync_after),
            .dstAccessMask = Vulkan::GetAccess(barrier.scope.access_after),
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = Vulkan::FromNative<VkBuffer>(barrier.buffer->GetNativeHandle()),
            .offset = barrier.offset,
            .size = barrier.size == UINT64_MAX ? VK_WHOLE_SIZE : barrier.size,
        });
    }

    if (memory.empty() && images.empty() && buffers.empty()) return;
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = static_cast<uint32_t>(memory.size()),
        .pMemoryBarriers = memory.data(),
        .bufferMemoryBarrierCount = static_cast<uint32_t>(buffers.size()),
        .pBufferMemoryBarriers = buffers.data(),
        .imageMemoryBarrierCount = static_cast<uint32_t>(images.size()),
        .pImageMemoryBarriers = images.data(),
    };
    vkCmdPipelineBarrier2(m_command_buffer, &dependency);
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
    const VkViewport native{
        .x = static_cast<float>(viewport.offset.x),
        .y = static_cast<float>(viewport.offset.y + viewport.size.y),
        .width = static_cast<float>(viewport.size.x),
        .height = -static_cast<float>(viewport.size.y),
        .minDepth = viewport.depth_range.x,
        .maxDepth = viewport.depth_range.y,
    };
    vkCmdSetViewport(m_command_buffer, 0, 1, &native);
}

void RM::Command::SetScissor(const Rect& rect) const
{
    const VkRect2D native{
        .offset = {static_cast<int32_t>(rect.offset.x), static_cast<int32_t>(rect.offset.y)},
        .extent = {rect.size.x, rect.size.y},
    };
    vkCmdSetScissor(m_command_buffer, 0, 1, &native);
}

void RM::Command::SetPrimitiveTopology(const PrimitiveTopology topology)
{
    vkCmdSetPrimitiveTopology(m_command_buffer, Vulkan::GetPrimitiveTopology(topology));
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
    const VkBufferCopy2 region{
        .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
        .srcOffset = source_offset,
        .dstOffset = dest_offset,
        .size = size,
    };
    const VkCopyBufferInfo2 copy_info{
        .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
        .srcBuffer = Vulkan::FromNative<VkBuffer>(source->GetNativeHandle()),
        .dstBuffer = Vulkan::FromNative<VkBuffer>(dest->GetNativeHandle()),
        .regionCount = 1,
        .pRegions = &region,
    };
    vkCmdCopyBuffer2(m_command_buffer, &copy_info);
    return {};
}

RM::Result<void> RM::Command::CopyBufferToTexture(const Buffer* source,
                                                  const Texture* dest,
                                                  const BufferTextureCopy& region)
{
#ifdef RIMERA_DEBUG
    if (!source || !dest) return std::unexpected("Cannot copy a null resource");
#endif
    const auto block = Vulkan::GetFormatBlock(dest->GetFormat());
    if (!block.bytes) return std::unexpected("Texture format does not support buffer copies");
    const auto footprint = m_device->GetCopyFootprint(dest, region.mip_level);
    const auto size = dest->GetDimensions();
    const VkBufferImageCopy2 copy{
        .sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
        .bufferOffset = region.buffer_offset,
        .bufferRowLength = footprint.bytes_per_row / block.bytes * block.width,
        .bufferImageHeight = 0,
        .imageSubresource = {Vulkan::GetAspectMask(dest->GetFormat()),
                             dest->GetBaseMip() + region.mip_level,
                             dest->GetBaseLayer() + region.array_layer,
                             1},
        .imageExtent = {std::max(1u, size.x >> region.mip_level),
                        std::max(1u, size.y >> region.mip_level),
                        1},
    };
    const VkCopyBufferToImageInfo2 copy_info{
        .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
        .srcBuffer = Vulkan::FromNative<VkBuffer>(source->GetNativeHandle()),
        .dstImage = Vulkan::FromNative<VkImage>(dest->GetNativeHandle()),
        .dstImageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .regionCount = 1,
        .pRegions = &copy,
    };
    vkCmdCopyBufferToImage2(m_command_buffer, &copy_info);
    return {};
}

RM::Result<void> RM::Command::CopyTexture(const Texture* source, const Texture* dest)
{
#ifdef RIMERA_DEBUG
    if (!source || !dest) return std::unexpected("Cannot copy a null texture");
    if (source->GetDimensions().x != dest->GetDimensions().x ||
        source->GetDimensions().y != dest->GetDimensions().y ||
        source->GetMipLevels() != dest->GetMipLevels() ||
        source->GetArrayLayers() != dest->GetArrayLayers())
        return std::unexpected("Texture copy ranges must match");
#endif
    std::vector<VkImageCopy2> regions;
    regions.reserve(source->GetMipLevels());
    const auto dimensions = source->GetDimensions();
    for (uint32_t mip = 0; mip < source->GetMipLevels(); ++mip)
    {
        regions.push_back({
            .sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2,
            .srcSubresource = {Vulkan::GetAspectMask(source->GetFormat()),
                               source->GetBaseMip() + mip,
                               source->GetBaseLayer(),
                               source->GetArrayLayers()},
            .dstSubresource = {Vulkan::GetAspectMask(dest->GetFormat()),
                               dest->GetBaseMip() + mip,
                               dest->GetBaseLayer(),
                               dest->GetArrayLayers()},
            .extent = {std::max(1u, dimensions.x >> mip), std::max(1u, dimensions.y >> mip), 1},
        });
    }
    const VkCopyImageInfo2 copy_info{
        .sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2,
        .srcImage = Vulkan::FromNative<VkImage>(source->GetNativeHandle()),
        .srcImageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .dstImage = Vulkan::FromNative<VkImage>(dest->GetNativeHandle()),
        .dstImageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .regionCount = static_cast<uint32_t>(regions.size()),
        .pRegions = regions.data(),
    };
    vkCmdCopyImage2(m_command_buffer, &copy_info);
    return {};
}

RM::Result<void> RM::Command::Blit(const Texture* source,
                                   const Texture* dest,
                                   const BlitRegion& region)
{
    auto src_dim = source->GetDimensions();
    auto dst_dim = dest->GetDimensions();

    auto pipeline = m_device->m_blit_pipelines.find(dest->GetFormat());
    if (pipeline == m_device->m_blit_pipelines.end())
    {
        auto result = Helpers::CreateBlitShader(m_device, dest->GetFormat());
        if (!result) return std::unexpected(result.error());
        pipeline = m_device->m_blit_pipelines.emplace(dest->GetFormat(), *result).first;
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

    *m_blit_data = {
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
    m_blit_buffer->Unmap();

    BeginRenderPass(RenderPassBuilder(dst_dim).Color(dest).Build());

    if (auto result = BindShader(pipeline->second); !result) return std::unexpected(result.error());
    if (auto result = BindConstantBuffer(1, m_blit_buffer); !result)
        return std::unexpected(result.error());

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
    return m_command_buffer;
}

void* RM::Command::GetNativeAllocator() const
{
    return Vulkan::ToNative(m_command_pool);
}

RM::Result<void> RM::Command::Initialize(Device* device, QueueType queue_type)
{
    m_device = device;
    m_queue_type = queue_type;
    const auto native_device = static_cast<VkDevice>(device->GetNativeHandle());
    const VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = device->m_queue_families[Vulkan::GetQueueIndex(queue_type)].index,
    };
    if (const auto result = vkCreateCommandPool(native_device, &pool_info, nullptr, &m_command_pool)
        ;
        result != VK_SUCCESS)
        return std::unexpected("Failed to create command pool, " + Vulkan::ResultMessage(result));
    const VkCommandBufferAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (const auto result = vkAllocateCommandBuffers(native_device,
                                                     &allocate_info,
                                                     &m_command_buffer);
        result != VK_SUCCESS)
        return std::unexpected("Failed to allocate command buffer, " +
                               Vulkan::ResultMessage(result));

    auto blit_buffer_result = device->CreateBuffer({
        .size = sizeof(BlitPassData),
        .data = nullptr,
        .memory_type = MemoryType::HostVisible,
        .flags = BufferFlags::eUnorderedAccess,
    });
    if (!blit_buffer_result) return std::unexpected(blit_buffer_result.error());
    m_blit_buffer = *blit_buffer_result;
    auto mapped = m_blit_buffer->Map();
    if (!mapped) return std::unexpected(mapped.error());
    m_blit_data = static_cast<BlitPassData*>(*mapped);

    SamplerCreateInfo create_info{
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
    m_sampler = *sampler_result;
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

void RM::Command::SetStencilReference(uint32_t reference)
{
    vkCmdSetStencilReference(m_command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
}