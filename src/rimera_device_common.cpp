#include "rimera_device.hpp"
#include "rimera_helpers.hpp"
#include "rimera_render_graph.hpp"

RM::Result<RM::Device*> RM::CreateDevice(const DeviceCreateInfo& info)
{
    return Device::CreateObject<Device>(info);
}

void RM::DestroyDevice(const Device* device)
{
    delete device;
}

RM::Result<RM::Fence*> RM::Device::CreateFence()
{
    return CreateObject<Fence>(this);
}

void RM::Device::DestroyImmediate(const Fence* fence)
{
    if (!fence) return;
    FlushRetired(fence);
    delete fence;
}

RM::Result<RM::Queue*> RM::Device::CreateQueue(QueueType type)
{
    auto result = CreateObject<Queue>(this, type);
    if (!result) return std::unexpected(result.error());
    auto* queue = *result;
    queue->m_id = ++m_queue_id;
#ifdef RIMERA_DEBUG
    m_queues.emplace(queue->m_id, queue);
#endif
    queue->AddResidencySet(m_core_residency_set);
    return queue;
}

void RM::Device::DestroyImmediate(const Queue* queue)
{
    if (!queue) return;
#ifdef RIMERA_DEBUG
    m_queues.erase(queue->m_id);
#endif
    delete queue;
}

RM::Result<RM::Swapchain*> RM::Device::CreateSwapchain(Queue* queue,
                                                       const SwapchainCreateInfo& info)
{
    return CreateObject<Swapchain>(this, queue, info);
}

void RM::Device::DestroyImmediate(const Swapchain* swapchain) const
{
    delete swapchain;
}

RM::Result<RM::Command*> RM::Device::CreateCommand(CommandType type)
{
    return CreateObject<Command>(this, type);
}

void RM::Device::DestroyImmediate(const Command* command) const
{
    delete command;
}

RM::Result<RM::CommandRing*> RM::Device::CreateCommandRing(QueueType type)
{
    return CreateObject<CommandRing>(this, type);
}

void RM::Device::DestroyImmediate(const CommandRing* command_ring) const
{
    delete command_ring;
}

RM::Result<RM::ResidencySet*> RM::Device::CreateResidencySet()
{
    return CreateObject<ResidencySet>(this);
}

void RM::Device::DestroyImmediate(const ResidencySet* residency_set) const
{
    delete residency_set;
}

RM::Result<RM::Pipeline*> RM::Device::CreatePipeline(const ComputePipelineCreateInfo& info)
{
    return CreateObject<Pipeline>(this, info);
}

RM::Result<RM::Pipeline*> RM::Device::CreatePipeline(const GraphicsPipelineCreateInfo& info)
{
    return CreateObject<Pipeline>(this, info);
}

RM::Result<RM::Pipeline*> RM::Device::CreatePipeline(const MeshGraphicsPipelineCreateInfo& info)
{
    return CreateObject<Pipeline>(this, info);
}

void RM::Device::DestroyImmediate(const Pipeline* pipeline) const
{
    delete pipeline;
}

RM::Result<RM::Texture*> RM::Device::CreateTexture(const TextureCreateInfo& info)
{
    return CreateObject<Texture>(this, info);
}

void RM::Device::DestroyImmediate(const Texture* texture) const
{
#ifdef RIMERA_DEBUG
    if (texture) texture->ValidateIdle("Texture");
#endif
    delete texture;
}

RM::Result<RM::Texture*> RM::Device::CreateTextureView(const TextureViewCreateInfo& info)
{
    return CreateObject<Texture>(this, info);
}

RM::Result<RM::Buffer*> RM::Device::CreateBuffer(const BufferCreateInfo& info)
{
    return CreateObject<Buffer>(this, info);
}

void RM::Device::DestroyImmediate(const Buffer* buffer) const
{
#ifdef RIMERA_DEBUG
    if (buffer) buffer->ValidateIdle("Buffer");
#endif
    delete buffer;
}

RM::Result<RM::Sampler*> RM::Device::CreateSampler(const SamplerCreateInfo& info)
{
    return CreateObject<Sampler>(this, info);
}

void RM::Device::DestroyImmediate(const Sampler* sampler) const
{
    delete sampler;
}

RM::Result<RM::Heap*> RM::Device::CreateHeap(const HeapCreateInfo& info)
{
    return CreateObject<Heap>(this, info);
}

void RM::Device::DestroyImmediate(const Heap* heap) const
{
#ifdef RIMERA_DEBUG
    if (heap) heap->ValidateIdle("Heap");
#endif
    delete heap;
}

RM::Result<RM::RenderGraph*> RM::Device::CreateRenderGraph(const RenderGraphCreateInfo& info)
{
    return CreateObject<RenderGraph>(this, info);
}

void RM::Device::DestroyImmediate(const RenderGraph* render_graph) const
{
    delete render_graph;
}

RM::Result<void> RM::Device::UploadTexture(Texture* texture, const void* data)
{
    const auto footprint = GetCopyFootprint(texture);
    if (!footprint.size || footprint.size > UINT32_MAX)
        return std::unexpected("Texture format does not support initial data");

    if (!m_upload_queue)
    {
        auto queue = CreateQueue(QueueType::Transfer);
        if (!queue) return std::unexpected(queue.error());
        m_upload_queue = *queue;
    }
    if (!m_upload_command)
    {
        auto command = CreateCommand(CommandType::Graphics);
        if (!command) return std::unexpected(command.error());
        m_upload_command = *command;
    }
    if (!m_upload_fence)
    {
        auto fence = CreateFence();
        if (!fence) return std::unexpected(fence.error());
        m_upload_fence = *fence;
    }

    auto staging_result = CreateBuffer({
        .size = static_cast<uint32_t>(footprint.size),
        .data = nullptr,
        .flags = BufferFlags::eNone,
    });
    if (!staging_result) return std::unexpected(staging_result.error());
    auto* staging = *staging_result;

    auto mapped = staging->Map();
    if (!mapped)
    {
        DestroyImmediate(staging);
        return std::unexpected(mapped.error());
    }
    auto* destination = static_cast<uint8_t*>(*mapped);
    const auto* source = static_cast<const uint8_t*>(data);
    for (uint32_t row = 0; row < footprint.row_count; ++row)
    {
        std::memcpy(destination + uint64_t{row} * footprint.bytes_per_row,
                    source + uint64_t{row} * footprint.row_size,
                    footprint.row_size);
    }
    staging->Unmap();

    m_core_residency_set->AddAllocation(texture);
    m_core_residency_set->AddAllocation(staging);
    m_core_residency_set->Commit();

    m_upload_command->Begin();
    auto result = m_upload_command->CopyBufferToTexture(staging, texture, {});
    m_upload_command->End();
    if (result)
    {
        m_upload_queue->Submit(m_upload_command);
        auto value = m_upload_queue->Signal(m_upload_fence);
        result = value ? m_upload_fence->Wait(*value) : Result<void>(std::unexpected(value.error()));
    }

    m_core_residency_set->RemoveAllocation(texture);
    m_core_residency_set->Commit();
    DestroyImmediate(staging);
    return result;
}

void RM::Device::ReleaseRetired()
{
    std::vector<RetiredObject> completed;
    std::erase_if(m_retired,
                  [&](const RetiredObject& retired)
                  {
                      if (!retired.fence->IsComplete(retired.value)) return false;
                      completed.push_back(retired);
                      return true;
                  });
    for (const auto& retired : completed) retired.destroy(retired.object);
}

void RM::Device::FlushRetired(const Fence* fence)
{
    std::vector<RetiredObject> flushed;
    std::erase_if(m_retired,
                  [&](const RetiredObject& retired)
                  {
                      if (fence && retired.fence != fence) return false;
                      flushed.push_back(retired);
                      return true;
                  });
    for (const auto& retired : flushed)
    {
        if (retired.fence->GetSignaledValue() >= retired.value)
            static_cast<void>(retired.fence->Wait(retired.value));
        retired.destroy(retired.object);
    }
}

#ifdef RIMERA_DEBUG
const RM::Queue* RM::Device::FindQueue(uint64_t id) const
{
    const auto it = m_queues.find(id);
    return it == m_queues.end() ? nullptr : it->second;
}
#endif