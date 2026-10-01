#pragma once
#include "rimera_device.hpp"
#include <memory>

struct IRShaderReflection;

namespace RM::Metal
{
    struct ShaderReflectionDeleter
    {
        void operator()(IRShaderReflection* reflection) const noexcept;
    };

    struct CompiledShader
    {
        NS::SharedPtr<MTL::Function> function;
        std::unique_ptr<IRShaderReflection, ShaderReflectionDeleter> reflection;
        // Populated only for compute shaders.
        DispatchGroup compute_dispatch_group{};
    };

    [[nodiscard]] CompiledShader CompileShader(Device* device,
                                               const ShaderCreateInfo& info,
                                               ShaderType type);

    MTL::BlendFactor ToBlendFactor(BlendFactor factor);
    MTL::BlendOperation ToBlendOperation(BlendOp operation);
    MTL::StencilOperation ToStencilOperation(StencilOp operation);
    MTL::PrimitiveTopologyClass ToPrimitiveTopologyClass(PolygonMode mode);
    MTL::CullMode ToCullMode(CullMode mode);
    MTL::Winding ToWinding(FrontFace face);
    MTL::TriangleFillMode ToTriangleFillMode(FillMode mode);
    MTL::VertexFormat ToVertexFormat(Format format);
    MTL::VertexStepFunction ToVertexStepFunction(VertexInputRate rate);
    NS::SharedPtr<MTL::VertexDescriptor> CreateVertexDescriptor(
        const VertexInputState& input,
        const IRShaderReflection* reflection);

    MTL::PixelFormat ToPixelFormat(Format format);
    MTL::PrimitiveType ToPrimitiveType(PrimitiveTopology topology);

    Format FromPixelFormat(MTL::PixelFormat format);
    MTL::TextureUsage ToTextureUsage(EnumFlags<TextureUsage> flags);
    MTL::SamplerMinMagFilter ToSamplerMinMagFilter(Filter filter);
    MTL::SamplerMipFilter ToSamplerMipFilter(Filter filter);
    MTL::SamplerAddressMode ToSamplerAddressMode(Wrap wrap);
    MTL::SamplerBorderColor ToSamplerBorderColor(BorderColor color);
    MTL::CompareFunction ToCompareFunction(ComparisonFunc function);
    MTL::SamplerReductionMode ToSamplerReductionMode(ReductionType type);
    bool SupportsHDR(void* window);
    NS::SharedPtr<CA::MetalLayer> CreateSwapLayer(NS::SharedPtr<MTL::Device>& device,
                                                  const DeviceCreateInfo& info);
    MTL::Stages ToBarrierStages(BarrierStage stages);
} // namespace RM::Metal