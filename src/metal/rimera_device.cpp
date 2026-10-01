#include "rimera_device.hpp"
#include "rimera_metal.hpp"
#define IR_RUNTIME_METALCPP
#define IR_PRIVATE_IMPLEMENTATION
#include "rimera_helpers.hpp"
#include "metal_irconverter_runtime/metal_irconverter_runtime.h"
#include "metal_irconverter/metal_irconverter.h"

void RM::DestroyDevice(const Device* device)
{
    delete device;
}

RM::Device::Device(const DeviceCreateInfo& info)
{
    m_device = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    m_swap_layer = Metal::CreateSwapLayer(m_device, info);
    m_core_residency_set = CreateResidencySet();
    m_resource_descriptor_heap = NS::TransferPtr(
        m_device->newBuffer(sizeof(IRDescriptorTableEntry) * info.resource_descriptor_count,
                            MTL::ResourceStorageModeShared));
    m_sampler_descriptor_heap = NS::TransferPtr(
        m_device->newBuffer(sizeof(IRDescriptorTableEntry) * info.sampler_descriptor_count,
                            MTL::ResourceStorageModeShared));

    m_core_residency_set->AddAllocation(m_resource_descriptor_heap.get());
    m_core_residency_set->AddAllocation(m_sampler_descriptor_heap.get());

    m_graphics_queue = CreateQueue(QueueType::Graphics);

    CreateRootSignature();

    m_core_residency_set->Commit();
    m_core_residency_set->RequestResidency();

    m_blit_pipelines[Format::BGRA8_UNorm] = Helpers::CreateBlitShader(this, Format::BGRA8_UNorm);
    m_blit_pipelines[Format::BGRA10_XR_sRGB] = Helpers::CreateBlitShader(
        this,
        Format::BGRA10_XR_sRGB);
}

RM::Device::~Device()
{
    m_core_residency_set->RemoveAllAllocations();
    m_core_residency_set->EndResidency();
}

RM::Fence* RM::Device::CreateFence() { return new Fence(this); }

void RM::Device::DestroyFence(const Fence* fence) { delete fence; }

RM::Queue* RM::Device::CreateQueue(const QueueType type)
{
    auto* queue = new Queue(this, type);
    queue->AddResidencySet(m_swap_layer->residencySet());
    queue->AddResidencySet(m_core_residency_set);
    return queue;
}

void RM::Device::DestroyQueue(const Queue* queue) { delete queue; }

RM::Command* RM::Device::CreateCommand(CommandType type) { return new Command(this, type); }

void RM::Device::DestroyCommand(const Command* command) const { delete command; }

RM::CommandRing* RM::Device::CreateCommandRing(QueueType type)
{
    return new CommandRing(this, type);
}

void RM::Device::DestroyCommandRing(const CommandRing* command_ring) const { delete command_ring; }

RM::ResidencySet* RM::Device::CreateResidencySet() { return new ResidencySet(this); }

void RM::Device::DestroyResidencySet(const ResidencySet* residency_set) const
{
    delete residency_set;
}

RM::Pipeline* RM::Device::CreatePipeline(const ComputePipelineCreateInfo& info)
{
    return new Pipeline(this, info);
}

RM::Pipeline* RM::Device::CreatePipeline(const GraphicsPipelineCreateInfo& info)
{
    return new Pipeline(this, info);
}

RM::TextureView* RM::Device::CreateTextureView(const TextureViewCreateInfo& info)
{
    return new TextureView(this, info);
}

void RM::Device::DestroyTextureView(const TextureView* texture_view) const
{
    delete texture_view;
}

RM::Sampler* RM::Device::CreateSampler(const SamplerCreateInfo& info)
{
    return new Sampler(this, info);
}

void RM::Device::DestroySampler(const Sampler* sampler)
{
    delete sampler;
}

RM::TextureView* RM::Device::GetCurrentBackBuffer()
{
    return new TextureView(this, m_drawable->texture());
}

void RM::Device::DestroyPipeline(const Pipeline* shader) { delete shader; }

RM::Texture* RM::Device::CreateTexture(const TextureCreateInfo& info)
{
    return new Texture(this, info);
}

void RM::Device::DestroyTexture(const Texture* texture) const { delete texture; }

RM::Buffer* RM::Device::CreateBuffer(const BufferCreateInfo& info)
{
    return new Buffer(this, info);
}

void RM::Device::DestroyBuffer(const Buffer* buffer) { delete buffer; }

void RM::Device::NewFrame(Queue* queue)
{
    m_frame_pool.reset();
    m_frame_pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    m_drawable = m_swap_layer->nextDrawable();
    if (!m_drawable) throw std::runtime_error("Failed to acquire a swapchain drawable");

    auto* texture = m_drawable->texture();
    const auto resource_id = texture->gpuResourceID()._impl;
    if (!m_swapchain_descriptors.contains(resource_id))
    {
        m_swapchain_descriptors.emplace(resource_id, AddDescriptor(texture));
    }

    auto* native_queue = static_cast<MTL4::CommandQueue*>(queue->GetNativeHandle());
    native_queue->wait(m_drawable);
}

void RM::Device::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) return;

    const auto current_size = m_swap_layer->drawableSize();
    if (current_size.width == width && current_size.height == height) return;

    m_drawable = nullptr;

    m_swap_layer->setDrawableSize(CGSizeMake(width, height));
}

void* RM::Device::GetNativeHandle() const { return m_device.get(); }

void RM::Device::CreateRootSignature()
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

    if (!m_root_sig) throw std::runtime_error("Failed to create root signature");

    m_arg_buffer =
        NS::TransferPtr(m_device->newBuffer(sizeof(uint64_t) * 2, MTL::ResourceStorageModeShared));
    auto arg_table_desc = NS::TransferPtr(MTL4::ArgumentTableDescriptor::alloc()->init());
    arg_table_desc->setMaxBufferBindCount(3);

    m_core_residency_set->AddAllocation(m_arg_buffer.get());

    NS::Error* arg_error;
    m_arg_table = NS::TransferPtr(m_device->newArgumentTable(arg_table_desc.get(), &arg_error));
    m_arg_table->setAddress(m_resource_descriptor_heap->gpuAddress(), kIRDescriptorHeapBindPoint);
    m_arg_table->setAddress(m_sampler_descriptor_heap->gpuAddress(), kIRSamplerHeapBindPoint);
    m_arg_table->setAddress(m_arg_buffer->gpuAddress(), kIRArgumentBufferBindPoint);
}

uint32_t RM::Device::AddDescriptor(MTL::Texture* texture)
{
    uint32_t descriptor = 0;
    if (m_free_resources.empty())
    {
        descriptor = m_current_resource++;
    }
    else
    {
        descriptor = m_free_resources.back();
        m_free_resources.pop_back();
    }
    auto* entries = static_cast<IRDescriptorTableEntry*>(m_resource_descriptor_heap->contents());
    IRDescriptorTableSetTexture(&entries[descriptor], texture, 0.0f, 0);
    return descriptor;
}

uint32_t RM::Device::AddDescriptor(const Buffer* buffer)
{
    uint32_t descriptor = 0;
    if (m_free_resources.empty())
    {
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

uint32_t RM::Device::AddDescriptor(const TextureView* texture)
{
    uint32_t descriptor = 0;
    if (m_free_resources.empty())
    {
        descriptor = m_current_resource++;
    }
    else
    {
        descriptor = m_free_resources.back();
        m_free_resources.pop_back();
    }
    auto* entries = static_cast<IRDescriptorTableEntry*>(m_resource_descriptor_heap->contents());
    auto* native_texture = static_cast<MTL::Texture*>(texture->GetView());
    IRDescriptorTableSetTexture(&entries[descriptor], native_texture, 0.0f, 0);

    return descriptor;
}

uint32_t RM::Device::AddDescriptor(Sampler* sampler)
{
    uint32_t descriptor = 0;
    if (m_free_resources.empty())
    {
        descriptor = m_current_resource++;
    }
    else
    {
        descriptor = m_free_resources.back();
        m_free_resources.pop_back();
    }
    auto* entries = static_cast<IRDescriptorTableEntry*>(m_resource_descriptor_heap->contents());
    auto* native_sampler = static_cast<MTL::SamplerState*>(sampler->GetNativeHandle());
    IRDescriptorTableSetSampler(&entries[descriptor], native_sampler, 0.0f);

    return descriptor;
}

void RM::Device::RemoveDescriptor(const Buffer* buffer)
{
    m_free_resources.emplace_back(buffer->GetResourceIndex());
}

void RM::Device::RemoveDescriptor(const TextureView* texture)
{
    m_free_resources.emplace_back(texture->GetResourceIndex());
}

void RM::Device::RemoveDescriptor(const Sampler* sampler)
{
    m_free_resources.emplace_back(sampler->GetSamplerIndex());
}

void* RM::Device::GetNativeRootSignature() const { return m_root_sig; }