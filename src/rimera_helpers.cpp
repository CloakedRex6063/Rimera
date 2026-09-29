#include "rimera_helpers.hpp"
#include "rimera_device.hpp"
#include "dxcapi.h"
#include "rimera_builders.hpp"

RM::Pipeline* RM::Helpers::CreateBlitShader(Device* device)
{
    constexpr auto blit_shader = R"(
        struct BlitConstants
        {
            uint src_index;
            uint src_mip;
            uint sampler_index;
            uint padding;
            float4 uv_rect;
        };
        ConstantBuffer<BlitConstants> PassConstants : register(b1);

        struct VSOut
        {
            float4 pos : SV_Position;
            float2 uv : TEXCOORD0;
        };

        VSOut VSMain(uint vid : SV_VertexID)
        {
            VSOut o;
            float2 t = float2((vid << 1) & 2, vid & 2);
            o.pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
            o.uv = PassConstants.uv_rect.xy + t * PassConstants.uv_rect.zw;
            return o;
        }

        float4 PSMain(VSOut i) : SV_Target
        {
            Texture2D<float4> src = ResourceDescriptorHeap[PassConstants.src_index];
            SamplerState s = SamplerDescriptorHeap[PassConstants.sampler_index];
            return src.SampleLevel(s, i.uv, PassConstants.src_mip);
        }

        float PSDepth(VSOut i) : SV_Depth
        {
            Texture2D<float> src = ResourceDescriptorHeap[PassConstants.src_index];
            SamplerState s = SamplerDescriptorHeap[PassConstants.sampler_index];
            return src.SampleLevel(s, i.uv, 0);
        }
    )";

    auto vertex_shader = ShaderBuilder(blit_shader)
                         .EntryPoint("VSMain")
                         .Build();

    auto color_shader = ShaderBuilder(blit_shader)
                        .EntryPoint("PSMain")
                        .Build();

    auto blit_info = GraphicsPipelineBuilder(vertex_shader, color_shader)
                     .AddRenderTarget(Format::RGBA8_UNorm)
                     .AddBlendInfo(
                         BlendInfoBuilder()
                         .Disable()
                         .Build())
                     .DisableDepth()
                     .DisableStencil()
                     .Cull(CullMode::None)
                     .Fill(FillMode::Solid)
                     .Primitive(PolygonMode::Triangle)
                     .SampleCount(1)
                     .Build();
    return device->CreatePipeline(blit_info);
}

void RM::Helpers::CheckDxc(HRESULT result, const char* operation)
{
    if (FAILED(result))
        throw std::runtime_error(std::string(operation) + " (HRESULT " +
                                 std::to_string(result) + ")");
}

static const wchar_t* GetTargetProfile(RM::ShaderType type)
{
    switch (type)
    {
        case RM::ShaderType::Compute:
            return L"cs_6_8";
        case RM::ShaderType::Vertex:
            return L"vs_6_8";
        case RM::ShaderType::Fragment:
            return L"ps_6_8";
        case RM::ShaderType::Mesh:
            return L"ms_6_8";
        case RM::ShaderType::Amplification:
            return L"as_6_8";
    }
    throw std::invalid_argument("Invalid shader type");
}

static std::string_view GetStageName(RM::ShaderType type)
{
    switch (type)
    {
        case RM::ShaderType::Compute:
            return "compute";
        case RM::ShaderType::Vertex:
            return "vertex";
        case RM::ShaderType::Fragment:
            return "fragment";
        case RM::ShaderType::Mesh:
            return "mesh";
        case RM::ShaderType::Amplification:
            return "amplification";
    }
    return "unknown";
}

ComPtr<IDxcBlob> RM::Helpers::CompileHlsl(std::string_view source,
                                          std::string_view entry_point,
                                          ShaderType type)
{
    const wchar_t* target = GetTargetProfile(type);

    ComPtr<IDxcUtils> utils;
    CheckDxc(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)),
             "Failed to create DXC utilities");
    ComPtr<IDxcCompiler3> compiler;
    CheckDxc(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)),
             "Failed to create HLSL compiler");

    if (entry_point.empty() ||
        entry_point.size() > std::numeric_limits<UINT32>::max() ||
        entry_point.find('\0') != std::string_view::npos)
        throw std::invalid_argument("Invalid HLSL entry point");

    ComPtr<IDxcBlobEncoding> entry_utf8;
    CheckDxc(utils->CreateBlob(entry_point.data(),
                               static_cast<UINT32>(entry_point.size()),
                               DXC_CP_UTF8,
                               &entry_utf8),
             "Failed to copy HLSL entry point");
    ComPtr<IDxcBlobWide> entry_wide;
    CheckDxc(utils->GetBlobAsWide(entry_utf8.Get(), entry_wide.GetAddressOf()),
             "Failed to convert HLSL entry point");

    const wchar_t* arguments[] = {
        L"-E", entry_wide->GetStringPointer(),
        L"-T", target,
        L"-HV", L"2021",
    };

    const DxcBuffer buffer{source.data(), source.size(), DXC_CP_UTF8};
    ComPtr<IDxcResult> result;
    CheckDxc(compiler->Compile(&buffer,
                               arguments,
                               std::size(arguments),
                               nullptr,
                               IID_PPV_ARGS(&result)),
             "Failed to invoke HLSL compiler");

    HRESULT status = S_OK;
    CheckDxc(result->GetStatus(&status), "Failed to obtain HLSL compilation status");
    if (FAILED(status))
    {
        ComPtr<IDxcBlobUtf8> diagnostics;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&diagnostics), nullptr);
        const std::string message =
            diagnostics && diagnostics->GetStringLength()
                ? std::string(diagnostics->GetStringPointer(), diagnostics->GetStringLength())
                : "DXC returned no diagnostics";
        throw std::runtime_error("HLSL " + std::string(GetStageName(type)) +
                                 " shader compilation failed: " + message);
    }

    ComPtr<IDxcBlob> dxil;
    CheckDxc(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&dxil), nullptr),
             "Failed to obtain compiled DXIL");
    if (!dxil || !dxil->GetBufferSize())
        throw std::runtime_error("HLSL compiler returned empty DXIL");
    return dxil;
}

bool RM::Helpers::IsDepthStencilFormat(Format format) noexcept
{
    switch (format)
    {
        case Format::Depth16_UNorm:
        case Format::Depth32_Float:
        case Format::Stencil8:
        case Format::Depth24_UNorm_Stencil8:
        case Format::Depth32_Float_Stencil8:
            return true;

        default:
            return false;
    }
}