#pragma once
#include "rimera_device.hpp"
#include <memory>
#include <string>

struct IRShaderReflection;

namespace RM::Metal
{
    [[nodiscard]] std::string MetalError(const std::string& operation, const NS::Error* error);

    struct ShaderReflectionDeleter
    {
        void operator()(IRShaderReflection* reflection) const noexcept;
    };

    struct CompiledShader
    {
        NS::SharedPtr<MTL::Function> function;
        std::unique_ptr<IRShaderReflection, ShaderReflectionDeleter> reflection;
        // Populated for compute, mesh, and amplification shaders.
        DispatchGroup compute_dispatch_group{};
        uint32_t payload_size = 0;
    };

    [[nodiscard]] Result<CompiledShader> CompileShader(Device* device,
                                                       const ShaderCreateInfo& info,
                                                       ShaderType type);

    [[nodiscard]] Result<MTL::BlendFactor> ToBlendFactor(BlendFactor factor);
    [[nodiscard]] Result<MTL::BlendOperation> ToBlendOperation(BlendOp operation);
    [[nodiscard]] Result<MTL::StencilOperation> ToStencilOperation(StencilOp operation);
    [[nodiscard]] Result<NS::SharedPtr<MTL::StencilDescriptor>> CreateStencilDescriptor(
        const StencilOpState& state, const DepthStencilState& depth);
    [[nodiscard]] Result<MTL::PrimitiveTopologyClass> ToPrimitiveTopologyClass(PolygonMode mode);
    [[nodiscard]] Result<MTL::CullMode> ToCullMode(CullMode mode);
    [[nodiscard]] Result<MTL::Winding> ToWinding(FrontFace face);
    [[nodiscard]] Result<MTL::TriangleFillMode> ToTriangleFillMode(FillMode mode);
    [[nodiscard]] Result<MTL::VertexFormat> ToVertexFormat(Format format);
    [[nodiscard]] Result<MTL::VertexStepFunction> ToVertexStepFunction(VertexInputRate rate);
    [[nodiscard]] Result<NS::SharedPtr<MTL::VertexDescriptor>> CreateVertexDescriptor(
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
    MTL::StorageMode ToStorageMode(HeapMemory memory);
    NS::SharedPtr<MTL::TextureDescriptor> CreateTextureDescriptor(const TextureCreateInfo& info);
} // namespace RM::Metal
