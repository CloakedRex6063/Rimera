#include "rimera_shader.hpp"

#include "rimera_d3d12.hpp"
#include "rimera_device.hpp"
#include "rimera_helpers.hpp"

void* RM::Pipeline::GetNativeHandle() const
{
    return m_pso.Get();
}

RM::Pipeline::Pipeline(Device* device, const ComputePipelineCreateInfo& info) : m_device(device),
    m_type(PipelineType::Compute)
{
    const std::string entry_point(
        info.shader.entry_point.empty() ? "main" : info.shader.entry_point);

    auto dxil_blob = Helpers::CompileHlsl(info.shader.code, entry_point, ShaderType::Compute);

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
        .CachedPSO = nullptr,
        .Flags = D3D12_PIPELINE_STATE_FLAG_NONE,
    };
    native_device->CreateComputePipelineState(&desc, IID_PPV_ARGS(m_pso.GetAddressOf()));
}

RM::Pipeline::Pipeline(Device* device, const GraphicsPipelineCreateInfo& info) : m_device(device), m_type(PipelineType::Graphics)
{
    auto* native_device = static_cast<ID3D12Device*>(m_device->GetNativeHandle());
    auto* root_sig = static_cast<ID3D12RootSignature*>(device->GetNativeRootSignature());

    const std::string vertex_entry_point(info.vertex_shader.entry_point.empty()
                                             ? "main"
                                             : info.vertex_shader.entry_point);
    auto vertex_dxil_blob = Helpers::CompileHlsl(info.vertex_shader.code,
                                                 vertex_entry_point,
                                                 ShaderType::Vertex);
    
    const std::string frag_entry_point(info.fragment_shader.entry_point.empty()
                                         ? "main"
                                         : info.fragment_shader.entry_point);
    
    auto frag_dxil_blob = Helpers::CompileHlsl(info.fragment_shader.code,
                                               frag_entry_point,
                                               ShaderType::Fragment);

    D3D12_SHADER_BYTECODE vertex_code{
        .pShaderBytecode = vertex_dxil_blob->GetBufferPointer(),
        .BytecodeLength = vertex_dxil_blob->GetBufferSize(),
    };
    D3D12_SHADER_BYTECODE frag_code{
        .pShaderBytecode = frag_dxil_blob->GetBufferPointer(),
        .BytecodeLength = frag_dxil_blob->GetBufferSize(),
    };

    auto elements = CreateInputLayout(info.input_state.bindings, info.input_state.attributes);

    D3D12_BLEND_DESC blend_desc{
        .AlphaToCoverageEnable = false,
        .IndependentBlendEnable = true,
    };
    for (size_t i = 0; i < info.rtv_formats.size(); i++)
    {
        auto rt_info = info.blend_infos[i];
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
        .RasterizerState = {
            .FillMode = D3D12::GetFillMode(info.rasterizer_state.fill_mode),
            .CullMode = D3D12::GetCullMode(info.rasterizer_state.cull_mode),
            .FrontCounterClockwise = D3D12::GetFrontFace(info.rasterizer_state.front_face),
            .DepthBias = info.rasterizer_state.depth_bias,
            .DepthBiasClamp = info.rasterizer_state.depth_bias_clamp,
            .SlopeScaledDepthBias = info.rasterizer_state.slope_scaled_depth_bias,
            .DepthClipEnable = info.rasterizer_state.depth_clip_enable,
            .MultisampleEnable = info.rasterizer_state.multisample_enable,
            .AntialiasedLineEnable = info.rasterizer_state.antialiased_line_enable,
            .ForcedSampleCount = info.rasterizer_state.sample_count,
            .ConservativeRaster = D3D12::GetConservativeRaster(
                info.rasterizer_state.conservative_raster),
        },
        .DepthStencilState = {
            .DepthEnable = info.depth_stencil_state.depth_enable,
            .DepthWriteMask = D3D12::GetDepthWriteMask(info.depth_stencil_state.depth_write_enable),
            .DepthFunc = D3D12::GetComparisonFunc(info.depth_stencil_state.depth_test),
            .StencilEnable = info.depth_stencil_state.stencil_enable,
            .StencilReadMask = info.depth_stencil_state.stencil_read_mask,
            .StencilWriteMask = info.depth_stencil_state.stencil_write_mask,
            .FrontFace = D3D12::GetStencilOpDesc(info.depth_stencil_state.front_stencil_op),
            .BackFace = D3D12::GetStencilOpDesc(info.depth_stencil_state.back_stencil_op),
        },
        .InputLayout = {
            .pInputElementDescs = elements.data(),
            .NumElements = static_cast<uint32_t>(elements.size()),
        },
        .PrimitiveTopologyType = D3D12::GetPolygonMode(info.polygon_mode),
        .NumRenderTargets = static_cast<uint32_t>(info.rtv_formats.size()),
        .DSVFormat = info.dsv_format
                         ? D3D12::GetFormat(info.dsv_format.value())
                         : DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .NodeMask = 0,
        .CachedPSO = nullptr,
        .Flags = D3D12_PIPELINE_STATE_FLAG_NONE
    };
    for (size_t i = 0; i < info.rtv_formats.size(); i++)
    {
        desc.RTVFormats[i] = D3D12::GetFormat(info.rtv_formats[i]);
    }
    native_device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(m_pso.GetAddressOf()));
}

RM::Pipeline::Pipeline(Device* device, const MeshGraphicsPipelineCreateInfo& info)
{
}

std::vector<D3D12_INPUT_ELEMENT_DESC> RM::Pipeline::CreateInputLayout(
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

        if (binding == bindings.end())
            throw std::runtime_error("Invalid vertex binding");

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