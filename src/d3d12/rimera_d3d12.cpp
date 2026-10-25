#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#include "windows.h"
#include "wrl/client.h"
using namespace Microsoft::WRL;
#include "directx/d3d12.h"
#include "dxgi1_6.h"
#include "D3D12MemAlloc.h"
#include "algorithm"
#include "array"
#include "cstring"
#include "cwchar"
#include "ranges"
#include "span"
#include "vector"
#include "rimera_d3d12.hpp"
#include "cassert"
#include "rimera_buffer.hpp"
#include "rimera_texture.hpp"
#include "rimera_helpers.hpp"

RM::Result<D3D12_SHADER_BYTECODE> RM::D3D12::GetShaderBytecode(const Shader* shader,
                                                                ShaderType type)
{
    if (!shader) return std::unexpected("Pipeline requires a compiled shader");
    if (shader->GetType() != type)
        return std::unexpected("Compiled shader does not match the pipeline stage");
    return D3D12_SHADER_BYTECODE{.pShaderBytecode = shader->GetBytecode(),
                                 .BytecodeLength = shader->GetBytecodeSize()};
}

#ifdef RIMERA_DEBUG
void RM::D3D12::EnableDebugLayer()
{
    ComPtr<ID3D12Debug1> debug;
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) return;
    debug->EnableDebugLayer();
    debug->SetEnableGPUBasedValidation(TRUE);
}
#endif

std::string RM::D3D12::HResultMessage(const HRESULT hr)
{
    char* buffer = nullptr;
    DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(hr),
        0,
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);

    if (length == 0)
        return "Unknown HRESULT";

    std::string message(buffer, length);
    LocalFree(buffer);

    while (!message.empty() &&
           (message.back() == '\r' || message.back() == '\n'))
        message.pop_back();

    return message;
}

// https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_sdr_white_level
float RM::D3D12::GetSdrWhiteLevel(const wchar_t* device_name)
{
    constexpr float kDefaultWhite = 80.f;
    UINT32 path_count = 0;
    UINT32 mode_count = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) !=
        ERROR_SUCCESS)
        return kDefaultWhite;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,
                           &path_count,
                           paths.data(),
                           &mode_count,
                           modes.data(),
                           nullptr) != ERROR_SUCCESS)
        return kDefaultWhite;

    for (UINT32 i = 0; i < path_count; ++i)
    {
        const auto& path = paths[i];
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header = {.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,
                         .size = sizeof(source),
                         .adapterId = path.sourceInfo.adapterId,
                         .id = path.sourceInfo.id};
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
        if (std::wcscmp(source.viewGdiDeviceName, device_name) != 0) continue;

        DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
        white.header = {.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL,
                        .size = sizeof(white),
                        .adapterId = path.targetInfo.adapterId,
                        .id = path.targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&white.header) != ERROR_SUCCESS) return kDefaultWhite;
        return static_cast<float>(white.SDRWhiteLevel) / 1000.f * kDefaultWhite;
    }
    return kDefaultWhite;
}

D3D12_COMMAND_LIST_TYPE RM::D3D12::GetQueueType(QueueType type)
{
    switch (type)
    {
        case QueueType::Graphics:
            return D3D12_COMMAND_LIST_TYPE_DIRECT;
        case QueueType::Compute:
            return D3D12_COMMAND_LIST_TYPE_COMPUTE;
        case QueueType::Transfer:
            return D3D12_COMMAND_LIST_TYPE_COPY;
    }
    return D3D12_COMMAND_LIST_TYPE_DIRECT;
}

D3D12_RESOURCE_DESC RM::D3D12::GetResourceDesc(const BufferCreateInfo& info)
{
    return {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment = 0,
        .Width = info.size,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = D3D12_RESOURCE_FLAG_NONE,
    };
}

D3D12_RESOURCE_DESC RM::D3D12::GetResourceDesc(const TextureCreateInfo& info)
{
    D3D12_RESOURCE_FLAGS flags{};
    if (info.usage.contains(TextureUsage::RenderTarget))
    {
        flags |= Helpers::IsDepthStencilFormat(info.format)
                     ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                     : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (info.usage.contains(TextureUsage::ShaderWrite))
    {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    auto resource_format = GetFormat(info.format);
    switch (info.format)
    {
        case Format::Depth16_UNorm:
            resource_format = DXGI_FORMAT_R16_TYPELESS;
            break;
        case Format::Depth32_Float:
            resource_format = DXGI_FORMAT_R32_TYPELESS;
            break;
        case Format::Depth24_UNorm_Stencil8:
            resource_format = DXGI_FORMAT_R24G8_TYPELESS;
            break;
        case Format::Depth32_Float_Stencil8:
            resource_format = DXGI_FORMAT_R32G8X24_TYPELESS;
            break;
        default:
            break;
    }
    return {
        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
        .Width = info.width,
        .Height = info.height,
        .DepthOrArraySize = static_cast<uint16_t>(info.array_layers),
        .MipLevels = static_cast<uint16_t>(info.mip_levels),
        .Format = resource_format,
        .SampleDesc = DXGI_SAMPLE_DESC{.Count = 1, .Quality = 0},
        .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags = flags,
    };
}

D3D12_HEAP_TYPE RM::D3D12::GetHeapType(HeapMemory memory)
{
    switch (memory)
    {
        case HeapMemory::DeviceLocal:
            return D3D12_HEAP_TYPE_DEFAULT;
        case HeapMemory::HostVisible:
            return D3D12_HEAP_TYPE_GPU_UPLOAD;
    }
    return D3D12_HEAP_TYPE_DEFAULT;
}

DXGI_FORMAT RM::D3D12::GetFormat(const Format& format)
{
    switch (format)
    {
        case Format::A8_UNorm:
            return DXGI_FORMAT_A8_UNORM;
        case Format::R8_UNorm:
            return DXGI_FORMAT_R8_UNORM;
        case Format::R8_UNorm_sRGB:
            return DXGI_FORMAT_UNKNOWN;
        case Format::R8_SNorm:
            return DXGI_FORMAT_R8_SNORM;
        case Format::R8_UInt:
            return DXGI_FORMAT_R8_UINT;
        case Format::R8_SInt:
            return DXGI_FORMAT_R8_SINT;
        case Format::R16_UNorm:
            return DXGI_FORMAT_R16_UNORM;
        case Format::R16_SNorm:
            return DXGI_FORMAT_R16_SNORM;
        case Format::R16_UInt:
            return DXGI_FORMAT_R16_UINT;
        case Format::R16_SInt:
            return DXGI_FORMAT_R16_SINT;
        case Format::R16_Float:
            return DXGI_FORMAT_R16_FLOAT;
        case Format::RG8_UNorm:
            return DXGI_FORMAT_R8G8_UNORM;
        case Format::RG8_UNorm_sRGB:
            return DXGI_FORMAT_UNKNOWN;
        case Format::RG8_SNorm:
            return DXGI_FORMAT_R8G8_SNORM;
        case Format::RG8_UInt:
            return DXGI_FORMAT_R8G8_UINT;
        case Format::RG8_SInt:
            return DXGI_FORMAT_R8G8_SINT;
        case Format::B5G6R5_UNorm:
            return DXGI_FORMAT_B5G6R5_UNORM;
        case Format::A1BGR5_UNorm:
            return DXGI_FORMAT_UNKNOWN;
        case Format::ABGR4_UNorm:
            return DXGI_FORMAT_UNKNOWN;
        case Format::BGR5A1_UNorm:
            return DXGI_FORMAT_B5G5R5A1_UNORM;
        case Format::R32_UInt:
            return DXGI_FORMAT_R32_UINT;
        case Format::R32_SInt:
            return DXGI_FORMAT_R32_SINT;
        case Format::R32_Float:
            return DXGI_FORMAT_R32_FLOAT;
        case Format::RG16_UNorm:
            return DXGI_FORMAT_R16G16_UNORM;
        case Format::RG16_SNorm:
            return DXGI_FORMAT_R16G16_SNORM;
        case Format::RG16_UInt:
            return DXGI_FORMAT_R16G16_UINT;
        case Format::RG16_SInt:
            return DXGI_FORMAT_R16G16_SINT;
        case Format::RG16_Float:
            return DXGI_FORMAT_R16G16_FLOAT;
        case Format::RGBA8_UNorm:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8_UNorm_sRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case Format::RGBA8_SNorm:
            return DXGI_FORMAT_R8G8B8A8_SNORM;
        case Format::RGBA8_UInt:
            return DXGI_FORMAT_R8G8B8A8_UINT;
        case Format::RGBA8_SInt:
            return DXGI_FORMAT_R8G8B8A8_SINT;
        case Format::BGRA8_UNorm:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case Format::BGRA8_UNorm_sRGB:
            return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case Format::RGB10A2_UNorm:
            return DXGI_FORMAT_R10G10B10A2_UNORM;
        case Format::RGB10A2_UInt:
            return DXGI_FORMAT_R10G10B10A2_UINT;
        case Format::RG11B10_Float:
            return DXGI_FORMAT_R11G11B10_FLOAT;
        case Format::RGB9E5_Float:
            return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
        case Format::BGR10A2_UNorm:
            return DXGI_FORMAT_UNKNOWN;
        case Format::BGR10_XR:
            return DXGI_FORMAT_UNKNOWN;
        case Format::BGR10_XR_sRGB:
            return DXGI_FORMAT_UNKNOWN;
        case Format::BGRA10_XR:
            return DXGI_FORMAT_UNKNOWN;
        case Format::BGRA10_XR_sRGB:
            return DXGI_FORMAT_UNKNOWN;
        case Format::RG32_UInt:
            return DXGI_FORMAT_R32G32_UINT;
        case Format::RG32_SInt:
            return DXGI_FORMAT_R32G32_SINT;
        case Format::RG32_Float:
            return DXGI_FORMAT_R32G32_FLOAT;
        case Format::RGBA16_UNorm:
            return DXGI_FORMAT_R16G16B16A16_UNORM;
        case Format::RGBA16_SNorm:
            return DXGI_FORMAT_R16G16B16A16_SNORM;
        case Format::RGBA16_UInt:
            return DXGI_FORMAT_R16G16B16A16_UINT;
        case Format::RGBA16_SInt:
            return DXGI_FORMAT_R16G16B16A16_SINT;
        case Format::RGBA16_Float:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case Format::RGBA32_UInt:
            return DXGI_FORMAT_R32G32B32A32_UINT;
        case Format::RGBA32_SInt:
            return DXGI_FORMAT_R32G32B32A32_SINT;
        case Format::RGBA32_Float:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case Format::BC1_RGBA_UNorm:
            return DXGI_FORMAT_BC1_UNORM;
        case Format::BC1_RGBA_UNorm_sRGB:
            return DXGI_FORMAT_BC1_UNORM_SRGB;
        case Format::BC2_RGBA_UNorm:
            return DXGI_FORMAT_BC2_UNORM;
        case Format::BC2_RGBA_UNorm_sRGB:
            return DXGI_FORMAT_BC2_UNORM_SRGB;
        case Format::BC3_RGBA_UNorm:
            return DXGI_FORMAT_BC3_UNORM;
        case Format::BC3_RGBA_UNorm_sRGB:
            return DXGI_FORMAT_BC3_UNORM_SRGB;
        case Format::BC4_R_UNorm:
            return DXGI_FORMAT_BC4_UNORM;
        case Format::BC4_R_SNorm:
            return DXGI_FORMAT_BC4_SNORM;
        case Format::BC5_RG_UNorm:
            return DXGI_FORMAT_BC5_UNORM;
        case Format::BC5_RG_SNorm:
            return DXGI_FORMAT_BC5_SNORM;
        case Format::BC6H_RGB_Float:
            return DXGI_FORMAT_BC6H_SF16;
        case Format::BC6H_RGB_UFloat:
            return DXGI_FORMAT_BC6H_UF16;
        case Format::BC7_RGBA_UNorm:
            return DXGI_FORMAT_BC7_UNORM;
        case Format::BC7_RGBA_UNorm_sRGB:
            return DXGI_FORMAT_BC7_UNORM_SRGB;
        case Format::Depth16_UNorm:
            return DXGI_FORMAT_D16_UNORM;
        case Format::Depth32_Float:
            return DXGI_FORMAT_D32_FLOAT;
        case Format::Stencil8:
            return DXGI_FORMAT_UNKNOWN;
        case Format::Depth24_UNorm_Stencil8:
            return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case Format::Depth32_Float_Stencil8:
            return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

RM::Format RM::D3D12::GetFormat(const DXGI_FORMAT& format)
{
    switch (format)
    {
        case DXGI_FORMAT_UNKNOWN:
            return Format::Unknown;
        case DXGI_FORMAT_A8_UNORM:
            return Format::A8_UNorm;
        case DXGI_FORMAT_R8_UNORM:
            return Format::R8_UNorm;
        case DXGI_FORMAT_R8_SNORM:
            return Format::R8_SNorm;
        case DXGI_FORMAT_R8_UINT:
            return Format::R8_UInt;
        case DXGI_FORMAT_R8_SINT:
            return Format::R8_SInt;
        case DXGI_FORMAT_R16_UNORM:
            return Format::R16_UNorm;
        case DXGI_FORMAT_R16_SNORM:
            return Format::R16_SNorm;
        case DXGI_FORMAT_R16_UINT:
            return Format::R16_UInt;
        case DXGI_FORMAT_R16_SINT:
            return Format::R16_SInt;
        case DXGI_FORMAT_R16_FLOAT:
            return Format::R16_Float;
        case DXGI_FORMAT_R8G8_UNORM:
            return Format::RG8_UNorm;
        case DXGI_FORMAT_R8G8_SNORM:
            return Format::RG8_SNorm;
        case DXGI_FORMAT_R8G8_UINT:
            return Format::RG8_UInt;
        case DXGI_FORMAT_R8G8_SINT:
            return Format::RG8_SInt;
        case DXGI_FORMAT_B5G6R5_UNORM:
            return Format::B5G6R5_UNorm;
        case DXGI_FORMAT_B5G5R5A1_UNORM:
            return Format::BGR5A1_UNorm;
        case DXGI_FORMAT_R32_UINT:
            return Format::R32_UInt;
        case DXGI_FORMAT_R32_SINT:
            return Format::R32_SInt;
        case DXGI_FORMAT_R32_FLOAT:
            return Format::R32_Float;
        case DXGI_FORMAT_R16G16_UNORM:
            return Format::RG16_UNorm;
        case DXGI_FORMAT_R16G16_SNORM:
            return Format::RG16_SNorm;
        case DXGI_FORMAT_R16G16_UINT:
            return Format::RG16_UInt;
        case DXGI_FORMAT_R16G16_SINT:
            return Format::RG16_SInt;
        case DXGI_FORMAT_R16G16_FLOAT:
            return Format::RG16_Float;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            return Format::RGBA8_UNorm;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return Format::RGBA8_UNorm_sRGB;
        case DXGI_FORMAT_R8G8B8A8_SNORM:
            return Format::RGBA8_SNorm;
        case DXGI_FORMAT_R8G8B8A8_UINT:
            return Format::RGBA8_UInt;
        case DXGI_FORMAT_R8G8B8A8_SINT:
            return Format::RGBA8_SInt;
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            return Format::BGRA8_UNorm;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return Format::BGRA8_UNorm_sRGB;
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            return Format::RGB10A2_UNorm;
        case DXGI_FORMAT_R10G10B10A2_UINT:
            return Format::RGB10A2_UInt;
        case DXGI_FORMAT_R11G11B10_FLOAT:
            return Format::RG11B10_Float;
        case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
            return Format::RGB9E5_Float;
        case DXGI_FORMAT_R32G32_UINT:
            return Format::RG32_UInt;
        case DXGI_FORMAT_R32G32_SINT:
            return Format::RG32_SInt;
        case DXGI_FORMAT_R32G32_FLOAT:
            return Format::RG32_Float;
        case DXGI_FORMAT_R16G16B16A16_UNORM:
            return Format::RGBA16_UNorm;
        case DXGI_FORMAT_R16G16B16A16_SNORM:
            return Format::RGBA16_SNorm;
        case DXGI_FORMAT_R16G16B16A16_UINT:
            return Format::RGBA16_UInt;
        case DXGI_FORMAT_R16G16B16A16_SINT:
            return Format::RGBA16_SInt;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return Format::RGBA16_Float;
        case DXGI_FORMAT_R32G32B32A32_UINT:
            return Format::RGBA32_UInt;
        case DXGI_FORMAT_R32G32B32A32_SINT:
            return Format::RGBA32_SInt;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return Format::RGBA32_Float;
        case DXGI_FORMAT_BC1_UNORM:
            return Format::BC1_RGBA_UNorm;
        case DXGI_FORMAT_BC1_UNORM_SRGB:
            return Format::BC1_RGBA_UNorm_sRGB;
        case DXGI_FORMAT_BC2_UNORM:
            return Format::BC2_RGBA_UNorm;
        case DXGI_FORMAT_BC2_UNORM_SRGB:
            return Format::BC2_RGBA_UNorm_sRGB;
        case DXGI_FORMAT_BC3_UNORM:
            return Format::BC3_RGBA_UNorm;
        case DXGI_FORMAT_BC3_UNORM_SRGB:
            return Format::BC3_RGBA_UNorm_sRGB;
        case DXGI_FORMAT_BC4_UNORM:
            return Format::BC4_R_UNorm;
        case DXGI_FORMAT_BC4_SNORM:
            return Format::BC4_R_SNorm;
        case DXGI_FORMAT_BC5_UNORM:
            return Format::BC5_RG_UNorm;
        case DXGI_FORMAT_BC5_SNORM:
            return Format::BC5_RG_SNorm;
        case DXGI_FORMAT_BC6H_SF16:
            return Format::BC6H_RGB_Float;
        case DXGI_FORMAT_BC6H_UF16:
            return Format::BC6H_RGB_UFloat;
        case DXGI_FORMAT_BC7_UNORM:
            return Format::BC7_RGBA_UNorm;
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return Format::BC7_RGBA_UNorm_sRGB;
        case DXGI_FORMAT_D16_UNORM:
            return Format::Depth16_UNorm;
        case DXGI_FORMAT_D32_FLOAT:
            return Format::Depth32_Float;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
            return Format::Depth24_UNorm_Stencil8;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return Format::Depth32_Float_Stencil8;
        default:
            return Format::Unknown;
    }
}

D3D12_BARRIER_ACCESS RM::D3D12::GetBarrierAccess(BarrierAccess access)
{
    return static_cast<D3D12_BARRIER_ACCESS>(
        static_cast<uint32_t>(access));
}

D3D12_BARRIER_SYNC RM::D3D12::GetBarrierSync(BarrierStage stage)
{
    const EnumFlags flags(stage);
    D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_NONE;
    if (flags.intersects({BarrierStage::VertexShading,
                          BarrierStage::AmplificationShading,
                          BarrierStage::MeshShading}))
        sync |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
    if (flags.intersects(BarrierStage::PixelShading)) sync |= D3D12_BARRIER_SYNC_PIXEL_SHADING;
    if (flags.intersects(BarrierStage::ComputeShading))
        sync |= D3D12_BARRIER_SYNC_COMPUTE_SHADING;
    if (flags.intersects(BarrierStage::Copy)) sync |= D3D12_BARRIER_SYNC_COPY;
    if (flags.intersects(BarrierStage::AccelerationStructureBuild))
        sync |= D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
    if (flags.intersects(BarrierStage::AccelerationStructureCopy))
        sync |= D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE;
    if (flags.intersects(BarrierStage::RenderTarget)) sync |= D3D12_BARRIER_SYNC_RENDER_TARGET;
    if (flags.intersects(BarrierStage::DepthStencil)) sync |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
    if (flags.intersects(BarrierStage::IndexInput)) sync |= D3D12_BARRIER_SYNC_INDEX_INPUT;
    return sync;
}

D3D12_BARRIER_TYPE RM::D3D12::GetBarrierType(BarrierType type)
{
    switch (type)
    {
        case BarrierType::Texture:
            return D3D12_BARRIER_TYPE_TEXTURE;
        case BarrierType::Buffer:
            return D3D12_BARRIER_TYPE_BUFFER;
    }
    return D3D12_BARRIER_TYPE_GLOBAL;
}

D3D12_RENDER_PASS_BEGINNING_ACCESS RM::D3D12::ToBeginAccess(const Format format,
                                                            const LoadOp load_op,
                                                            const ClearColor clear_color)
{
    D3D12_RENDER_PASS_BEGINNING_ACCESS access{};
    switch (load_op)
    {
        case LoadOp::Load:
            access.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
            break;
        case LoadOp::Clear:
            access.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
            access.Clear.ClearValue.Format = GetFormat(format);
            access.Clear.ClearValue.Color[0] = clear_color.r;
            access.Clear.ClearValue.Color[1] = clear_color.g;
            access.Clear.ClearValue.Color[2] = clear_color.b;
            access.Clear.ClearValue.Color[3] = clear_color.a;
            break;
        case LoadOp::DontCare:
            access.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
            break;
    }
    return access;
}

D3D12_RENDER_PASS_BEGINNING_ACCESS RM::D3D12::ToBeginAccess(const Format format,
                                                            const LoadOp load_op,
                                                            const float clear_depth,
                                                            const uint8_t depth)
{
    D3D12_RENDER_PASS_BEGINNING_ACCESS access{};
    switch (load_op)
    {
        case LoadOp::Load:
            access.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
            break;
        case LoadOp::Clear:
            access.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
            access.Clear.ClearValue.Format = GetFormat(format);
            access.Clear.ClearValue.DepthStencil.Depth = clear_depth;
            access.Clear.ClearValue.DepthStencil.Stencil = depth;
            break;
        case LoadOp::DontCare:
            access.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
            break;
    }
    return access;
}

D3D12_RENDER_PASS_ENDING_ACCESS RM::D3D12::ToEndAccess(const StoreOp store_op)
{
    D3D12_RENDER_PASS_ENDING_ACCESS end_access{};
    switch (store_op)
    {
        case StoreOp::Store:
            end_access.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
            break;

        case StoreOp::DontCare:
            end_access.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD;
            break;
    }
    return end_access;
}

D3D12_GLOBAL_BARRIER RM::D3D12::ConvertBarrier(const GlobalBarrier& src)
{
    D3D12_GLOBAL_BARRIER dst{};
    FillScope(dst, src);
    return dst;
}

D3D12_TEXTURE_BARRIER RM::D3D12::ConvertBarrier(const TextureBarrier& src)
{
#ifdef RIMERA_DEBUG
    assert(src.texture != nullptr);
    assert(src.subresource.num_mips > 0);
    assert(src.subresource.num_array_layers > 0);
#endif

    D3D12_TEXTURE_BARRIER dst{};
    FillScope(dst, src);

    dst.LayoutBefore = static_cast<D3D12_BARRIER_LAYOUT>(
        static_cast<uint32_t>(src.layout_before));

    dst.LayoutAfter = static_cast<D3D12_BARRIER_LAYOUT>(
        static_cast<uint32_t>(src.layout_after));

    dst.pResource = static_cast<ID3D12Resource*>(src.texture->GetNativeHandle());

    dst.Subresources.IndexOrFirstMipLevel = src.texture->GetBaseMip() + src.subresource.first_mip;
    dst.Subresources.NumMipLevels = src.subresource.num_mips;
    dst.Subresources.FirstArraySlice =
        src.texture->GetBaseLayer() + src.subresource.first_array_layer;
    dst.Subresources.NumArraySlices = src.subresource.num_array_layers;
    dst.Subresources.FirstPlane = 0;
    dst.Subresources.NumPlanes = 1;

    dst.Flags = src.discard ? D3D12_TEXTURE_BARRIER_FLAG_DISCARD : D3D12_TEXTURE_BARRIER_FLAG_NONE;
    return dst;
}

D3D12_BUFFER_BARRIER RM::D3D12::ConvertBarrier(const BufferBarrier& src)
{
#ifdef RIMERA_DEBUG
    assert(src.buffer != nullptr);
#endif

    D3D12_BUFFER_BARRIER dst{};
    FillScope(dst, src);

    dst.pResource = static_cast<ID3D12Resource*>(src.buffer->GetNativeHandle());
    dst.Offset = src.offset;
    dst.Size = src.size;

    return dst;
}

D3D12_FILL_MODE RM::D3D12::GetFillMode(const FillMode mode) noexcept
{
    switch (mode)
    {
        case FillMode::Wireframe:
            return D3D12_FILL_MODE_WIREFRAME;
        case FillMode::Solid:
            return D3D12_FILL_MODE_SOLID;
    }
    return D3D12_FILL_MODE_WIREFRAME;
}

D3D12_PRIMITIVE_TOPOLOGY_TYPE RM::D3D12::GetPolygonMode(const PolygonMode mode) noexcept
{
    switch (mode)
    {
        case PolygonMode::Point:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        case PolygonMode::Line:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case PolygonMode::Triangle:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
}

D3D12_PRIMITIVE_TOPOLOGY RM::D3D12::GetPrimitiveTopology(PrimitiveTopology topology) noexcept
{
    switch (topology)
    {
        case PrimitiveTopology::PointList:
            return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        case PrimitiveTopology::LineList:
            return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case PrimitiveTopology::LineStrip:
            return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case PrimitiveTopology::TriangleList:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case PrimitiveTopology::TriangleStrip:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    }
    return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

D3D12_COMPARISON_FUNC RM::D3D12::GetComparisonFunc(const ComparisonFunc test) noexcept
{
    switch (test)
    {
        case ComparisonFunc::Always:
            return D3D12_COMPARISON_FUNC_ALWAYS;
        case ComparisonFunc::Never:
            return D3D12_COMPARISON_FUNC_NEVER;
        case ComparisonFunc::Less:
            return D3D12_COMPARISON_FUNC_LESS;
        case ComparisonFunc::LessEqual:
            return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case ComparisonFunc::Greater:
            return D3D12_COMPARISON_FUNC_GREATER;
        case ComparisonFunc::GreaterEqual:
            return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case ComparisonFunc::Equal:
            return D3D12_COMPARISON_FUNC_EQUAL;
        case ComparisonFunc::NotEqual:
            return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    }
    return D3D12_COMPARISON_FUNC_ALWAYS;
}

D3D12_CULL_MODE RM::D3D12::GetCullMode(const CullMode mode) noexcept
{
    switch (mode)
    {
        case CullMode::Back:
            return D3D12_CULL_MODE_BACK;
        case CullMode::Front:
            return D3D12_CULL_MODE_FRONT;
        case CullMode::None:
            return D3D12_CULL_MODE_NONE;
    }
    return D3D12_CULL_MODE_NONE;
}

bool RM::D3D12::GetFrontFace(const FrontFace front_face) noexcept
{
    switch (front_face)
    {
        case FrontFace::Clockwise:
            return false;
        case FrontFace::CounterClockwise:
            return true;
    }
    return true;
}

D3D12_CONSERVATIVE_RASTERIZATION_MODE RM::D3D12::GetConservativeRaster(
    bool conservative_raster) noexcept
{
    return conservative_raster
               ? D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON
               : D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
}

D3D12_DEPTH_WRITE_MASK RM::D3D12::GetDepthWriteMask(bool depth_write_enable) noexcept
{
    return depth_write_enable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
}

D3D12_STENCIL_OP RM::D3D12::GetStencilOp(StencilOp op) noexcept
{
    switch (op)
    {
        case StencilOp::Keep:
            return D3D12_STENCIL_OP_KEEP;
        case StencilOp::Zero:
            return D3D12_STENCIL_OP_ZERO;
        case StencilOp::Replace:
            return D3D12_STENCIL_OP_REPLACE;
        case StencilOp::IncreaseSat:
            return D3D12_STENCIL_OP_INCR_SAT;
        case StencilOp::DecreaseSat:
            return D3D12_STENCIL_OP_DECR_SAT;
        case StencilOp::Invert:
            return D3D12_STENCIL_OP_INVERT;
        case StencilOp::Increment:
            return D3D12_STENCIL_OP_INCR;
        case StencilOp::Decrement:
            return D3D12_STENCIL_OP_DECR;
    }
    return D3D12_STENCIL_OP_KEEP;
}

D3D12_DEPTH_STENCILOP_DESC RM::D3D12::GetStencilOpDesc(StencilOpState op_desc) noexcept
{
    D3D12_DEPTH_STENCILOP_DESC stencil_op_desc{
        .StencilFailOp = GetStencilOp(op_desc.StencilFailOp),
        .StencilDepthFailOp = GetStencilOp(op_desc.StencilDepthFailOp),
        .StencilPassOp = GetStencilOp(op_desc.StencilPassOp),
        .StencilFunc = GetComparisonFunc(op_desc.StencilFunc)
    };
    return stencil_op_desc;
}

D3D12_INPUT_CLASSIFICATION RM::D3D12::GetInputClassification(const VertexInputRate rate) noexcept
{
    switch (rate)
    {
        case VertexInputRate::Vertex:
            return D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        case VertexInputRate::Instance:
            return D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
    }
    return D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
}

D3D12_BLEND RM::D3D12::GetBlendFactor(BlendFactor factor) noexcept
{
    switch (factor)
    {
        case BlendFactor::Zero:
            return D3D12_BLEND_ZERO;
        case BlendFactor::One:
            return D3D12_BLEND_ONE;
        case BlendFactor::SrcColor:
            return D3D12_BLEND_SRC_COLOR;
        case BlendFactor::InvSrcColor:
            return D3D12_BLEND_INV_SRC_COLOR;
        case BlendFactor::SrcAlpha:
            return D3D12_BLEND_SRC_ALPHA;
        case BlendFactor::InvSrcAlpha:
            return D3D12_BLEND_INV_SRC_ALPHA;
        case BlendFactor::DestColor:
            return D3D12_BLEND_DEST_COLOR;
        case BlendFactor::InvDestColor:
            return D3D12_BLEND_INV_DEST_COLOR;
        case BlendFactor::DestAlpha:
            return D3D12_BLEND_DEST_ALPHA;
        case BlendFactor::InvDestAlpha:
            return D3D12_BLEND_INV_DEST_ALPHA;
        case BlendFactor::SrcAlphaSat:
            return D3D12_BLEND_SRC_ALPHA_SAT;
        case BlendFactor::ColorFactor:
            return D3D12_BLEND_BLEND_FACTOR;
        case BlendFactor::InvColorFactor:
            return D3D12_BLEND_INV_BLEND_FACTOR;
        case BlendFactor::Src1Color:
            return D3D12_BLEND_SRC1_COLOR;
        case BlendFactor::InvSrc1Color:
            return D3D12_BLEND_INV_SRC1_COLOR;
        case BlendFactor::Src1Alpha:
            return D3D12_BLEND_SRC1_ALPHA;
        case BlendFactor::InvSrc1Alpha:
            return D3D12_BLEND_INV_SRC1_ALPHA;
        case BlendFactor::AlphaFactor:
            return D3D12_BLEND_ALPHA_FACTOR;
        case BlendFactor::InvAlphaFactor:
            return D3D12_BLEND_INV_ALPHA_FACTOR;
    }
    return D3D12_BLEND_ZERO;
}

D3D12_BLEND_OP RM::D3D12::GetBlendOp(BlendOp op) noexcept
{
    switch (op)
    {
        case BlendOp::Add:
            return D3D12_BLEND_OP_ADD;
        case BlendOp::Subtract:
            return D3D12_BLEND_OP_SUBTRACT;
        case BlendOp::RevSubtract:
            return D3D12_BLEND_OP_REV_SUBTRACT;
        case BlendOp::Min:
            return D3D12_BLEND_OP_MIN;
        case BlendOp::Max:
            return D3D12_BLEND_OP_MAX;
    }
    return D3D12_BLEND_OP_ADD;
}

D3D12_LOGIC_OP RM::D3D12::GetLogicOp(LogicOp op) noexcept
{
    switch (op)
    {
        case LogicOp::Clear:
            return D3D12_LOGIC_OP_CLEAR;
        case LogicOp::And:
            return D3D12_LOGIC_OP_AND;
        case LogicOp::AndReverse:
            return D3D12_LOGIC_OP_AND_REVERSE;
        case LogicOp::Copy:
            return D3D12_LOGIC_OP_COPY;
        case LogicOp::AndInverted:
            return D3D12_LOGIC_OP_AND_INVERTED;
        case LogicOp::NoOp:
            return D3D12_LOGIC_OP_NOOP;
        case LogicOp::Xor:
            return D3D12_LOGIC_OP_XOR;
        case LogicOp::Or:
            return D3D12_LOGIC_OP_OR;
        case LogicOp::Nor:
            return D3D12_LOGIC_OP_NOR;
        case LogicOp::Equivalent:
            return D3D12_LOGIC_OP_EQUIV;
        case LogicOp::Invert:
            return D3D12_LOGIC_OP_INVERT;
        case LogicOp::OrReverse:
            return D3D12_LOGIC_OP_OR_REVERSE;
        case LogicOp::CopyInverted:
            return D3D12_LOGIC_OP_COPY_INVERTED;
        case LogicOp::OrInverted:
            return D3D12_LOGIC_OP_OR_INVERTED;
        case LogicOp::Nand:
            return D3D12_LOGIC_OP_NAND;
        case LogicOp::Set:
            return D3D12_LOGIC_OP_SET;
    }
    return D3D12_LOGIC_OP_CLEAR;
}

namespace RM::D3D12
{
    using RM::Filter;
    using RM::ReductionType;

    constexpr D3D12_FILTER EncodeFilter(const Filter min_filter,
                                        const Filter mag_filter,
                                        const ReductionType type = ReductionType::Standard) noexcept
    {
        constexpr auto get_base = [](const Filter f) -> int
        {
            switch (f)
            {
                case Filter::Nearest:
                case Filter::NearestMipNearest:
                case Filter::NearestMipLinear:
                    return 0;
                case Filter::Linear:
                case Filter::LinearMipNearest:
                case Filter::LinearMipLinear:
                    return 1;
            }
            return 0;
        };

        constexpr auto get_mip = [](const Filter f) -> int
        {
            switch (f)
            {
                case Filter::Nearest:
                case Filter::Linear:
                case Filter::NearestMipNearest:
                case Filter::LinearMipNearest:
                    return 0;
                case Filter::NearestMipLinear:
                case Filter::LinearMipLinear:
                    return 1;
            }
            return 0;
        };

        const int min_type = get_base(min_filter);
        const int mag_type = get_base(mag_filter);
        const int mip_type = get_mip(min_filter);

        const D3D12_FILTER_REDUCTION_TYPE reduction = [&]() -> D3D12_FILTER_REDUCTION_TYPE
        {
            switch (type)
            {
                case ReductionType::Standard:
                    return D3D12_FILTER_REDUCTION_TYPE_STANDARD;
                case ReductionType::Comparison:
                    return D3D12_FILTER_REDUCTION_TYPE_COMPARISON;
                case ReductionType::Minimum:
                    return D3D12_FILTER_REDUCTION_TYPE_MINIMUM;
                case ReductionType::Maximum:
                    return D3D12_FILTER_REDUCTION_TYPE_MAXIMUM;
            }
            return D3D12_FILTER_REDUCTION_TYPE_STANDARD;
        }();

        return D3D12_ENCODE_BASIC_FILTER(static_cast<D3D12_FILTER_TYPE>(min_type),
                                         static_cast<D3D12_FILTER_TYPE>(mag_type),
                                         static_cast<D3D12_FILTER_TYPE>(mip_type),
                                         reduction);
    }

    // Standard
    static_assert(
        EncodeFilter(Filter::NearestMipNearest, Filter::Nearest) ==
        D3D12_FILTER_MIN_MAG_MIP_POINT);
    static_assert(
        EncodeFilter(Filter::NearestMipLinear, Filter::Nearest) ==
        D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR);
    static_assert(
        EncodeFilter(Filter::NearestMipNearest, Filter::Linear) ==
        D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT);
    static_assert(
        EncodeFilter(Filter::NearestMipLinear, Filter::Linear) ==
        D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR);
    static_assert(
        EncodeFilter(Filter::LinearMipNearest, Filter::Nearest) ==
        D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT);
    static_assert(
        EncodeFilter(Filter::LinearMipLinear, Filter::Nearest) ==
        D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR);
    static_assert(
        EncodeFilter(Filter::LinearMipNearest, Filter::Linear) ==
        D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT);
    static_assert(
        EncodeFilter(Filter::LinearMipLinear, Filter::Linear) == D3D12_FILTER_MIN_MAG_MIP_LINEAR);

    // Comparison
    static_assert(
        EncodeFilter(Filter::NearestMipNearest, Filter::Nearest, ReductionType::Comparison) ==
        D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT);
    static_assert(
        EncodeFilter(Filter::NearestMipLinear, Filter::Nearest, ReductionType::Comparison) ==
        D3D12_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR);
    static_assert(
        EncodeFilter(Filter::NearestMipNearest, Filter::Linear, ReductionType::Comparison) ==
        D3D12_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT);
    static_assert(
        EncodeFilter(Filter::NearestMipLinear, Filter::Linear, ReductionType::Comparison) ==
        D3D12_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR);
    static_assert(
        EncodeFilter(Filter::LinearMipNearest, Filter::Nearest, ReductionType::Comparison) ==
        D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT);
    static_assert(
        EncodeFilter(Filter::LinearMipLinear, Filter::Nearest, ReductionType::Comparison) ==
        D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR);
    static_assert(
        EncodeFilter(Filter::LinearMipNearest, Filter::Linear, ReductionType::Comparison) ==
        D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT);
    static_assert(
        EncodeFilter(Filter::LinearMipLinear, Filter::Linear, ReductionType::Comparison) ==
        D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR);

    // Minimum
    static_assert(
        EncodeFilter(Filter::NearestMipNearest, Filter::Nearest, ReductionType::Minimum) ==
        D3D12_FILTER_MINIMUM_MIN_MAG_MIP_POINT);
    static_assert(EncodeFilter(Filter::NearestMipLinear, Filter::Nearest, ReductionType::Minimum) ==
                  D3D12_FILTER_MINIMUM_MIN_MAG_POINT_MIP_LINEAR);
    static_assert(EncodeFilter(Filter::NearestMipNearest, Filter::Linear, ReductionType::Minimum) ==
                  D3D12_FILTER_MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT);
    static_assert(EncodeFilter(Filter::NearestMipLinear, Filter::Linear, ReductionType::Minimum) ==
                  D3D12_FILTER_MINIMUM_MIN_POINT_MAG_MIP_LINEAR);
    static_assert(EncodeFilter(Filter::LinearMipNearest, Filter::Nearest, ReductionType::Minimum) ==
                  D3D12_FILTER_MINIMUM_MIN_LINEAR_MAG_MIP_POINT);
    static_assert(EncodeFilter(Filter::LinearMipLinear, Filter::Nearest, ReductionType::Minimum) ==
                  D3D12_FILTER_MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR);
    static_assert(EncodeFilter(Filter::LinearMipNearest, Filter::Linear, ReductionType::Minimum) ==
                  D3D12_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT);
    static_assert(EncodeFilter(Filter::LinearMipLinear, Filter::Linear, ReductionType::Minimum) ==
                  D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR);

    // Maximum
    static_assert(
        EncodeFilter(Filter::NearestMipNearest, Filter::Nearest, ReductionType::Maximum) ==
        D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_POINT);
    static_assert(EncodeFilter(Filter::NearestMipLinear, Filter::Nearest, ReductionType::Maximum) ==
                  D3D12_FILTER_MAXIMUM_MIN_MAG_POINT_MIP_LINEAR);
    static_assert(EncodeFilter(Filter::NearestMipNearest, Filter::Linear, ReductionType::Maximum) ==
                  D3D12_FILTER_MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT);
    static_assert(EncodeFilter(Filter::NearestMipLinear, Filter::Linear, ReductionType::Maximum) ==
                  D3D12_FILTER_MAXIMUM_MIN_POINT_MAG_MIP_LINEAR);
    static_assert(EncodeFilter(Filter::LinearMipNearest, Filter::Nearest, ReductionType::Maximum) ==
                  D3D12_FILTER_MAXIMUM_MIN_LINEAR_MAG_MIP_POINT);
    static_assert(EncodeFilter(Filter::LinearMipLinear, Filter::Nearest, ReductionType::Maximum) ==
                  D3D12_FILTER_MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR);
    static_assert(EncodeFilter(Filter::LinearMipNearest, Filter::Linear, ReductionType::Maximum) ==
                  D3D12_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT);
    static_assert(EncodeFilter(Filter::LinearMipLinear, Filter::Linear, ReductionType::Maximum) ==
                  D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR);
}

D3D12_FILTER RM::D3D12::ToFilter(const Filter min_filter,
                                 const Filter mag_filter,
                                 const ReductionType type) noexcept
{
    return RM::D3D12::EncodeFilter(min_filter, mag_filter, type);
}

D3D12_TEXTURE_ADDRESS_MODE RM::D3D12::ToWrap(const Wrap wrap) noexcept
{
    switch (wrap)
    {
        case Wrap::Repeat:
            return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case Wrap::MirroredRepeat:
            return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        case Wrap::ClampToEdge:
            return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case Wrap::Border:
            return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    }
    return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
}
