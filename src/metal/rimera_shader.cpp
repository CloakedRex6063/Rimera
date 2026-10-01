#include "rimera_shader.hpp"
#include "rimera_metal.hpp"
#include "rimera_helpers.hpp"
#include "stdexcept"
#include "string"

namespace
{
    std::runtime_error MetalError(const char* operation, const NS::Error* error)
    {
        const auto* description = error ? error->localizedDescription() : nullptr;
        const char* message = description ? description->utf8String() : nullptr;
        return std::runtime_error(std::string(operation) + ": " +
                                  (message ? message : "Metal returned no error description"));
    }
} // namespace

void* RM::Pipeline::GetNativeHandle() const
{
    switch (m_type)
    {
        case PipelineType::Compute:
            return m_compute_pso.get();
        case PipelineType::Graphics:
            return m_render_pso.get();
    }
    return m_render_pso.get();
}

RM::Pipeline::Pipeline(Device* device, const ComputePipelineCreateInfo& info)
    : m_device(device), m_type(PipelineType::Compute)
{
    auto shader = Metal::CompileShader(device, info.shader, ShaderType::Compute);
    auto* native_device = static_cast<MTL::Device*>(device->GetNativeHandle());
    auto pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    NS::Error* error = nullptr;
    auto pipeline =
        NS::TransferPtr(native_device->newComputePipelineState(shader.function.get(), &error));
    if (!pipeline) throw MetalError("Failed to create compute pipeline", error);

    m_compute_pso = std::move(pipeline);
    m_compute_dispatch_group = shader.compute_dispatch_group;
}

RM::Pipeline::Pipeline(Device* device, const GraphicsPipelineCreateInfo& info)
    : m_device(device), m_type(PipelineType::Graphics), m_rasterizer_state(info.rasterizer_state)
{
    if (!device) throw std::invalid_argument("Graphics pipeline requires a valid device");
    if (info.rtv_formats.size() > 8)
        throw std::invalid_argument("Metal supports at most eight color attachments");
    if (info.blend_infos.size() > info.rtv_formats.size())
        throw std::invalid_argument("Blend entries exceed the number of color attachments");

    Metal::ToCullMode(info.rasterizer_state.cull_mode);
    Metal::ToWinding(info.rasterizer_state.front_face);
    Metal::ToTriangleFillMode(info.rasterizer_state.fill_mode);

    auto pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    auto* native_device = static_cast<MTL::Device*>(device->GetNativeHandle());
    auto descriptor = NS::TransferPtr(MTL::RenderPipelineDescriptor::alloc()->init());
    descriptor->setInputPrimitiveTopology(Metal::ToPrimitiveTopologyClass(info.polygon_mode));
    descriptor->setRasterizationEnabled(true);
    descriptor->setRasterSampleCount(1);
    descriptor->setAlphaToCoverageEnabled(false);
    descriptor->setAlphaToOneEnabled(false);

    for (size_t i = 0; i < info.rtv_formats.size(); ++i)
    {
        const auto format = Metal::ToPixelFormat(info.rtv_formats[i]);
        if (format == MTL::PixelFormatInvalid || Helpers::IsDepthStencilFormat(info.rtv_formats[i]))
            throw std::invalid_argument("Invalid color attachment format");
        auto* attachment = descriptor->colorAttachments()->object(i);
        attachment->setPixelFormat(format);
        attachment->setWriteMask(MTL::ColorWriteMaskAll);
        attachment->setBlendingEnabled(false);
        if (i >= info.blend_infos.size()) continue;
        const auto& blend = info.blend_infos[i];
        if (blend.logic_op_enable)
            throw std::invalid_argument("Metal does not support color logic operations");
        attachment->setBlendingEnabled(blend.blend_enable);

        if (!blend.blend_enable) continue;
        attachment->setSourceRGBBlendFactor(Metal::ToBlendFactor(blend.src_color_factor));
        attachment->setDestinationRGBBlendFactor(Metal::ToBlendFactor(blend.dst_color_factor));
        attachment->setRgbBlendOperation(Metal::ToBlendOperation(blend.color_blend_op));
        attachment->setSourceAlphaBlendFactor(Metal::ToBlendFactor(blend.src_alpha_factor));
        attachment->setDestinationAlphaBlendFactor(Metal::ToBlendFactor(blend.dst_alpha_factor));
        attachment->setAlphaBlendOperation(Metal::ToBlendOperation(blend.alpha_blend_op));
    }

    descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatInvalid);
    descriptor->setStencilAttachmentPixelFormat(MTL::PixelFormatInvalid);
    if (info.dsv_format)
    {
        const auto format = *info.dsv_format;
        if (!Helpers::IsDepthStencilFormat(format))
            throw std::invalid_argument("Invalid depth/stencil attachment format");
        if (format != Format::Stencil8)
            descriptor->setDepthAttachmentPixelFormat(Metal::ToPixelFormat(format));
        if (format == Format::Stencil8 || format == Format::Depth24_UNorm_Stencil8 ||
            format == Format::Depth32_Float_Stencil8)
            descriptor->setStencilAttachmentPixelFormat(Metal::ToPixelFormat(format));
    }

    const auto& depth = info.depth_stencil_state;
    if (depth.depth_enable && descriptor->depthAttachmentPixelFormat() == MTL::PixelFormatInvalid)
        throw std::invalid_argument("Depth testing requires a depth attachment format");
    if (depth.stencil_enable && descriptor->stencilAttachmentPixelFormat() ==
        MTL::PixelFormatInvalid)
        throw std::invalid_argument("Stencil testing requires a stencil attachment format");
    auto depth_descriptor = NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
    depth_descriptor->setDepthCompareFunction(depth.depth_enable
                                                  ? Metal::ToCompareFunction(depth.depth_test)
                                                  : MTL::CompareFunctionAlways);
    depth_descriptor->setDepthWriteEnabled(depth.depth_enable && depth.depth_write_enable);
    if (depth.stencil_enable)
    {
        const auto make_stencil = [&](const StencilOpState& state)
        {
            auto stencil = NS::TransferPtr(MTL::StencilDescriptor::alloc()->init());
            stencil->setStencilCompareFunction(Metal::ToCompareFunction(state.StencilFunc));
            stencil->setStencilFailureOperation(Metal::ToStencilOperation(state.StencilFailOp));
            stencil->setDepthFailureOperation(Metal::ToStencilOperation(state.StencilDepthFailOp));
            stencil->setDepthStencilPassOperation(Metal::ToStencilOperation(state.StencilPassOp));
            stencil->setReadMask(depth.stencil_read_mask);
            stencil->setWriteMask(depth.stencil_write_mask);
            return stencil;
        };
        auto front = make_stencil(depth.front_stencil_op);
        auto back = make_stencil(depth.back_stencil_op);
        depth_descriptor->setFrontFaceStencil(front.get());
        depth_descriptor->setBackFaceStencil(back.get());
    }

    auto vertex_shader = Metal::CompileShader(device, info.vertex_shader, ShaderType::Vertex);
    auto frag_shader = Metal::CompileShader(device, info.fragment_shader, ShaderType::Fragment);
    auto vertex_descriptor = Metal::CreateVertexDescriptor(info.input_state,
                                                           vertex_shader.reflection.get());
    descriptor->setVertexDescriptor(vertex_descriptor.get());
    descriptor->setVertexFunction(vertex_shader.function.get());
    descriptor->setFragmentFunction(frag_shader.function.get());

    NS::Error* error = nullptr;
    auto pipeline =
        NS::TransferPtr(native_device->newRenderPipelineState(descriptor.get(), &error));
    if (!pipeline) throw MetalError("Failed to create graphics pipeline", error);
    auto depth_state = NS::TransferPtr(native_device->newDepthStencilState(depth_descriptor.get()));
    if (!depth_state) throw std::runtime_error("Failed to create graphics depth/stencil state");

    m_render_pso = std::move(pipeline);
    m_depth_stencil_state = std::move(depth_state);
}