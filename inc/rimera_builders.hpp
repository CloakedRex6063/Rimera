#include "rimera_structs.hpp"
#include "assert.h"

namespace RM
{
    template <class Derived, class T>
    class BarrierBuilderBase
    {
    public:
        Derived& BeforeStage(BarrierStage stage)
        {
            m_value.scope.sync_before = stage;
            return static_cast<Derived&>(*this);
        }

        Derived& BeforeAccess(BarrierAccess access)
        {
            m_value.scope.access_before = access;
            return static_cast<Derived&>(*this);
        }

        Derived& AfterStage(BarrierStage stage)
        {
            m_value.scope.sync_after = stage;
            return static_cast<Derived&>(*this);
        }

        Derived& AfterAccess(BarrierAccess access)
        {
            m_value.scope.access_after = access;
            return static_cast<Derived&>(*this);
        }

        [[nodiscard]] T Build() const
        {
            return m_value;
        }

    protected:
        BarrierBuilderBase()
        {
            m_value.scope.sync_before = BarrierStage::None;
            m_value.scope.sync_after = BarrierStage::None;
            m_value.scope.access_before = BarrierAccess::NoAccess;
            m_value.scope.access_after = BarrierAccess::NoAccess;
        }

        T m_value{};
    };

    class GlobalBarrierBuilder
        : public BarrierBuilderBase<GlobalBarrierBuilder, GlobalBarrier>
    {
    };

    class BufferBarrierBuilder
        : public BarrierBuilderBase<BufferBarrierBuilder, BufferBarrier>
    {
    public:
        explicit BufferBarrierBuilder(Buffer* buffer)
        {
            m_value.buffer = buffer;
            m_value.offset = 0;
            m_value.size = UINT64_MAX;
        }

        BufferBarrierBuilder& Size(uint32_t size)
        {
            m_value.size = size;
            return *this;
        }

        BufferBarrierBuilder& Offset(uint32_t offset)
        {
            m_value.offset = offset;
            return *this;
        }
    };

    class TextureBarrierBuilder
        : public BarrierBuilderBase<TextureBarrierBuilder, TextureBarrier>
    {
    public:
        explicit TextureBarrierBuilder(Texture* texture)
        {
            m_value.texture = texture;
            m_value.layout_before = BarrierLayout::Common;
            m_value.layout_after = BarrierLayout::Common;

            m_value.subresource = {.first_mip = 0, .num_mips = 1, .first_array_layer = 0,
                                   .num_array_layers = 1};
        }

        TextureBarrierBuilder& BeforeLayout(BarrierLayout layout)
        {
            m_value.layout_before = layout;
            return *this;
        }

        TextureBarrierBuilder& AfterLayout(BarrierLayout layout)
        {
            m_value.layout_after = layout;
            return *this;
        }

        TextureBarrierBuilder& Mips(uint32_t first, uint32_t count)
        {
            m_value.subresource.first_mip = first;
            m_value.subresource.num_mips = count;
            return *this;
        }

        TextureBarrierBuilder& ArrayLayers(uint32_t first, uint32_t count)
        {
            m_value.subresource.first_array_layer = first;
            m_value.subresource.num_array_layers = count;
            return *this;
        }

        TextureBarrierBuilder& Subresources(TextureBarrierSubresource range)
        {
            m_value.subresource = range;
            return *this;
        }
    };

    class RenderPassBuilder
    {
    public:
        explicit RenderPassBuilder(UInt2 size)
        {
            m_value.size = size;
        }

        RenderPassBuilder& Color(
            const TextureView* texture,
            LoadOp load = LoadOp::Load,
            StoreOp store = StoreOp::Store,
            ClearColor clear = {0.f, 0.f, 0.f, 1.f})
        {
            ColorAttachment attachment{};
            attachment.texture = texture;
            attachment.clear_color = clear;
            attachment.load_op = load;
            attachment.store_op = store;

            m_value.color_attachments.push_back(attachment);
            return *this;
        }

        RenderPassBuilder& Depth(
            const TextureView* texture,
            LoadOp load = LoadOp::Load,
            StoreOp store = StoreOp::Store,
            float clear_depth = 1.f,
            float clear_stencil = 0.f)
        {
            DepthAttachment attachment{};
            attachment.texture = texture;
            attachment.clear_depth = clear_depth;
            attachment.clear_stencil = clear_stencil;
            attachment.load_op = load;
            attachment.store_op = store;

            m_value.depth_attachment = attachment;
            return *this;
        }

        [[nodiscard]] RenderPassCreateInfo Build() const
        {
            return m_value;
        }

    private:
        RenderPassCreateInfo m_value{};
    };

    class BlendInfoBuilder
    {
    public:
        BlendInfoBuilder()
        {
            m_value.src_color_factor = BlendFactor::One;
            m_value.dst_color_factor = BlendFactor::Zero;
            m_value.src_alpha_factor = BlendFactor::One;
            m_value.dst_alpha_factor = BlendFactor::Zero;
            m_value.color_blend_op = BlendOp::Add;
            m_value.alpha_blend_op = BlendOp::Add;
            m_value.logic_op = LogicOp::Copy;
        }

        BlendInfoBuilder& Color(
            BlendFactor src,
            BlendFactor dst,
            BlendOp op = BlendOp::Add)
        {
            m_value.blend_enable = true;
            m_value.logic_op_enable = false;
            m_value.src_color_factor = src;
            m_value.dst_color_factor = dst;
            m_value.color_blend_op = op;
            return *this;
        }

        BlendInfoBuilder& Alpha(
            BlendFactor src,
            BlendFactor dst,
            BlendOp op = BlendOp::Add)
        {
            m_value.blend_enable = true;
            m_value.logic_op_enable = false;
            m_value.src_alpha_factor = src;
            m_value.dst_alpha_factor = dst;
            m_value.alpha_blend_op = op;
            return *this;
        }

        BlendInfoBuilder& Logic(LogicOp op)
        {
            m_value.blend_enable = false;
            m_value.logic_op_enable = true;
            m_value.logic_op = op;
            return *this;
        }

        BlendInfoBuilder& Disable()
        {
            m_value.blend_enable = false;
            m_value.logic_op_enable = false;
            return *this;
        }

        [[nodiscard]] BlendInfo Build() const
        {
            return m_value;
        }

    private:
        BlendInfo m_value{};
    };

    class VertexBindingBuilder
    {
    public:
        VertexBindingBuilder(uint32_t binding, uint32_t stride)
        {
            m_value.binding = binding;
            m_value.stride = stride;
            m_value.input_rate = VertexInputRate::Vertex;
        }

        VertexBindingBuilder& Rate(VertexInputRate rate)
        {
            m_value.input_rate = rate;
            return *this;
        }

        [[nodiscard]] VertexBinding Build() const
        {
            return m_value;
        }

    private:
        VertexBinding m_value{};
    };

    class VertexAttributeBuilder
    {
    public:
        VertexAttributeBuilder(uint32_t location, Format format)
        {
            m_value.location = location;
            m_value.format = format;
        }

        VertexAttributeBuilder& Binding(uint32_t binding)
        {
            m_value.binding = binding;
            return *this;
        }

        VertexAttributeBuilder& Offset(uint32_t offset)
        {
            m_value.offset = offset;
            return *this;
        }

        VertexAttributeBuilder& Semantic(std::string_view semantic)
        {
            m_value.semantic = std::string(semantic);
            return *this;
        }

        [[nodiscard]] VertexAttribute Build() const
        {
            return m_value;
        }

    private:
        VertexAttribute m_value{};
    };

    class ShaderBuilder
    {
    public:
        explicit ShaderBuilder(std::string_view code)
        {
            m_value.code = code;
        }

        ShaderBuilder& EntryPoint(std::string_view entry_point)
        {
            m_value.entry_point = entry_point;
            return *this;
        }

        [[nodiscard]] ShaderCreateInfo Build() const
        {
            return m_value;
        }

    private:
        ShaderCreateInfo m_value{};
    };

    class GraphicsPipelineBuilder
    {
    public:
        GraphicsPipelineBuilder(
            ShaderCreateInfo vertex_shader,
            ShaderCreateInfo fragment_shader)
        {
            m_value.vertex_shader = std::move(vertex_shader);
            m_value.fragment_shader = std::move(fragment_shader);
            m_value.polygon_mode = PolygonMode::Triangle;

            m_value.rasterizer_state = RasterizerState{};
            m_value.rasterizer_state.depth_clip_enable = true;

            auto& depth = m_value.depth_stencil_state;
            depth.depth_enable = false;
            depth.depth_write_enable = false;
            depth.depth_test = ComparisonFunc::Less;
            depth.stencil_enable = false;
            depth.stencil_read_mask = 0xff;
            depth.stencil_write_mask = 0xff;

            constexpr StencilOpState stencil{
                StencilOp::Keep,
                StencilOp::Keep,
                StencilOp::Keep,
                ComparisonFunc::Always
            };

            depth.front_stencil_op = stencil;
            depth.back_stencil_op = stencil;
        }

        GraphicsPipelineBuilder& AddRenderTarget(Format format)
        {
            m_value.rtv_formats.push_back(format);
            return *this;
        }

        GraphicsPipelineBuilder& AddBlendInfo(BlendInfo blend)
        {
            m_value.blend_infos.push_back(blend);
            return *this;
        }

        GraphicsPipelineBuilder& AddVertexBinding(const VertexBinding& binding)
        {
            m_value.input_state.bindings.push_back(binding);
            return *this;
        }

        GraphicsPipelineBuilder& AddVertexAttribute(const VertexAttribute& attribute)
        {
            m_value.input_state.attributes.push_back(attribute);
            return *this;
        }

        GraphicsPipelineBuilder& DepthFormat(Format format)
        {
            m_value.dsv_format = format;
            return *this;
        }

        GraphicsPipelineBuilder& Depth(
            ComparisonFunc comparison = ComparisonFunc::Less,
            bool write = true)
        {
            m_value.depth_stencil_state.depth_enable = true;
            m_value.depth_stencil_state.depth_write_enable = write;
            m_value.depth_stencil_state.depth_test = comparison;
            return *this;
        }

        GraphicsPipelineBuilder& DisableDepth()
        {
            m_value.depth_stencil_state.depth_enable = false;
            m_value.depth_stencil_state.depth_write_enable = false;
            return *this;
        }

        GraphicsPipelineBuilder& Stencil(
            StencilOpState front,
            StencilOpState back,
            uint8_t read_mask = 0xff,
            uint8_t write_mask = 0xff)
        {
            auto& depth = m_value.depth_stencil_state;
            depth.stencil_enable = true;
            depth.stencil_read_mask = read_mask;
            depth.stencil_write_mask = write_mask;
            depth.front_stencil_op = front;
            depth.back_stencil_op = back;
            return *this;
        }

        GraphicsPipelineBuilder& DisableStencil()
        {
            m_value.depth_stencil_state.stencil_enable = false;
            return *this;
        }

        GraphicsPipelineBuilder& DepthStencil(const DepthStencilState& state)
        {
            m_value.depth_stencil_state = state;
            return *this;
        }

        GraphicsPipelineBuilder& Rasterizer(const RasterizerState& state)
        {
            m_value.rasterizer_state = state;
            return *this;
        }

        GraphicsPipelineBuilder& Cull(CullMode mode)
        {
            m_value.rasterizer_state.cull_mode = mode;
            return *this;
        }

        GraphicsPipelineBuilder& Winding(FrontFace face)
        {
            m_value.rasterizer_state.front_face = face;
            return *this;
        }

        GraphicsPipelineBuilder& Fill(FillMode mode)
        {
            m_value.rasterizer_state.fill_mode = mode;
            return *this;
        }

        GraphicsPipelineBuilder& DepthBias(
            int bias,
            float slope_scaled = 0.f,
            float clamp = 0.f)
        {
            m_value.rasterizer_state.depth_bias = bias;
            m_value.rasterizer_state.slope_scaled_depth_bias = slope_scaled;
            m_value.rasterizer_state.depth_bias_clamp = clamp;
            return *this;
        }

        GraphicsPipelineBuilder& DepthClip(bool enabled = true)
        {
            m_value.rasterizer_state.depth_clip_enable = enabled;
            return *this;
        }

        GraphicsPipelineBuilder& Multisample(bool enabled = true)
        {
            m_value.rasterizer_state.multisample_enable = enabled;
            return *this;
        }

        GraphicsPipelineBuilder& Primitive(PolygonMode mode)
        {
            m_value.polygon_mode = mode;
            return *this;
        }

        [[nodiscard]] GraphicsPipelineCreateInfo Build() const
        {
            return m_value;
        }

    private:
        GraphicsPipelineCreateInfo m_value{};
    };
}