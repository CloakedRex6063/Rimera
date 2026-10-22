#include "rimera_device.hpp"
#include "rimera_metal.hpp"
#define IR_RUNTIME_METALCPP
#define IR_PRIVATE_IMPLEMENTATION
#include "rimera_helpers.hpp"
#include "metal_irconverter_runtime/metal_irconverter_runtime.h"
#include "metal_irconverter/metal_irconverter.h"

RM::Result<void> RM::Device::Initialize(const DeviceCreateInfo& info)
{
    if (!info.resource_descriptor_count || !info.sampler_descriptor_count)
        return std::unexpected("Invalid device creation parameters");
    m_device = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    if (!m_device) return std::unexpected("No Metal device is available");
    auto core_residency_set_result = CreateResidencySet();
    if (!core_residency_set_result) return std::unexpected(core_residency_set_result.error());
    m_core_residency_set = std::move(*core_residency_set_result);
    m_resource_descriptor_heap = NS::TransferPtr(
        m_device->newBuffer(sizeof(IRDescriptorTableEntry) * info.resource_descriptor_count,
                            MTL::ResourceStorageModeShared));
    m_sampler_descriptor_heap = NS::TransferPtr(
        m_device->newBuffer(sizeof(IRDescriptorTableEntry) * info.sampler_descriptor_count,
                            MTL::ResourceStorageModeShared));

    if (!m_resource_descriptor_heap || !m_sampler_descriptor_heap)
        return std::unexpected("Failed to allocate descriptor heaps");
    m_core_residency_set->AddAllocation(m_resource_descriptor_heap.get());
    m_core_residency_set->AddAllocation(m_sampler_descriptor_heap.get());

    auto frame_fence_result = CreateFence();
    if (!frame_fence_result) return std::unexpected(frame_fence_result.error());
    m_frame_fence = *frame_fence_result;

    if (auto result = CreateRootSignature(); !result) return std::unexpected(result.error());

    m_core_residency_set->Commit();
    m_core_residency_set->RequestResidency();

    for (const auto format : {Format::RGBA16_Float, Format::BGRA8_UNorm})
    {
        auto blit_result = Helpers::CreateBlitShader(this, format);
        if (!blit_result) return std::unexpected(blit_result.error());
        m_blit_pipelines[format] = *blit_result;
    }
    return {};
}

RM::Device::~Device()
{
    if (m_frame_fence) static_cast<void>(m_frame_fence->Wait());
    FlushRetired(nullptr);
    if (m_upload_command) DestroyImmediate(m_upload_command);
    if (m_upload_queue) DestroyImmediate(m_upload_queue);
    if (m_upload_fence) DestroyImmediate(m_upload_fence);
    if (m_frame_fence) DestroyImmediate(m_frame_fence);
    for (auto [format, pipeline] : m_blit_pipelines) DestroyImmediate(pipeline);
    DestroyImmediate(m_core_residency_set);
#ifdef RIMERA_DEBUG
    ValidateLiveObjects();
#endif
    if (m_root_sig) IRRootSignatureDestroy(m_root_sig);
}

RM::Result<void> RM::Device::NewFrame(Queue* queue)
{
    auto value = queue->Signal(m_frame_fence);
    if (!value) return std::unexpected(value.error());
    m_frame_values[m_current_frame] = *value;
    m_current_frame = (m_current_frame + 1) % m_frame_values.size();
    if (auto result = m_frame_fence->Wait(m_frame_values[m_current_frame]); !result)
        return result;
    ReleaseRetired();

    m_frame_pool.reset();
    m_frame_pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    return {};
}

void* RM::Device::GetNativeHandle() const { return m_device.get(); }

RM::Result<void> RM::Device::CreateRootSignature()
{
    IRRootParameter1 params[Command::kConstantBufferSlotCount + 1]{};
    for (uint32_t slot = 0; slot <= Command::kRootConstantSlot; ++slot)
    {
        params[slot].ParameterType = IRRootParameterTypeCBV;
        params[slot].ShaderVisibility = IRShaderVisibilityAll;
        params[slot].Descriptor.ShaderRegister = slot;
        params[slot].Descriptor.RegisterSpace = 0;
    }

    IRVersionedRootSignatureDescriptor desc{};
    desc.version = IRRootSignatureVersion_1_1;
    desc.desc_1_1.NumParameters = Command::kConstantBufferSlotCount + 1;
    desc.desc_1_1.pParameters = params;
    desc.desc_1_1.Flags =
        static_cast<IRRootSignatureFlags>(IRRootSignatureFlagCBVSRVUAVHeapDirectlyIndexed |
                                          IRRootSignatureFlagSamplerHeapDirectlyIndexed);

    IRError* error = nullptr;
    m_root_sig = IRRootSignatureCreateFromDescriptor(&desc, &error);
    if (error) IRErrorDestroy(error);

    if (!m_root_sig) return std::unexpected("Failed to create root signature");
    return {};
}

RM::Result<RM::ResourceAllocationInfo> RM::Device::GetAllocationInfo(
    const TextureCreateInfo& info,
    HeapMemory memory) const
{
    if (auto result = Texture::ValidateCreateInfo(info); !result)
        return std::unexpected(result.error());
    auto descriptor = Metal::CreateTextureDescriptor(info);
    descriptor->setStorageMode(Metal::ToStorageMode(memory));
    descriptor->setHazardTrackingMode(MTL::HazardTrackingModeUntracked);
    const auto size_and_align = m_device->heapTextureSizeAndAlign(descriptor.get());
    return ResourceAllocationInfo{.size = size_and_align.size, .alignment = size_and_align.align};
}

RM::Result<RM::ResourceAllocationInfo> RM::Device::GetAllocationInfo(
    const BufferCreateInfo& info,
    HeapMemory memory) const
{
    if (!info.size) return std::unexpected("Invalid buffer creation parameters");
    const MTL::ResourceOptions options =
        (memory == HeapMemory::HostVisible
             ? MTL::ResourceStorageModeShared
             : MTL::ResourceStorageModePrivate) |
        MTL::ResourceHazardTrackingModeUntracked;
    const auto size_and_align = m_device->heapBufferSizeAndAlign(info.size, options);
    return ResourceAllocationInfo{.size = size_and_align.size, .alignment = size_and_align.align};
}

RM::TextureCopyFootprint RM::Device::GetCopyFootprint(const Texture* texture,
                                                      uint32_t mip_level) const
{
    const auto block = Helpers::GetFormatBlockInfo(texture->GetFormat());
    if (!block.bytes) return {};
    const auto size = texture->GetDimensions();
    const uint32_t width = std::max(1u, size.x >> mip_level);
    const uint32_t height = std::max(1u, size.y >> mip_level);
    const uint32_t row_size = (width + block.width - 1) / block.width * block.bytes;
    const uint32_t row_count = (height + block.height - 1) / block.height;
    return {.bytes_per_row = row_size,
            .row_size = row_size,
            .row_count = row_count,
            .size = uint64_t{row_size} * row_count};
}

RM::Result<uint32_t> RM::Device::AddDescriptor(const Buffer* buffer)
{
    uint32_t descriptor = 0;
    if (m_free_resources.empty())
    {
        if (m_current_resource >=
            m_resource_descriptor_heap->length() / sizeof(IRDescriptorTableEntry))
            return std::unexpected("Resource descriptor heap is full");
        descriptor = m_current_resource++;
    }
    else
    {
        descriptor = m_free_resources.back();
        m_free_resources.pop_back();
    }
    auto* entries = static_cast<IRDescriptorTableEntry*>(m_resource_descriptor_heap->contents());
    auto* native_buffer = static_cast<MTL::Buffer*>(buffer->GetNativeHandle());
    IRDescriptorTableSetBuffer(&entries[descriptor], native_buffer->gpuAddress(), 0);

    return descriptor;
}

RM::Result<uint32_t> RM::Device::AddDescriptor(const Texture* texture)
{
    uint32_t descriptor = 0;
    if (m_free_resources.empty())
    {
        if (m_current_resource >=
            m_resource_descriptor_heap->length() / sizeof(IRDescriptorTableEntry))
            return std::unexpected("Resource descriptor heap is full");
        descriptor = m_current_resource++;
    }
    else
    {
        descriptor = m_free_resources.back();
        m_free_resources.pop_back();
    }
    auto* entries = static_cast<IRDescriptorTableEntry*>(m_resource_descriptor_heap->contents());
    auto* native_texture = static_cast<MTL::Texture*>(texture->GetNativeHandle());
    IRDescriptorTableSetTexture(&entries[descriptor], native_texture, 0.0f, 0);

    return descriptor;
}

RM::Result<uint32_t> RM::Device::AddDescriptor(Sampler* sampler)
{
    uint32_t descriptor = 0;
    if (m_free_samplers.empty())
    {
        if (m_current_sampler >=
            m_sampler_descriptor_heap->length() / sizeof(IRDescriptorTableEntry))
            return std::unexpected("Sampler descriptor heap is full");
        descriptor = m_current_sampler++;
    }
    else
    {
        descriptor = m_free_samplers.back();
        m_free_samplers.pop_back();
    }
    auto* entries = static_cast<IRDescriptorTableEntry*>(m_sampler_descriptor_heap->contents());
    auto* native_sampler = static_cast<MTL::SamplerState*>(sampler->GetNativeHandle());
    IRDescriptorTableSetSampler(&entries[descriptor], native_sampler, 0.0f);

    return descriptor;
}

void RM::Device::RemoveDescriptor(const Buffer* buffer)
{
    m_free_resources.emplace_back(buffer->GetResourceIndex());
}

void RM::Device::RemoveDescriptor(const Texture* texture)
{
    m_free_resources.emplace_back(texture->GetResourceIndex());
}

void RM::Device::RemoveDescriptor(const Sampler* sampler)
{
    m_free_samplers.emplace_back(sampler->GetSamplerIndex());
}

void* RM::Device::GetNativeRootSignature() const { return m_root_sig; }