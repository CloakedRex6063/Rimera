#include "rimera_command.hpp"
#include "rimera_device.hpp"
#include "rimera_helpers.hpp"
#include "rimera_metal.hpp"
#include "rimera_texture.hpp"

RM::Command::Command(Device* device) : m_device(device)
{
    auto* native_device = (MTL::Device*)device->GetNativeHandle();
    m_command_buffer = NS::TransferPtr(native_device->newCommandBuffer());
    m_command_allocator = NS::TransferPtr(native_device->newCommandAllocator());

    // Could prolly make it once and cache, but too lazy to do it rn
    m_blit_shader = std::shared_ptr<Shader>(Helpers::CreateBlitShader(device));
    m_blit_buffer = std::shared_ptr<Buffer>(
        device->CreateBuffer({.buffer_size = sizeof(BlitPassData), .data = nullptr}));

    m_device->GetCoreResidencySet()->AddAllocation(m_blit_buffer.get());
    m_device->GetCoreResidencySet()->Commit();
    m_device->GetCoreResidencySet()->RequestResidency();
}

RM::Command::~Command()
{
    m_command_buffer.reset();
    m_command_allocator.reset();
}

void RM::Command::Begin() const
{
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
        attachment->setTexture(static_cast<MTL::Texture*>(info_attach.texture->GetNativeHandle()));
        attachment->setClearColor(MTL::ClearColor(r, g, b, a));
        attachment->setLoadAction(static_cast<MTL::LoadAction>(info_attach.load_op));
        attachment->setStoreAction(static_cast<MTL::StoreAction>(info_attach.store_op));
    }

    if (info.depth_attachment.has_value())
    {
        auto info_attach = info.depth_attachment.value();
        auto* attachment = rpd->depthAttachment();
        attachment->setTexture(static_cast<MTL::Texture*>(info_attach.texture->GetNativeHandle()));
        attachment->setLoadAction(static_cast<MTL::LoadAction>(info_attach.load_op));
        attachment->setStoreAction(static_cast<MTL::StoreAction>(info_attach.store_op));
    }

    m_render_encoder = m_command_buffer->renderCommandEncoder(rpd.get());
}

void RM::Command::EndRenderPass() 
{
    m_render_encoder->endEncoding();
    m_render_encoder = nullptr;
}

void RM::Command::BeginComputePass()
{
    m_compute_encoder = m_command_buffer->computeCommandEncoder();
}

void RM::Command::EndComputePass()
{
    m_compute_encoder->endEncoding();
    m_compute_encoder = nullptr;
}

void RM::Command::BlitToSwapchain(const Device* device, const Texture* texture) const
{
    assert(device && texture);

    auto* drawable = static_cast<CA::MetalDrawable*>(device->GetCurrentPresentable());
    if (!drawable) return;

    auto* source = static_cast<MTL::Texture*>(texture->GetNativeHandle());
    auto* destination = drawable->texture();

    auto* encoder = m_command_buffer->computeCommandEncoder();
    encoder->barrierAfterQueueStages(MTL::StageAll,
                                     MTL::StageDispatch,
                                     MTL4::VisibilityOptionDevice);
    encoder->setComputePipelineState(
        static_cast<MTL::ComputePipelineState*>(m_blit_shader->GetNativeHandle()));

    encoder->setArgumentTable(m_device->GetArgTable().get());

    auto* pass_data = static_cast<BlitPassData*>(m_blit_buffer->GetContents());

    *pass_data = {
        .source_index = texture->GetResourceIndex(),
        .dest_index = device->GetCurrentPresentableDescriptor(),
    };

    BindConstantBuffer(1, m_blit_buffer.get());

    encoder->dispatchThreads(MTL::Size(destination->width(), destination->height(), 1),
                             MTL::Size(8, 8, 1));
    encoder->endEncoding();
}

void RM::Command::DispatchMesh(DispatchGroup dispatch_group) const
{
    m_render_encoder->drawMeshThreadgroups(
        MTL::Size(dispatch_group.x, dispatch_group.y, dispatch_group.z),
        MTL::Size(m_threads_per_ampl.x, m_threads_per_ampl.y, m_threads_per_ampl.z),
        MTL::Size(m_threads_per_mesh.x, m_threads_per_mesh.y, m_threads_per_mesh.z));
}

void RM::Command::Dispatch(DispatchGroup dispatch_group) const
{
    m_compute_encoder->dispatchThreads(
        MTL::Size(dispatch_group.x, dispatch_group.y, dispatch_group.z),
        MTL::Size(m_threads_per_compute.x, m_threads_per_compute.y, m_threads_per_compute.z));
}

void RM::Command::BindShader(const Shader* shader) const
{
    switch (shader->GetShaderType())
    {
        case ShaderType::eCompute:
            if (!m_compute_encoder) throw std::runtime_error("Begin the Compute Encoder with BeginComputePass");
            m_compute_encoder->setComputePipelineState(static_cast<MTL::ComputePipelineState*>(shader->GetNativeHandle()));
            break;
        case ShaderType::eRender:
            if (!m_render_encoder) throw std::runtime_error("Begin the Render Encoder with BeginRenderPass");
            m_render_encoder->setRenderPipelineState(static_cast<MTL::RenderPipelineState*>(shader->GetNativeHandle()));
            break;
    }
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

RM::CommandRing::CommandRing(Device* device)
{
    for (auto& command : m_commands)
    {
        command = new Command(device);
    }
}

RM::CommandRing::~CommandRing()
{
    for (const auto& command : m_commands)
    {
        delete command;
    }
}
