#include "rimera_shader.hpp"

#include "rimera_d3d12.hpp"
#include "rimera_device.hpp"
#include "rimera_helpers.hpp"
#include "directx/d3dx12.h"

void* RM::Pipeline::GetNativeHandle() const
{
    return m_pso.Get();
}

RM::Result<void> RM::Pipeline::Initialize(Device* device, const ComputePipelineCreateInfo& info)
{
    m_device = device;
    m_type = PipelineType::Compute;
    const std::string entry_point(
        info.shader.entry_point.empty() ? "main" : info.shader.entry_point);

    auto result_dxil_blob =
        Helpers::CompileHlsl(info.shader.code, entry_point, ShaderType::Compute);
    if (!result_dxil_blob) return std::unexpected(result_dxil_blob.error());
    ComPtr<IDxcBlob> dxil_blob;
    dxil_blob.Attach(*result_dxil_blob);

    auto* native_device = static_cast<ID3D12Device*>(m_device->GetNativeHandle());
    auto* root_sig = static_cast<ID3D12RootSignature*>(device->GetNativeRootSignature());
    D3D12_SHADER_BYTECODE code{
        .pShaderBytecode = dxil_blob->GetBufferPointer(),
        .BytecodeLength = dxil_blob->GetBufferSize(),
    };
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{
        .pRootSignature = root_sig,
        .CS = code,
        .NodeMask = 0,
        .CachedPSO = {},
        .Flags = D3D12_PIPELINE_STATE_FLAG_NONE,
    };
    if (FAILED(
            native_device->CreateComputePipelineState(&desc, IID_PPV_ARGS(m_pso.GetAddressOf()))))
        return std::unexpected("Failed to create compute pipeline");
    return {};
}

RM::Result<void> RM::Pipeline::Initialize(Device* device, const GraphicsPipelineCreateInfo& info)
{
    m_device = device;
    m_type = PipelineType::Graphics;
    if (info.rtv_formats.size() > 8 || info.blend_infos.size() > info.rtv_formats.size())
        return std::unexpected("Invalid graphics attachment count");
    auto* native_device = static_cast<ID3D12Device*>(m_device->GetNativeHandle());
    auto* root_sig = static_cast<ID3D12RootSignature*>(device->GetNativeRootSignature());

    const std::string vertex_entry_point(info.vertex_shader.entry_point.empty()
                                             ? "main"
                                             : info.vertex_shader.entry_point);
    auto result_vertex_dxil_blob =
        Helpers::CompileHlsl(info.vertex_shader.code, vertex_entry_point, ShaderType::Vertex);
    if (!result_vertex_dxil_blob) return std::unexpected(result_vertex_dxil_blob.error());
    ComPtr<IDxcBlob> vertex_dxil_blob;
    vertex_dxil_blob.Attach(*result_vertex_dxil_blob);

    const std::string frag_entry_point(info.fragment_shader.entry_point.empty()
                                         ? "main"
                                         : info.fragment_shader.entry_point);

    auto result_frag_dxil_blob =
        Helpers::CompileHlsl(info.fragment_shader.code, frag_entry_point, ShaderType::Fragment);
    if (!result_frag_dxil_blob) return std::unexpected(result_frag_dxil_blob.error());
    ComPtr<IDxcBlob> frag_dxil_blob;
    frag_dxil_blob.Attach(*result_frag_dxil_blob);

    D3D12_SHADER_BYTECODE vertex_code{
        .pShaderBytecode = vertex_dxil_blob->GetBufferPointer(),
        .BytecodeLength = vertex_dxil_blob->GetBufferSize(),
    };
    D3D12_SHADER_BYTECODE frag_code{
        .pShaderBytecode = frag_dxil_blob->GetBufferPointer(),
        .BytecodeLength = frag_dxil_blob->GetBufferSize(),
    };

    auto result_elements =
        CreateInputLayout(info.input_state.bindings, info.input_state.attributes);
    if (!result_elements) return std::unexpected(result_elements.error());
    auto elements = std::move(*result_elements);

    D3D12_BLEND_DESC blend_desc{
        .AlphaToCoverageEnable = false,
        .IndependentBlendEnable = true,
    };
    for (size_t i = 0; i < info.rtv_formats.size(); i++)
    {
        auto rt_info = i < info.blend_infos.size() ? info.blend_infos[i] : BlendInfo{};
        blend_desc.RenderTarget[i] = {
            .BlendEnable = rt_info.blend_enable,
            .LogicOpEnable = rt_info.logic_op_enable,
            .SrcBlend = D3D12::GetBlendFactor(rt_info.src_color_factor),
            .DestBlend = D3D12::GetBlendFactor(rt_info.dst_color_factor),
            .BlendOp = D3D12::GetBlendOp(rt_info.color_blend_op),
            .SrcBlendAlpha = D3D12::GetBlendFactor(rt_info.src_alpha_factor),
            .DestBlendAlpha = D3D12::GetBlendFactor(rt_info.dst_alpha_factor),
            .BlendOpAlpha = D3D12::GetBlendOp(rt_info.alpha_blend_op),
            .LogicOp = D3D12::GetLogicOp(rt_info.logic_op),
            .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
        };
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{
        .pRootSignature = root_sig,
        .VS = vertex_code,
        .PS = frag_code,
        .BlendState = blend_desc,
        .SampleMask = D3D12_DEFAULT_SAMPLE_MASK,
        .RasterizerState =
            {
                .FillMode = D3D12::GetFillMode(info.rasterizer_state.fill_mode),
                .CullMode = D3D12::GetCullMode(info.rasterizer_state.cull_mode),
                .FrontCounterClockwise = D3D12::GetFrontFace(info.rasterizer_state.front_face),
                .DepthBias = info.rasterizer_state.depth_bias,
                .DepthBiasClamp = info.rasterizer_state.depth_bias_clamp,
                .SlopeScaledDepthBias = info.rasterizer_state.slope_scaled_depth_bias,
                .DepthClipEnable = info.rasterizer_state.depth_clip_enable,
                .MultisampleEnable = info.rasterizer_state.multisample_enable,
                .AntialiasedLineEnable = false,
                .ForcedSampleCount = 0,
                .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
            },
        .DepthStencilState =
            {
                .DepthEnable = info.depth_stencil_state.depth_enable,
                .DepthWriteMask =
                    D3D12::GetDepthWriteMask(info.depth_stencil_state.depth_write_enable),
                .DepthFunc = D3D12::GetComparisonFunc(info.depth_stencil_state.depth_test),
                .StencilEnable = info.depth_stencil_state.stencil_enable,
                .StencilReadMask = info.depth_stencil_state.stencil_read_mask,
                .StencilWriteMask = info.depth_stencil_state.stencil_write_mask,
                .FrontFace = D3D12::GetStencilOpDesc(info.depth_stencil_state.front_stencil_op),
                .BackFace = D3D12::GetStencilOpDesc(info.depth_stencil_state.back_stencil_op),
            },
        .InputLayout =
            {
                .pInputElementDescs = elements.data(),
                .NumElements = static_cast<uint32_t>(elements.size()),
            },
        .PrimitiveTopologyType = D3D12::GetPolygonMode(info.polygon_mode),
        .NumRenderTargets = static_cast<uint32_t>(info.rtv_formats.size()),
        .DSVFormat = info.dsv_format ? D3D12::GetFormat(*info.dsv_format) : DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .NodeMask = 0,
        .CachedPSO = {},
        .Flags = D3D12_PIPELINE_STATE_FLAG_NONE};
    for (size_t i = 0; i < info.rtv_formats.size(); i++)
    {
        desc.RTVFormats[i] = D3D12::GetFormat(info.rtv_formats[i]);
    }
    if (FAILED(
            native_device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(m_pso.GetAddressOf()))))
        return std::unexpected("Failed to create graphics pipeline");
    return {};
}

RM::Result<void> RM::Pipeline::Initialize(Device* device, const MeshGraphicsPipelineCreateInfo& info)
{
    if (!device) return std::unexpected("Mesh graphics pipeline requires a valid device");
    if (info.rtv_formats.size() > 8 || info.blend_infos.size() > info.rtv_formats.size())
        return std::unexpected("Invalid mesh graphics attachment count");
    m_device = device;
    m_type = PipelineType::MeshGraphics;

    ComPtr<IDxcBlob> ampl_blob;
    if (!info.ampl_shader.code.empty())
    {
        auto result = D3D12::CompileShader(info.ampl_shader, ShaderType::Amplification);
        if (!result) return std::unexpected(result.error());
        ampl_blob = std::move(*result);
    }
    auto mesh_result = D3D12::CompileShader(info.mesh_shader, ShaderType::Mesh);
    if (!mesh_result) return std::unexpected(mesh_result.error());
    auto mesh_blob = std::move(*mesh_result);
    auto frag_result = D3D12::CompileShader(info.fragment_shader, ShaderType::Fragment);
    if (!frag_result) return std::unexpected(frag_result.error());
    auto frag_blob = std::move(*frag_result);

    D3D12_BLEND_DESC blend_desc{
        .AlphaToCoverageEnable = false,
        .IndependentBlendEnable = true,
    };
    for (size_t i = 0; i < info.rtv_formats.size(); ++i)
    {
        const auto blend = i < info.blend_infos.size() ? info.blend_infos[i] : BlendInfo{};
        blend_desc.RenderTarget[i] = {
            .BlendEnable = blend.blend_enable,
            .LogicOpEnable = blend.logic_op_enable,
            .SrcBlend = D3D12::GetBlendFactor(blend.src_color_factor),
            .DestBlend = D3D12::GetBlendFactor(blend.dst_color_factor),
            .BlendOp = D3D12::GetBlendOp(blend.color_blend_op),
            .SrcBlendAlpha = D3D12::GetBlendFactor(blend.src_alpha_factor),
            .DestBlendAlpha = D3D12::GetBlendFactor(blend.dst_alpha_factor),
            .BlendOpAlpha = D3D12::GetBlendOp(blend.alpha_blend_op),
            .LogicOp = D3D12::GetLogicOp(blend.logic_op),
            .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
        };
    }

    const auto& raster = info.rasterizer_state;
    const D3D12_RASTERIZER_DESC raster_desc{
        .FillMode = D3D12::GetFillMode(raster.fill_mode),
        .CullMode = D3D12::GetCullMode(raster.cull_mode),
        .FrontCounterClockwise = D3D12::GetFrontFace(raster.front_face),
        .DepthBias = raster.depth_bias,
        .DepthBiasClamp = raster.depth_bias_clamp,
        .SlopeScaledDepthBias = raster.slope_scaled_depth_bias,
        .DepthClipEnable = raster.depth_clip_enable,
        .MultisampleEnable = raster.multisample_enable,
        .AntialiasedLineEnable = false,
        .ForcedSampleCount = 0,
        .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
    };
    const auto& depth = info.depth_stencil_state;
    const D3D12_DEPTH_STENCIL_DESC depth_desc{
        .DepthEnable = depth.depth_enable,
        .DepthWriteMask = D3D12::GetDepthWriteMask(depth.depth_write_enable),
        .DepthFunc = D3D12::GetComparisonFunc(depth.depth_test),
        .StencilEnable = depth.stencil_enable,
        .StencilReadMask = depth.stencil_read_mask,
        .StencilWriteMask = depth.stencil_write_mask,
        .FrontFace = D3D12::GetStencilOpDesc(depth.front_stencil_op),
        .BackFace = D3D12::GetStencilOpDesc(depth.back_stencil_op),
    };
    D3DX12_MESH_SHADER_PIPELINE_STATE_DESC mesh_desc{
        .pRootSignature = static_cast<ID3D12RootSignature*>(device->GetNativeRootSignature()),
        .AS = D3D12::GetShaderBytecode(ampl_blob),
        .MS = D3D12::GetShaderBytecode(mesh_blob),
        .PS = D3D12::GetShaderBytecode(frag_blob),
        .BlendState = blend_desc,
        .SampleMask = D3D12_DEFAULT_SAMPLE_MASK,
        .RasterizerState = raster_desc,
        .DepthStencilState = depth_desc,
        .PrimitiveTopologyType = D3D12::GetPolygonMode(info.polygon_mode),
        .NumRenderTargets = static_cast<UINT>(info.rtv_formats.size()),
        .DSVFormat = info.dsv_format ? D3D12::GetFormat(*info.dsv_format)
                                      : DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Flags = D3D12_PIPELINE_STATE_FLAG_NONE,
    };
    for (size_t i = 0; i < info.rtv_formats.size(); ++i)
        mesh_desc.RTVFormats[i] = D3D12::GetFormat(info.rtv_formats[i]);

    CD3DX12_PIPELINE_MESH_STATE_STREAM stream(mesh_desc);

    auto* native_device = static_cast<ID3D12Device*>(device->GetNativeHandle());
    ComPtr<ID3D12Device2> device2;
    if (FAILED(native_device->QueryInterface(IID_PPV_ARGS(device2.GetAddressOf()))))
        return std::unexpected("D3D12 device does not support mesh pipeline creation");
    D3D12_PIPELINE_STATE_STREAM_DESC desc{
        .SizeInBytes = sizeof(stream),
        .pPipelineStateSubobjectStream = &stream,
    };
    if (FAILED(device2->CreatePipelineState(&desc, IID_PPV_ARGS(m_pso.GetAddressOf()))))
        return std::unexpected("Failed to create mesh graphics pipeline");
    return {};
}

RM::Result<std::vector<D3D12_INPUT_ELEMENT_DESC>> RM::Pipeline::CreateInputLayout(
    std::span<const VertexBinding> bindings,
    std::span<const VertexAttribute> attributes)
{
    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
    elements.reserve(attributes.size());
    for (const auto& attribute : attributes)
    {
        const auto binding = std::ranges::find_if(
            bindings,
            [&](const VertexBinding& b)
            {
                return b.binding == attribute.binding;
            }
            );

        if (binding == bindings.end()) return std::unexpected("Invalid vertex binding");

        D3D12_INPUT_ELEMENT_DESC desc{
            .SemanticName = attribute.semantic.data(),
            .SemanticIndex = 0,
            .Format = D3D12::GetFormat(attribute.format),
            .InputSlot = attribute.binding,
            .AlignedByteOffset = attribute.offset,
            .InputSlotClass = D3D12::GetInputClassification(binding->input_rate),
            .InstanceDataStepRate = binding->input_rate == VertexInputRate::Instance ? 1u : 0u
            // TODO: Allow more instances later
        };
        elements.push_back(desc);
    }
    return elements;
}
