#include "rimera_helpers.hpp"
#include "rimera_device.hpp"
#include "rimera_builders.hpp"
#include "rimera_compiler.hpp"

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

    auto compiler = CreateCompiler();
    if (!compiler) return std::unexpected(compiler.error());
    auto vertex_shader = (*compiler)->Compile(ShaderBuilder(blit_shader)
                                                  .EntryPoint("VSMain")
                                                  .Build(),
                                              ShaderType::Vertex);
    auto color_shader = (*compiler)->Compile(ShaderBuilder(blit_shader)
                                                 .EntryPoint("PSMain")
                                                 .Build(),
                                             ShaderType::Fragment);
    DestroyCompiler(*compiler);
    if (!vertex_shader || !color_shader)
    {
        auto error = !vertex_shader ? vertex_shader.error() : color_shader.error();
        if (vertex_shader) DestroyShader(*vertex_shader);
        if (color_shader) DestroyShader(*color_shader);
        return std::unexpected(std::move(error));
    }

    auto blit_info = GraphicsPipelineBuilder(*vertex_shader, *color_shader)
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
    auto pipeline = device->CreatePipeline(blit_info);
    DestroyShader(*vertex_shader);
    DestroyShader(*color_shader);
    return pipeline;
}

RM::Result<void> RM::Helpers::CheckDxc(HRESULT result, const char* operation)
{
    if (FAILED(result))
        return std::unexpected(std::string(operation) + " (HRESULT " + std::to_string(result) +
                               ")");
    return {};
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

RM::Helpers::FormatBlockInfo RM::Helpers::GetFormatBlockInfo(Format format) noexcept
{
    switch (format)
    {
        case Format::A8_UNorm:
        case Format::R8_UNorm:
        case Format::R8_UNorm_sRGB:
        case Format::R8_SNorm:
        case Format::R8_UInt:
        case Format::R8_SInt:
        case Format::Stencil8:
            return {.bytes = 1};
        case Format::R16_UNorm:
        case Format::R16_SNorm:
        case Format::R16_UInt:
        case Format::R16_SInt:
        case Format::R16_Float:
        case Format::RG8_UNorm:
        case Format::RG8_UNorm_sRGB:
        case Format::RG8_SNorm:
        case Format::RG8_UInt:
        case Format::RG8_SInt:
        case Format::B5G6R5_UNorm:
        case Format::A1BGR5_UNorm:
        case Format::ABGR4_UNorm:
        case Format::BGR5A1_UNorm:
        case Format::Depth16_UNorm:
            return {.bytes = 2};
        case Format::R32_UInt:
        case Format::R32_SInt:
        case Format::R32_Float:
        case Format::RG16_UNorm:
        case Format::RG16_SNorm:
        case Format::RG16_UInt:
        case Format::RG16_SInt:
        case Format::RG16_Float:
        case Format::RGBA8_UNorm:
        case Format::RGBA8_UNorm_sRGB:
        case Format::RGBA8_SNorm:
        case Format::RGBA8_UInt:
        case Format::RGBA8_SInt:
        case Format::BGRA8_UNorm:
        case Format::BGRA8_UNorm_sRGB:
        case Format::RGB10A2_UNorm:
        case Format::RGB10A2_UInt:
        case Format::RG11B10_Float:
        case Format::RGB9E5_Float:
        case Format::BGR10A2_UNorm:
        case Format::BGR10_XR:
        case Format::BGR10_XR_sRGB:
        case Format::Depth32_Float:
        case Format::Depth24_UNorm_Stencil8:
            return {.bytes = 4};
        case Format::BGRA10_XR:
        case Format::BGRA10_XR_sRGB:
        case Format::RG32_UInt:
        case Format::RG32_SInt:
        case Format::RG32_Float:
        case Format::RGBA16_UNorm:
        case Format::RGBA16_SNorm:
        case Format::RGBA16_UInt:
        case Format::RGBA16_SInt:
        case Format::RGBA16_Float:
        case Format::Depth32_Float_Stencil8:
            return {.bytes = 8};
        case Format::RGBA32_UInt:
        case Format::RGBA32_SInt:
        case Format::RGBA32_Float:
            return {.bytes = 16};
        case Format::BC1_RGBA_UNorm:
        case Format::BC1_RGBA_UNorm_sRGB:
        case Format::BC4_R_UNorm:
        case Format::BC4_R_SNorm:
            return {.bytes = 8, .width = 4, .height = 4};
        case Format::BC2_RGBA_UNorm:
        case Format::BC2_RGBA_UNorm_sRGB:
        case Format::BC3_RGBA_UNorm:
        case Format::BC3_RGBA_UNorm_sRGB:
        case Format::BC5_RG_UNorm:
        case Format::BC5_RG_SNorm:
        case Format::BC6H_RGB_Float:
        case Format::BC6H_RGB_UFloat:
        case Format::BC7_RGBA_UNorm:
        case Format::BC7_RGBA_UNorm_sRGB:
            return {.bytes = 16, .width = 4, .height = 4};
        case Format::Unknown:
            break;
    }
    return {.bytes = 0};
}
