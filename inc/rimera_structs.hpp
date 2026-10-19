#pragma once
#include "rimera_result.hpp"

#include "optional"
#include "vector"
#include "string_view"
#include "rimera_enum_flags.hpp"

namespace RM
{
    class Buffer;
    class Texture;

    struct UInt2
    {
        uint32_t& operator[](int idx) { return elements[idx]; }
        uint32_t operator[](int idx) const { return elements[idx]; }

        union
        {
            struct
            {
                uint32_t x;
                uint32_t y;
            };

            uint32_t elements[2];
        };
    };

    struct Float2
    {
        float& operator[](int idx) { return elements[idx]; }
        float operator[](int idx) const { return elements[idx]; }

        union
        {
            struct
            {
                float x;
                float y;
            };

            float elements[2];
        };
    };

    struct Float4
    {
        float& operator[](int idx) { return elements[idx]; }
        float operator[](int idx) const { return elements[idx]; }

        union
        {
            struct
            {
                float x;
                float y;
                float z;
                float w;
            };

            float elements[4];
        };
    };

    struct Float4x4
    {
        Float4& operator[](int idx) { return columns[idx]; }
        const Float4& operator[](int idx) const { return columns[idx]; }

        Float4 columns[4];
    };

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

    enum class LoadOp
    {
        DontCare,
        Load,
        Clear,
    };

    enum class StoreOp
    {
        DontCare,
        Store,
    };

    struct ClearColor
    {
        float r, g, b, a;
    };

    struct ColorAttachment
    {
        const Texture* texture;
        ClearColor clear_color;
        StoreOp store_op;
        LoadOp load_op;
    };

    struct DepthAttachment
    {
        const Texture* texture;
        float clear_depth;
        float clear_stencil;
        LoadOp load_op;
        StoreOp store_op;
    };

    enum class Format : uint16_t
    {
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
        Unknown
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
        const void* initial_data = nullptr;
        uint32_t bytes_per_row = 0;
    };

    enum class BufferFlags
    {
        eNone = 0,
        eShaderResource = 1 << 1,
        eUnorderedAccess = 1 << 2,
    };

    struct BufferCreateInfo
    {
        uint32_t size;
        const void* data;
        EnumFlags<BufferFlags> flags;
    };

    enum class HeapMemory
    {
        DeviceLocal,
        HostVisible,
    };

    struct HeapCreateInfo
    {
        uint64_t size = 0;
        HeapMemory memory = HeapMemory::DeviceLocal;
    };

    struct ResourceAllocationInfo
    {
        uint64_t size = 0;
        uint64_t alignment = 0;
    };

    struct RenderPassCreateInfo
    {
        UInt2 size;
        std::vector<ColorAttachment> color_attachments;
        std::optional<DepthAttachment> depth_attachment;
    };

    struct DispatchGroup
    {
        uint32_t x, y, z;
    };

    enum class PipelineType
    {
        Compute,
        Graphics,
        MeshGraphics,
    };

    enum class ShaderType
    {
        Compute,
        Vertex,
        Fragment,
        Mesh,
        Amplification
    };

    struct ShaderCreateInfo
    {
        std::string_view code;
        std::string_view entry_point;
    };

    struct ComputePipelineCreateInfo
    {
        ShaderCreateInfo shader;
    };

    enum class FrontFace
    {
        Clockwise,
        CounterClockwise,
    };

    enum class CullMode
    {
        Back,
        Front,
        None
    };

    enum class FillMode
    {
        Wireframe,
        Solid
    };

    enum class ComparisonFunc
    {
        Always,
        Never,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        Equal,
        NotEqual,
    };

    enum class StencilOp
    {
        Keep,
        Zero,
        Replace,
        IncreaseSat,
        DecreaseSat,
        Invert,
        Increment,
        Decrement
    };

    struct StencilOpState
    {
        StencilOp StencilFailOp;
        StencilOp StencilDepthFailOp;
        StencilOp StencilPassOp;
        ComparisonFunc StencilFunc;
    };

    struct DepthStencilState
    {
        bool depth_enable;
        bool depth_write_enable;
        ComparisonFunc depth_test;
        bool stencil_enable;
        uint8_t stencil_read_mask;
        uint8_t stencil_write_mask;
        StencilOpState front_stencil_op;
        StencilOpState back_stencil_op;
    };

    struct RasterizerState
    {
        FillMode fill_mode = FillMode::Solid;
        CullMode cull_mode = CullMode::Back;
        FrontFace front_face = FrontFace::CounterClockwise;
        int depth_bias = 0;
        float depth_bias_clamp = 0;
        float slope_scaled_depth_bias = 0;
        bool depth_clip_enable = false;
        bool multisample_enable = false;
    };

    enum class BlendFactor
    {
        Zero,
        One,
        SrcColor,
        InvSrcColor,
        SrcAlpha,
        InvSrcAlpha,
        DestColor,
        InvDestColor,
        DestAlpha,
        InvDestAlpha,
        SrcAlphaSat,
        ColorFactor,
        InvColorFactor,
        Src1Color,
        InvSrc1Color,
        Src1Alpha,
        InvSrc1Alpha,
        AlphaFactor,
        InvAlphaFactor
    };

    enum class VertexInputRate
    {
        Vertex,
        Instance
    };

    struct VertexBinding
    {
        uint32_t binding;
        uint32_t stride;
        VertexInputRate input_rate;
    };

    struct VertexAttribute
    {
        uint32_t location;
        uint32_t binding;
        Format format;
        uint32_t offset;
        std::string semantic;
    };

    enum class BlendOp
    {
        Add,
        Subtract,
        RevSubtract,
        Min,
        Max,
    };

    enum class LogicOp
    {
        Clear,
        And,
        AndReverse,
        Copy,
        AndInverted,
        NoOp,
        Xor,
        Or,
        Nor,
        Equivalent,
        Invert,
        OrReverse,
        CopyInverted,
        OrInverted,
        Nand,
        Set
    };

    struct BlendInfo
    {
        bool blend_enable = false;
        bool logic_op_enable = false;
        BlendFactor src_color_factor;
        BlendFactor dst_color_factor;
        BlendFactor src_alpha_factor;
        BlendFactor dst_alpha_factor;
        BlendOp color_blend_op;
        BlendOp alpha_blend_op;
        LogicOp logic_op;
    };

    enum class PolygonMode
    {
        Point,
        Line,
        Triangle
    };

    enum class PrimitiveTopology
    {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip,
    };

    struct VertexInputState
    {
        std::vector<VertexBinding> bindings;
        std::vector<VertexAttribute> attributes;
    };

    struct GraphicsPipelineCreateInfo
    {
        std::vector<Format> rtv_formats;
        std::optional<Format> dsv_format;
        std::vector<BlendInfo> blend_infos;
        ShaderCreateInfo vertex_shader;
        ShaderCreateInfo fragment_shader;
        DepthStencilState depth_stencil_state;
        RasterizerState rasterizer_state;
        VertexInputState input_state;
        PolygonMode polygon_mode;
    };

    struct MeshGraphicsPipelineCreateInfo
    {
        ShaderCreateInfo ampl_shader;
        ShaderCreateInfo mesh_shader;
        ShaderCreateInfo fragment_shader;
        std::vector<Format> rtv_formats;
        std::optional<Format> dsv_format;
        std::vector<BlendInfo> blend_infos;
        DepthStencilState depth_stencil_state{};
        RasterizerState rasterizer_state{};
        PolygonMode polygon_mode = PolygonMode::Triangle;
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

    enum class BarrierType
    {
        Texture,
        Buffer
    };

    enum class BarrierAccess : uint32_t
    {
        Common = 0,
        VertexBuffer = 1u << 0,
        ConstantBuffer = 1u << 1,
        IndexBuffer = 1u << 2,
        RenderTarget = 1u << 3,
        UnorderedAccess = 1u << 4,
        DepthStencilWrite = 1u << 5,
        DepthStencilRead = 1u << 6,
        ShaderResource = 1u << 7,
        IndirectArgument = 1u << 9,
        Predication = IndirectArgument,
        CopyDestination = 1u << 10,
        CopySource = 1u << 11,
        ResolveDestination = 1u << 12,
        ResolveSource = 1u << 13,
        AccelerationStructureRead = 1u << 14,
        AccelerationStructureWrite = 1u << 15,
        ShadingRateSource = 1u << 16,
        NoAccess = 1u << 31,
    };

    enum class BarrierStage : uint32_t
    {
        None = 0,
        VertexShading = 1u << 0,
        PixelShading = 1u << 1,
        ComputeShading = 1u << 2,
        Copy = 1u << 3,
        AmplificationShading = 1u << 4,
        MeshShading = 1u << 5,
        AccelerationStructureBuild = 1u << 6,
        AccelerationStructureCopy = 1u << 7,
        RenderTarget = 1u << 8,
        DepthStencil = 1u << 9,
        IndexInput = 1u << 10,
        BlitSource = PixelShading,
        BlitDestination = RenderTarget,
    };

    enum class BarrierLayout : uint32_t
    {
        Undefined = 0xffffffffu,
        Common = 0,
        Present = Common,
        GenericRead = 1,
        RenderTarget = 2,
        UnorderedAccess = 3,
        DepthStencilWrite = 4,
        DepthStencilRead = 5,
        ShaderResource = 6,
        CopySource = 7,
        CopyDestination = 8,
        ResolveSource = 9,
        ResolveDestination = 10,
        ShadingRateSource = 11,
        BlitSource = ShaderResource,
        BlitDestination = RenderTarget,
    };

    struct BarrierScope
    {
        BarrierStage sync_before;
        BarrierStage sync_after;
        BarrierAccess access_before;
        BarrierAccess access_after;
    };

    struct TextureBarrierSubresource
    {
        uint32_t first_mip = 0;
        uint32_t num_mips = 1;

        uint32_t first_array_layer = 0;
        uint32_t num_array_layers = 1;
    };

    struct GlobalBarrier
    {
        BarrierScope scope;
    };

    struct TextureBarrier
    {
        BarrierScope scope;

        BarrierLayout layout_before;
        BarrierLayout layout_after;

        Texture* texture;
        TextureBarrierSubresource subresource;
        bool discard = false;
    };

    struct BufferBarrier
    {
        BarrierScope scope;
        Buffer* buffer;
        uint64_t offset = 0;
        uint64_t size = UINT64_MAX;
    };

    struct BarrierGroup
    {
        std::span<const GlobalBarrier> global_barriers;
        std::span<const TextureBarrier> texture_barriers;
        std::span<const BufferBarrier> buffer_barriers;
    };

    struct Rect
    {
        UInt2 offset;
        UInt2 size;
    };

    struct Viewport
    {
        UInt2 offset;
        UInt2 size;
        Float2 depth_range;
    };

    struct BlitRegion
    {
        Rect src_rect;
        Rect dst_rect;
        uint32_t src_mip;
    };

    enum class ReductionType
    {
        Standard,
        Comparison,
        Minimum,
        Maximum,
    };

    enum class Filter
    {
        Nearest,
        Linear,
        NearestMipNearest,
        LinearMipNearest,
        NearestMipLinear,
        LinearMipLinear,
    };

    enum class Wrap
    {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        Border,
    };

    enum class BorderColor
    {
        Black,
        White,
        TransparentBlack,
    };

    struct SamplerCreateInfo
    {
        Filter min_filter = Filter::Linear;
        Filter mag_filter = Filter::Linear;
        Wrap wrap_u = Wrap::Repeat;
        Wrap wrap_v = Wrap::Repeat;
        Wrap wrap_w = Wrap::Repeat;
        float min_lod = 0;
        float max_lod = 13;
        uint32_t max_anisotropy = 0;
        BorderColor border_color = BorderColor::Black;
        ComparisonFunc comparison_func = ComparisonFunc::Never;
        ReductionType reduction_type = ReductionType::Standard;
    };

    enum class TextureViewType
    {
        RenderTarget,
        DepthStencil,
        ShaderRead,
        ShaderReadWrite
    };

    struct TextureViewCreateInfo
    {
        Texture* texture;
        TextureViewType view_type;
        Format format = Format::Unknown;
        uint32_t base_mip = 0;
        uint32_t mip_count = 1;
        uint32_t base_layer = 0;
        uint32_t layer_count = 1;
    };

    struct RenderGraphCreateInfo
    {
        uint32_t frames_in_flight = 3;
        uint64_t heap_granularity = 16ull * 1024 * 1024;
    };
} // namespace RM
