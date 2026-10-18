#include "rimera_helpers.hpp"
#include "rimera_device.hpp"
#include "rimera_builders.hpp"

RM::Result<RM::Pipeline*> RM::Helpers::CreateBlitShader(Device* device, Format format)
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
                     .AddRenderTarget(format)
                     .AddBlendInfo(
                         BlendInfoBuilder()
                         .Disable()
                         .Build())
                     .DisableDepth()
                     .DisableStencil()
                     .Cull(CullMode::None)
                     .Fill(FillMode::Solid)
                     .Primitive(PolygonMode::Triangle)
                     .Build();
    return device->CreatePipeline(blit_info);
}

RM::Result<void> RM::Helpers::CheckDxc(HRESULT result, const char* operation)
{
    if (FAILED(result))
        return std::unexpected(std::string(operation) + " (HRESULT " + std::to_string(result) +
                               ")");
    return {};
}

static RM::Result<const wchar_t*> GetTargetProfile(RM::ShaderType type)
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
    return std::unexpected("Invalid shader type");
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

RM::Result<IDxcBlob*> RM::Helpers::CompileHlsl(std::string_view source,
                                               std::string_view entry_point,
                                               ShaderType type)
{
    auto target = GetTargetProfile(type);
    if (!target) return std::unexpected(target.error());
    if (source.empty()) return std::unexpected("HLSL source must not be empty");
    if (entry_point.empty() ||
        entry_point.size() > std::numeric_limits<UINT32>::max() ||
        entry_point.find('\0') != std::string_view::npos)
        return std::unexpected("Invalid HLSL entry point");

    struct Resources
    {
        IDxcUtils* utils = nullptr;
        IDxcCompiler3* compiler = nullptr;
        IDxcBlobEncoding* entry_utf8 = nullptr;
        IDxcBlobWide* entry_wide = nullptr;
        IDxcResult* result = nullptr;
        IDxcBlobUtf8* diagnostics = nullptr;
        IDxcBlob* dxil = nullptr;
        ~Resources()
        {
            if (dxil) dxil->Release();
            if (diagnostics) diagnostics->Release();
            if (result) result->Release();
            if (entry_wide) entry_wide->Release();
            if (entry_utf8) entry_utf8->Release();
            if (compiler) compiler->Release();
            if (utils) utils->Release();
        }
    } resources;
    if (auto result = CheckDxc(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&resources.utils)),
                               "Failed to create DXC utilities");
        !result)
        return std::unexpected(result.error());
    if (auto result =
            CheckDxc(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&resources.compiler)),
                     "Failed to create HLSL compiler");
        !result)
        return std::unexpected(result.error());
    if (auto result = CheckDxc(resources.utils->CreateBlob(entry_point.data(),
                                                           static_cast<UINT32>(entry_point.size()),
                                                           DXC_CP_UTF8,
                                                           &resources.entry_utf8),
                               "Failed to copy HLSL entry point");
        !result)
        return std::unexpected(result.error());
    if (auto result =
            CheckDxc(resources.utils->GetBlobAsWide(resources.entry_utf8, &resources.entry_wide),
                     "Failed to convert HLSL entry point");
        !result)
        return std::unexpected(result.error());
    const wchar_t* arguments[] = {
        L"-E",
        resources.entry_wide->GetStringPointer(),
        L"-T",
        *target,
        L"-HV",
        L"2021",
    };
    const DxcBuffer buffer{source.data(), source.size(), DXC_CP_UTF8};
    if (auto result = CheckDxc(resources.compiler->Compile(&buffer,
                                                           arguments,
                                                           std::size(arguments),
                                                           nullptr,
                                                           IID_PPV_ARGS(&resources.result)),
                               "Failed to invoke HLSL compiler");
        !result)
        return std::unexpected(result.error());
    HRESULT status = S_OK;
    if (auto result = CheckDxc(resources.result->GetStatus(&status),
                               "Failed to obtain HLSL compilation status");
        !result)
        return std::unexpected(result.error());
    if (FAILED(status))
    {
        if (auto result = CheckDxc(resources.result->GetOutput(DXC_OUT_ERRORS,
                                                               IID_PPV_ARGS(&resources.diagnostics),
                                                               nullptr),
                                   "Failed to obtain HLSL diagnostics");
            !result)
            return std::unexpected(result.error());
        const std::string message =
            resources.diagnostics && resources.diagnostics->GetStringLength()
                ? std::string(resources.diagnostics->GetStringPointer(),
                              resources.diagnostics->GetStringLength())
                : "DXC returned no diagnostics";
        return std::unexpected("HLSL " + std::string(GetStageName(type)) +
                               " shader compilation failed: " + message);
    }
    if (auto result = CheckDxc(
            resources.result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&resources.dxil), nullptr),
            "Failed to obtain compiled DXIL");
        !result)
        return std::unexpected(result.error());
    if (!resources.dxil || !resources.dxil->GetBufferSize())
        return std::unexpected("HLSL compiler returned empty DXIL");
    return std::exchange(resources.dxil, nullptr);
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

bool RM::Helpers::IsEmpty(const Rect& r)
{
    return r.offset.x == 0 && r.offset.y == 0 && r.size.x == 0 && r.size.y == 0;
}