#include "rimera_device.hpp"
#include "rimera_metal.hpp"
#define IR_RUNTIME_METALCPP
#define IR_PRIVATE_IMPLEMENTATION
#include "rimera_helpers.hpp"
#include "metal_irconverter_runtime/metal_irconverter_runtime.h"
#include "metal_irconverter/metal_irconverter.h"

RM::Result<RM::Device*> RM::CreateDevice(const DeviceCreateInfo& info)
{
    return Device::CreateObject<Device>(info);
}

void RM::DestroyDevice(const Device* device)
{
    delete device;
}

RM::Result<void> RM::Device::Initialize(const DeviceCreateInfo& info)
{
    if (!info.window || !info.width || !info.height || !info.resource_descriptor_count ||
        !info.sampler_descriptor_count)
        return std::unexpected("Invalid device creation parameters");
    m_device = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    if (!m_device) return std::unexpected("No Metal device is available");
    m_swap_layer = Metal::CreateSwapLayer(m_device, info);
    if (!m_swap_layer) return std::unexpected("Failed to create swapchain layer");
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

    auto graphics_queue_result = CreateQueue(QueueType::Graphics);
    if (!graphics_queue_result) return std::unexpected(graphics_queue_result.error());
    m_graphics_queue = std::move(*graphics_queue_result);

    if (auto result = CreateRootSignature(); !result) return std::unexpected(result.error());

    m_core_residency_set->Commit();
    m_core_residency_set->RequestResidency();

    auto blit_bgra8_result = Helpers::CreateBlitShader(this, Format::BGRA8_UNorm_sRGB);
    if (!blit_bgra8_result) return std::unexpected(blit_bgra8_result.error());
    m_blit_pipelines[Format::BGRA8_UNorm_sRGB] = std::move(*blit_bgra8_result);
    auto blit_hdr_result = Helpers::CreateBlitShader(this, Format::BGRA10_XR_sRGB);
    if (!blit_hdr_result) return std::unexpected(blit_hdr_result.error());
    m_blit_pipelines[Format::BGRA10_XR_sRGB] = std::move(*blit_hdr_result);
    return {};
}

RM::Device::~Device()
{
    if (m_graphics_queue) static_cast<void>(m_graphics_queue->WaitIdle());
    for (auto [id, texture] : m_swapchain_textures) delete texture;
    for (auto [format, pipeline] : m_blit_pipelines) delete pipeline;
    if (m_core_residency_set)
    {
        m_core_residency_set->RemoveAllAllocations();
        m_core_residency_set->EndResidency();
        delete m_core_residency_set;
    }
    delete m_graphics_queue;
    if (m_root_sig) IRRootSignatureDestroy(m_root_sig);
}

RM::Result<RM::Fence*> RM::Device::CreateFence() { return CreateObject<Fence>(this); }

void RM::Device::DestroyFence(const Fence* fence) { delete fence; }

RM::Result<RM::Queue*> RM::Device::CreateQueue(const QueueType type)
{
    auto result = CreateObject<Queue>(this, type);
    if (!result) return std::unexpected(result.error());
    auto* queue = *result;
    queue->AddResidencySet(m_swap_layer->residencySet());
    queue->AddResidencySet(m_core_residency_set);
    return queue;
}

void RM::Device::DestroyQueue(const Queue* queue) { delete queue; }

RM::Result<RM::Command*> RM::Device::CreateCommand(CommandType type)
{
    return CreateObject<Command>(this, type);
}

void RM::Device::DestroyCommand(const Command* command) const { delete command; }

RM::Result<RM::CommandRing*> RM::Device::CreateCommandRing(QueueType type)
{
    return CreateObject<CommandRing>(this, type);
}

void RM::Device::DestroyCommandRing(const CommandRing* command_ring) const { delete command_ring; }

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

RM::Result<RM::Texture*> RM::Device::CreateTextureView(const TextureViewCreateInfo& info)
{
    return CreateObject<Texture>(this, info);
}

RM::Result<RM::Sampler*> RM::Device::CreateSampler(const SamplerCreateInfo& info)
{
    return CreateObject<Sampler>(this, info);
}

void RM::Device::DestroySampler(const Sampler* sampler)
{
    delete sampler;
}

RM::Texture* RM::Device::GetCurrentBackBuffer()
{
    if (!m_drawable) return nullptr;
    const auto it = m_swapchain_textures.find(m_drawable->texture()->gpuResourceID()._impl);
    return it == m_swapchain_textures.end() ? nullptr : it->second;
}

void RM::Device::DestroyPipeline(const Pipeline* shader) { delete shader; }

RM::Result<RM::Texture*> RM::Device::CreateTexture(const TextureCreateInfo& info)
{
    return CreateObject<Texture>(this, info);
}

void RM::Device::DestroyTexture(const Texture* texture) const { delete texture; }

RM::Result<RM::Buffer*> RM::Device::CreateBuffer(const BufferCreateInfo& info)
{
    return CreateObject<Buffer>(this, info);
}

void RM::Device::DestroyBuffer(const Buffer* buffer) { delete buffer; }

RM::Result<void> RM::Device::NewFrame(Queue* queue)
{
    m_frame_pool.reset();
    m_frame_pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    m_drawable = m_swap_layer->nextDrawable();
    if (!m_drawable) return std::unexpected("Failed to acquire a swapchain drawable");

    auto* texture = m_drawable->texture();
    const auto resource_id = texture->gpuResourceID()._impl;
    if (!m_swapchain_textures.contains(resource_id))
    {
        auto result = CreateObject<Texture>(this, texture);
        if (!result) return std::unexpected(result.error());
        m_swapchain_textures.emplace(resource_id, *result);
    }

    auto* native_queue = static_cast<MTL4::CommandQueue*>(queue->GetNativeHandle());
    native_queue->wait(m_drawable);
    return {};
}

RM::Result<void> RM::Device::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) return {};

    const auto current_size = m_swap_layer->drawableSize();
    if (current_size.width == width && current_size.height == height) return {};

    if (auto result = m_graphics_queue->WaitIdle(); !result) return std::unexpected(result.error());
    for (auto [id, texture] : m_swapchain_textures) delete texture;
    m_swapchain_textures.clear();
    m_drawable = nullptr;

    m_swap_layer->setDrawableSize(CGSizeMake(width, height));
    return {};
}

void* RM::Device::GetNativeHandle() const { return m_device.get(); }

RM::Result<void> RM::Device::CreateRootSignature()
{
    IRRootParameter1 params[2]{}; // Bump to 3 when adding push constants

    params[0].ParameterType = IRRootParameterTypeCBV;
    params[0].ShaderVisibility = IRShaderVisibilityAll;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;

    params[1].ParameterType = IRRootParameterTypeCBV;
    params[1].ShaderVisibility = IRShaderVisibilityAll;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].Descriptor.RegisterSpace = 0;

    // params[2].ParameterType = IRRootParameterType32BitConstants;
    // params[2].ShaderVisibility = IRShaderVisibilityAll;
    // params[2].Constants.ShaderRegister = 2;
    // params[2].Constants.RegisterSpace = 0;
    // params[2].Constants.Num32BitValues = 4;

    IRVersionedRootSignatureDescriptor desc{};
    desc.version = IRRootSignatureVersion_1_1;
    desc.desc_1_1.NumParameters = 2;
    desc.desc_1_1.pParameters = params;
    desc.desc_1_1.Flags =
        static_cast<IRRootSignatureFlags>(IRRootSignatureFlagCBVSRVUAVHeapDirectlyIndexed |
                                          IRRootSignatureFlagSamplerHeapDirectlyIndexed);

    IRError* error = nullptr;
    m_root_sig = IRRootSignatureCreateFromDescriptor(&desc, &error);
    if (error) IRErrorDestroy(error);

    if (!m_root_sig) return std::unexpected("Failed to create root signature");

    m_arg_buffer =
        NS::TransferPtr(m_device->newBuffer(sizeof(uint64_t) * 2, MTL::ResourceStorageModeShared));
    auto arg_table_desc = NS::TransferPtr(MTL4::ArgumentTableDescriptor::alloc()->init());
    arg_table_desc->setMaxBufferBindCount(3);

    if (!m_arg_buffer) return std::unexpected("Failed to allocate argument buffer");
    m_core_residency_set->AddAllocation(m_arg_buffer.get());

    NS::Error* arg_error = nullptr;
    m_arg_table = NS::TransferPtr(m_device->newArgumentTable(arg_table_desc.get(), &arg_error));
    if (!m_arg_table) return std::unexpected("Failed to create argument table");
    m_arg_table->setAddress(m_resource_descriptor_heap->gpuAddress(), kIRDescriptorHeapBindPoint);
    m_arg_table->setAddress(m_sampler_descriptor_heap->gpuAddress(), kIRSamplerHeapBindPoint);
    m_arg_table->setAddress(m_arg_buffer->gpuAddress(), kIRArgumentBufferBindPoint);
    return {};
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
