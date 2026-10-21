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

void RM::Device::DestroyFence(const Fence* fence)
{
    delete fence;
}

void RM::Device::DestroyQueue(const Queue* queue)
{
    delete queue;
}

RM::Result<RM::Command*> RM::Device::CreateCommand(CommandType type)
{
    return CreateObject<Command>(this, type);
}

void RM::Device::DestroyCommand(const Command* command) const
{
    delete command;
}

RM::Result<RM::CommandRing*> RM::Device::CreateCommandRing(QueueType type)
{
    return CreateObject<CommandRing>(this, type);
}

void RM::Device::DestroyCommandRing(const CommandRing* command_ring) const
{
    delete command_ring;
}

RM::Result<RM::ResidencySet*> RM::Device::CreateResidencySet()
{
    return CreateObject<ResidencySet>(this);
}

void RM::Device::DestroyResidencySet(const ResidencySet* residency_set) const
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

void RM::Device::DestroyPipeline(const Pipeline* pipeline)
{
    delete pipeline;
}

RM::Result<RM::Texture*> RM::Device::CreateTexture(const TextureCreateInfo& info)
{
    return CreateObject<Texture>(this, info);
}

void RM::Device::DestroyTexture(const Texture* texture) const
{
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

void RM::Device::DestroyBuffer(const Buffer* buffer)
{
    delete buffer;
}

RM::Result<RM::Sampler*> RM::Device::CreateSampler(const SamplerCreateInfo& info)
{
    return CreateObject<Sampler>(this, info);
}

void RM::Device::DestroySampler(const Sampler* sampler)
{
    delete sampler;
}

RM::Result<RM::Heap*> RM::Device::CreateHeap(const HeapCreateInfo& info)
{
    return CreateObject<Heap>(this, info);
}

void RM::Device::DestroyHeap(const Heap* heap) const
{
    delete heap;
}

RM::Result<RM::RenderGraph*> RM::Device::CreateRenderGraph(const RenderGraphCreateInfo& info)
{
    return CreateObject<RenderGraph>(this, info);
}

void RM::Device::DestroyRenderGraph(const RenderGraph* render_graph) const
{
    delete render_graph;
}

RM::Result<void> RM::Device::UploadTexture(Texture* texture, const void* data)
{
    const auto footprint = GetCopyFootprint(texture);
    if (!footprint.size || footprint.size > UINT32_MAX)
        return std::unexpected("Texture format does not support initial data");

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
        DestroyBuffer(staging);
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
        m_graphics_queue->Submit(m_upload_command);
        result = m_graphics_queue->Signal(m_upload_fence, ++m_upload_value);
        if (result) result = m_upload_fence->Wait(m_upload_value);
    }

    m_core_residency_set->RemoveAllocation(staging);
    m_core_residency_set->RemoveAllocation(texture);
    m_core_residency_set->Commit();
    DestroyBuffer(staging);
    return result;
}