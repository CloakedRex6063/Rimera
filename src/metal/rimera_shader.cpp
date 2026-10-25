#include "rimera_shader.hpp"
#include "rimera_metal.hpp"
#include "rimera_helpers.hpp"
#include "string"

void* RM::Pipeline::GetNativeHandle() const
{
    switch (m_type)
    {
        case PipelineType::Compute:
            return m_compute_pso.get();
        case PipelineType::Graphics:
        case PipelineType::MeshGraphics:
            return m_render_pso.get();
    }
    return m_render_pso.get();
}

RM::Result<void> RM::Pipeline::Initialize(Device* device, const ComputePipelineCreateInfo& info)
{
    m_device = device;
    m_type = PipelineType::Compute;
    auto result_shader = Metal::CompileShader(device, info.shader, ShaderType::Compute);
    if (!result_shader) return std::unexpected(result_shader.error());
    auto shader = std::move(*result_shader);
    auto* native_device = static_cast<MTL::Device*>(device->GetNativeHandle());
    auto pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    NS::Error* error = nullptr;
    auto pipeline =
        NS::TransferPtr(native_device->newComputePipelineState(shader.function.get(), &error));
    if (!pipeline) return std::unexpected(Metal::MetalError("Failed to create compute pipeline", error));

    m_compute_pso = std::move(pipeline);
    m_compute_dispatch_group = shader.compute_dispatch_group;
    return {};
}

RM::Result<void> RM::Pipeline::Initialize(Device* device, const GraphicsPipelineCreateInfo& info)
{
    m_device = device;
    m_type = PipelineType::Graphics;
    m_rasterizer_state = info.rasterizer_state;
    if (!device) return std::unexpected("Graphics pipeline requires a valid device");
    if (info.rtv_formats.size() > 8)
        return std::unexpected("Metal supports at most eight color attachments");
    if (info.blend_infos.size() > info.rtv_formats.size())
        return std::unexpected("Blend entries exceed the number of color attachments");

    auto cull_mode_result = Metal::ToCullMode(info.rasterizer_state.cull_mode);
    if (!cull_mode_result) return std::unexpected(cull_mode_result.error());
    m_cull_mode = *cull_mode_result;

    auto winding_result = Metal::ToWinding(info.rasterizer_state.front_face);
    if (!winding_result) return std::unexpected(winding_result.error());
    m_winding = *winding_result;

    auto fill_mode_result = Metal::ToTriangleFillMode(info.rasterizer_state.fill_mode);
    if (!fill_mode_result) return std::unexpected(fill_mode_result.error());
    m_fill_mode = *fill_mode_result;

    auto pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    auto* native_device = static_cast<MTL::Device*>(device->GetNativeHandle());
    auto descriptor = NS::TransferPtr(MTL::RenderPipelineDescriptor::alloc()->init());
    auto primitive_topology_result = Metal::ToPrimitiveTopologyClass(info.polygon_mode);
    if (!primitive_topology_result) return std::unexpected(primitive_topology_result.error());
    descriptor->setInputPrimitiveTopology(*primitive_topology_result);
    descriptor->setRasterizationEnabled(true);
    descriptor->setRasterSampleCount(1);
    descriptor->setAlphaToCoverageEnabled(false);
    descriptor->setAlphaToOneEnabled(false);

    for (size_t i = 0; i < info.rtv_formats.size(); ++i)
    {
        const auto format = Metal::ToPixelFormat(info.rtv_formats[i]);
        if (format == MTL::PixelFormatInvalid || Helpers::IsDepthStencilFormat(info.rtv_formats[i]))
            return std::unexpected("Invalid color attachment format");
        auto* attachment = descriptor->colorAttachments()->object(i);
        attachment->setPixelFormat(format);
        attachment->setWriteMask(MTL::ColorWriteMaskAll);
        attachment->setBlendingEnabled(false);
        if (i >= info.blend_infos.size()) continue;
        const auto& blend = info.blend_infos[i];
        if (blend.logic_op_enable)
            return std::unexpected("Metal does not support color logic operations");
        attachment->setBlendingEnabled(blend.blend_enable);

        if (!blend.blend_enable) continue;
        auto src_color_factor_result = Metal::ToBlendFactor(blend.src_color_factor);
        if (!src_color_factor_result) return std::unexpected(src_color_factor_result.error());
        attachment->setSourceRGBBlendFactor(*src_color_factor_result);
        auto dst_color_factor_result = Metal::ToBlendFactor(blend.dst_color_factor);
        if (!dst_color_factor_result) return std::unexpected(dst_color_factor_result.error());
        attachment->setDestinationRGBBlendFactor(*dst_color_factor_result);
        auto color_blend_op_result = Metal::ToBlendOperation(blend.color_blend_op);
        if (!color_blend_op_result) return std::unexpected(color_blend_op_result.error());
        attachment->setRgbBlendOperation(*color_blend_op_result);
        auto src_alpha_factor_result = Metal::ToBlendFactor(blend.src_alpha_factor);
        if (!src_alpha_factor_result) return std::unexpected(src_alpha_factor_result.error());
        attachment->setSourceAlphaBlendFactor(*src_alpha_factor_result);
        auto dst_alpha_factor_result = Metal::ToBlendFactor(blend.dst_alpha_factor);
        if (!dst_alpha_factor_result) return std::unexpected(dst_alpha_factor_result.error());
        attachment->setDestinationAlphaBlendFactor(*dst_alpha_factor_result);
        auto alpha_blend_op_result = Metal::ToBlendOperation(blend.alpha_blend_op);
        if (!alpha_blend_op_result) return std::unexpected(alpha_blend_op_result.error());
        attachment->setAlphaBlendOperation(*alpha_blend_op_result);
    }

    descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatInvalid);
    descriptor->setStencilAttachmentPixelFormat(MTL::PixelFormatInvalid);
    if (info.dsv_format)
    {
        const auto format = *info.dsv_format;
        if (!Helpers::IsDepthStencilFormat(format))
            return std::unexpected("Invalid depth/stencil attachment format");
        if (format != Format::Stencil8)
            descriptor->setDepthAttachmentPixelFormat(Metal::ToPixelFormat(format));
        if (format == Format::Stencil8 || format == Format::Depth24_UNorm_Stencil8 ||
            format == Format::Depth32_Float_Stencil8)
            descriptor->setStencilAttachmentPixelFormat(Metal::ToPixelFormat(format));
    }

    const auto& depth = info.depth_stencil_state;
    if (depth.depth_enable && descriptor->depthAttachmentPixelFormat() == MTL::PixelFormatInvalid)
        return std::unexpected("Depth testing requires a depth attachment format");
    if (depth.stencil_enable && descriptor->stencilAttachmentPixelFormat() ==
        MTL::PixelFormatInvalid)
        return std::unexpected("Stencil testing requires a stencil attachment format");
    auto depth_descriptor = NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
    depth_descriptor->setDepthCompareFunction(depth.depth_enable
                                                  ? Metal::ToCompareFunction(depth.depth_test)
                                                  : MTL::CompareFunctionAlways);
    depth_descriptor->setDepthWriteEnabled(depth.depth_enable && depth.depth_write_enable);
    if (depth.stencil_enable)
    {
        auto result_front = Metal::CreateStencilDescriptor(depth.front_stencil_op, depth);
        if (!result_front) return std::unexpected(result_front.error());
        auto front = std::move(*result_front);
        auto result_back = Metal::CreateStencilDescriptor(depth.back_stencil_op, depth);
        if (!result_back) return std::unexpected(result_back.error());
        auto back = std::move(*result_back);
        depth_descriptor->setFrontFaceStencil(front.get());
        depth_descriptor->setBackFaceStencil(back.get());
    }

    auto result_vertex_shader =
        Metal::CompileShader(device, info.vertex_shader, ShaderType::Vertex);
    if (!result_vertex_shader) return std::unexpected(result_vertex_shader.error());
    auto vertex_shader = std::move(*result_vertex_shader);
    auto result_frag_shader =
        Metal::CompileShader(device, info.fragment_shader, ShaderType::Fragment);
    if (!result_frag_shader) return std::unexpected(result_frag_shader.error());
    auto frag_shader = std::move(*result_frag_shader);
    auto result_vertex_descriptor =
        Metal::CreateVertexDescriptor(info.input_state, vertex_shader.reflection.get());
    if (!result_vertex_descriptor) return std::unexpected(result_vertex_descriptor.error());
    auto vertex_descriptor = std::move(*result_vertex_descriptor);
    descriptor->setVertexDescriptor(vertex_descriptor.get());
    descriptor->setVertexFunction(vertex_shader.function.get());
    descriptor->setFragmentFunction(frag_shader.function.get());

    NS::Error* error = nullptr;
    auto pipeline =
        NS::TransferPtr(native_device->newRenderPipelineState(descriptor.get(), &error));
    if (!pipeline) return std::unexpected(Metal::MetalError("Failed to create graphics pipeline", error));
    auto depth_state = NS::TransferPtr(native_device->newDepthStencilState(depth_descriptor.get()));
    if (!depth_state) return std::unexpected("Failed to create graphics depth/stencil state");

    m_render_pso = std::move(pipeline);
    m_depth_stencil_state = std::move(depth_state);
    return {};
}

RM::Result<void> RM::Pipeline::Initialize(Device* device, const MeshGraphicsPipelineCreateInfo& info)
{
    m_device = device;
    m_type = PipelineType::MeshGraphics;
    m_rasterizer_state = info.rasterizer_state;
    if (!device) return std::unexpected("Mesh graphics pipeline requires a valid device");
    if (info.rtv_formats.size() > 8)
        return std::unexpected("Metal supports at most eight color attachments");
    if (info.blend_infos.size() > info.rtv_formats.size())
        return std::unexpected("Blend entries exceed the number of color attachments");

    auto cull_mode_result = Metal::ToCullMode(info.rasterizer_state.cull_mode);
    if (!cull_mode_result) return std::unexpected(cull_mode_result.error());
    m_cull_mode = *cull_mode_result;

    auto winding_result = Metal::ToWinding(info.rasterizer_state.front_face);
    if (!winding_result) return std::unexpected(winding_result.error());
    m_winding = *winding_result;

    auto fill_mode_result = Metal::ToTriangleFillMode(info.rasterizer_state.fill_mode);
    if (!fill_mode_result) return std::unexpected(fill_mode_result.error());
    m_fill_mode = *fill_mode_result;

    auto pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    auto* native_device = static_cast<MTL::Device*>(device->GetNativeHandle());
    auto descriptor = NS::TransferPtr(MTL::MeshRenderPipelineDescriptor::alloc()->init());
    descriptor->setRasterizationEnabled(true);
    descriptor->setRasterSampleCount(1);
    descriptor->setAlphaToCoverageEnabled(false);
    descriptor->setAlphaToOneEnabled(false);

    for (size_t i = 0; i < info.rtv_formats.size(); ++i)
    {
        const auto format = Metal::ToPixelFormat(info.rtv_formats[i]);
        if (format == MTL::PixelFormatInvalid || Helpers::IsDepthStencilFormat(info.rtv_formats[i]))
            return std::unexpected("Invalid color attachment format");
        auto* attachment = descriptor->colorAttachments()->object(i);
        attachment->setPixelFormat(format);
        attachment->setWriteMask(MTL::ColorWriteMaskAll);
        attachment->setBlendingEnabled(false);
        if (i >= info.blend_infos.size()) continue;
        const auto& blend = info.blend_infos[i];
        if (blend.logic_op_enable)
            return std::unexpected("Metal does not support color logic operations");
        attachment->setBlendingEnabled(blend.blend_enable);

        if (!blend.blend_enable) continue;
        auto src_color_factor_result = Metal::ToBlendFactor(blend.src_color_factor);
        if (!src_color_factor_result) return std::unexpected(src_color_factor_result.error());
        attachment->setSourceRGBBlendFactor(*src_color_factor_result);
        auto dst_color_factor_result = Metal::ToBlendFactor(blend.dst_color_factor);
        if (!dst_color_factor_result) return std::unexpected(dst_color_factor_result.error());
        attachment->setDestinationRGBBlendFactor(*dst_color_factor_result);
        auto color_blend_op_result = Metal::ToBlendOperation(blend.color_blend_op);
        if (!color_blend_op_result) return std::unexpected(color_blend_op_result.error());
        attachment->setRgbBlendOperation(*color_blend_op_result);
        auto src_alpha_factor_result = Metal::ToBlendFactor(blend.src_alpha_factor);
        if (!src_alpha_factor_result) return std::unexpected(src_alpha_factor_result.error());
        attachment->setSourceAlphaBlendFactor(*src_alpha_factor_result);
        auto dst_alpha_factor_result = Metal::ToBlendFactor(blend.dst_alpha_factor);
        if (!dst_alpha_factor_result) return std::unexpected(dst_alpha_factor_result.error());
        attachment->setDestinationAlphaBlendFactor(*dst_alpha_factor_result);
        auto alpha_blend_op_result = Metal::ToBlendOperation(blend.alpha_blend_op);
        if (!alpha_blend_op_result) return std::unexpected(alpha_blend_op_result.error());
        attachment->setAlphaBlendOperation(*alpha_blend_op_result);
    }

    descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatInvalid);
    descriptor->setStencilAttachmentPixelFormat(MTL::PixelFormatInvalid);
    if (info.dsv_format)
    {
        const auto format = *info.dsv_format;
        if (!Helpers::IsDepthStencilFormat(format))
            return std::unexpected("Invalid depth/stencil attachment format");
        if (format != Format::Stencil8)
            descriptor->setDepthAttachmentPixelFormat(Metal::ToPixelFormat(format));
        if (format == Format::Stencil8 || format == Format::Depth24_UNorm_Stencil8 ||
            format == Format::Depth32_Float_Stencil8)
            descriptor->setStencilAttachmentPixelFormat(Metal::ToPixelFormat(format));
    }

    const auto& depth = info.depth_stencil_state;
    if (depth.depth_enable && descriptor->depthAttachmentPixelFormat() == MTL::PixelFormatInvalid)
        return std::unexpected("Depth testing requires a depth attachment format");
    if (depth.stencil_enable && descriptor->stencilAttachmentPixelFormat() ==
        MTL::PixelFormatInvalid)
        return std::unexpected("Stencil testing requires a stencil attachment format");
    auto depth_descriptor = NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
    depth_descriptor->setDepthCompareFunction(depth.depth_enable
                                                  ? Metal::ToCompareFunction(depth.depth_test)
                                                  : MTL::CompareFunctionAlways);
    depth_descriptor->setDepthWriteEnabled(depth.depth_enable && depth.depth_write_enable);
    if (depth.stencil_enable)
    {
        auto result_front = Metal::CreateStencilDescriptor(depth.front_stencil_op, depth);
        if (!result_front) return std::unexpected(result_front.error());
        auto front = std::move(*result_front);
        auto result_back = Metal::CreateStencilDescriptor(depth.back_stencil_op, depth);
        if (!result_back) return std::unexpected(result_back.error());
        auto back = std::move(*result_back);
        depth_descriptor->setFrontFaceStencil(front.get());
        depth_descriptor->setBackFaceStencil(back.get());
    }

    auto result_mesh_shader = Metal::CompileShader(device, info.mesh_shader, ShaderType::Mesh);
    if (!result_mesh_shader) return std::unexpected(result_mesh_shader.error());
    auto mesh_shader = std::move(*result_mesh_shader);
    m_mesh_dispatch_group = mesh_shader.compute_dispatch_group;
    descriptor->setMeshFunction(mesh_shader.function.get());
    descriptor->setRequiredThreadsPerMeshThreadgroup(
        MTL::Size(m_mesh_dispatch_group.x, m_mesh_dispatch_group.y, m_mesh_dispatch_group.z));

    m_ampl_dispatch_group = {.x = 1, .y = 1, .z = 1};
    if (info.ampl_shader)
    {
        auto result_ampl_shader =
            Metal::CompileShader(device, info.ampl_shader, ShaderType::Amplification);
        if (!result_ampl_shader) return std::unexpected(result_ampl_shader.error());
        auto ampl_shader = std::move(*result_ampl_shader);
        m_ampl_dispatch_group = ampl_shader.compute_dispatch_group;
        descriptor->setObjectFunction(ampl_shader.function.get());
        descriptor->setRequiredThreadsPerObjectThreadgroup(
            MTL::Size(m_ampl_dispatch_group.x, m_ampl_dispatch_group.y, m_ampl_dispatch_group.z));
        descriptor->setPayloadMemoryLength(ampl_shader.payload_size);
    }

    auto result_frag_shader =
        Metal::CompileShader(device, info.fragment_shader, ShaderType::Fragment);
    if (!result_frag_shader) return std::unexpected(result_frag_shader.error());
    auto frag_shader = std::move(*result_frag_shader);
    descriptor->setFragmentFunction(frag_shader.function.get());

    NS::Error* error = nullptr;
    auto pipeline =
        NS::TransferPtr(native_device->newRenderPipelineState(descriptor.get(), MTL::PipelineOptionNone, nullptr, &error));
    if (!pipeline) return std::unexpected(Metal::MetalError("Failed to create mesh graphics pipeline", error));
    auto depth_state = NS::TransferPtr(native_device->newDepthStencilState(depth_descriptor.get()));
    if (!depth_state) return std::unexpected("Failed to create mesh graphics depth/stencil state");

    m_render_pso = std::move(pipeline);
    m_depth_stencil_state = std::move(depth_state);
    return {};
}
