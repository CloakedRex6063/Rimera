#pragma once

#include "optional"
#include "vector"
#include "string_view"
#include "rimera_enum_flags.hpp"

namespace RM
{
    class Buffer;
    class Texture;
    
    struct DeviceCreateInfo
    {
        void* window = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t resource_descriptor_count = 8192;
        uint32_t sampler_descriptor_count = 256;
    };

    enum class QueueType
    {
        Graphics,
        Compute,
        Transfer
    };

    using CommandType = QueueType;

    struct Size
    {
        uint32_t width;
        uint32_t height;
    };

    enum class LoadOp
    {
        eDontCare,
        Load,
        Clear,
    };

    enum class StoreOp
    {
        eDontCare,
        Store,
    };

    struct ClearColor
    {
        float r, g, b, a;
    };

    struct ColorAttachment
    {
        Texture* texture;
        ClearColor clear_color;
        StoreOp store_op;
        LoadOp load_op;
    };

    struct DepthAttachment
    {
        Texture* texture;
        float clear_depth;
        float clear_stencil;
        StoreOp store_op;
        LoadOp load_op;
    };

    enum class Format : uint16_t
    {
        Invalid,
        A8_UNorm,
        R8_UNorm,
        R8_UNorm_sRGB,
        R8_SNorm,
        R8_UInt,
        R8_SInt,
        R16_UNorm,
        R16_SNorm,
        R16_UInt,
        R16_SInt,
        R16_Float,
        RG8_UNorm,
        RG8_UNorm_sRGB,
        RG8_SNorm,
        RG8_UInt,
        RG8_SInt,
        B5G6R5_UNorm,
        A1BGR5_UNorm,
        ABGR4_UNorm,
        BGR5A1_UNorm,
        R32_UInt,
        R32_SInt,
        R32_Float,
        RG16_UNorm,
        RG16_SNorm,
        RG16_UInt,
        RG16_SInt,
        RG16_Float,
        RGBA8_UNorm,
        RGBA8_UNorm_sRGB,
        RGBA8_SNorm,
        RGBA8_UInt,
        RGBA8_SInt,
        BGRA8_UNorm,
        BGRA8_UNorm_sRGB,
        RGB10A2_UNorm,
        RGB10A2_UInt,
        RG11B10_Float,
        RGB9E5_Float,
        BGR10A2_UNorm,
        BGR10_XR,
        BGR10_XR_sRGB,
        BGRA10_XR,
        BGRA10_XR_sRGB,
        RG32_UInt,
        RG32_SInt,
        RG32_Float,
        RGBA16_UNorm,
        RGBA16_SNorm,
        RGBA16_UInt,
        RGBA16_SInt,
        RGBA16_Float,
        RGBA32_UInt,
        RGBA32_SInt,
        RGBA32_Float,
        BC1_RGBA_UNorm,
        BC1_RGBA_UNorm_sRGB,
        BC2_RGBA_UNorm,
        BC2_RGBA_UNorm_sRGB,
        BC3_RGBA_UNorm,
        BC3_RGBA_UNorm_sRGB,
        BC4_R_UNorm,
        BC4_R_SNorm,
        BC5_RG_UNorm,
        BC5_RG_SNorm,
        BC6H_RGB_Float,
        BC6H_RGB_UFloat,
        BC7_RGBA_UNorm,
        BC7_RGBA_UNorm_sRGB,
        Depth16_UNorm,
        Depth32_Float,
        Stencil8,
        Depth24_UNorm_Stencil8,
        Depth32_Float_Stencil8,
    };

    enum class TextureUsage : uint8_t
    {
        Unknown = 0,
        ShaderRead = 1u << 0,
        ShaderWrite = 1u << 1,
        RenderTarget = 1u << 2,
    };

    struct TextureCreateInfo
    {
        uint32_t width{};
        uint32_t height{};
        uint32_t mip_levels = 1;
        uint32_t array_layers = 1;
        Format format = Format::RGBA8_UNorm;
        EnumFlags<TextureUsage> usage{};
        // Optional synchronous upload of mip 0 for a single-layer RGBA8 texture.
        const void* initial_data = nullptr;
        uint32_t bytes_per_row = 0;
    };

    struct BufferCreateInfo
    {
        uint32_t buffer_size;
        const void* data;
    };

    struct RenderPassCreateInfo
    {
        Size size;
        std::vector<ColorAttachment> color_attachments;
        std::optional<DepthAttachment> depth_attachment;
    };

    struct DispatchGroup
    {
        uint32_t x, y, z;
    };

    enum class ShaderType
    {
        eCompute,
        eRender,
    };

    struct ComputeShaderCreateInfo
    {
        std::string_view code;
        std::string_view entry_point;
    };
    
    struct BLASGeometryCreateInfo
    {
        Buffer* vertex_buffer;
        Buffer* index_buffer;
        uint32_t vertex_stride;
        uint32_t triangle_count;
    };
    
    struct BLASCreateInfo
    {
        std::vector<BLASGeometryCreateInfo> infos;
    };
    
    class BLAS;
    
    struct Float4
    {
        float& operator[](int idx) { return elements[idx]; }
        float  operator[](int idx) const { return elements[idx]; }

        union
        {
            struct
            {
                float x;
                float y;
                float z;
            };

            float elements[4];
        };
    };
    
    struct Float4x4
    {
        Float4&       operator[](int idx) { return columns[idx]; }
        const Float4& operator[](int idx) const { return columns[idx]; }

        Float4        columns[4];
    };
    
    struct TLASInstanceCreateInfo
    {
        Float4x4 transform;
        BLAS* blas;
        uint32_t user_id;
    };
    
    struct TLASCreateInfo
    {
        std::vector<TLASInstanceCreateInfo> infos;
    };
}  // namespace RM
