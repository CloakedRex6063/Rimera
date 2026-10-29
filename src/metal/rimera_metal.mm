#include "rimera_metal.hpp"
#include "rimera_helpers.hpp"
#include "rimera_compiler.hpp"
#include "dxc/dxcapi.h"
#include "metal_irconverter/metal_irconverter.h"
#define IR_RUNTIME_METALCPP
#include "metal_irconverter_runtime/metal_irconverter_runtime.h"
#include "string"
#include "utility"

namespace RM::Metal
{
    RM::Result<std::pair<IRShaderStage, const char*>> ShaderStage(RM::ShaderType type)
    {
        switch (type)
        {
            case RM::ShaderType::Compute:
                return std::pair{IRShaderStageCompute, "compute"};
            case RM::ShaderType::Vertex:
                return std::pair{IRShaderStageVertex, "vertex"};
            case RM::ShaderType::Fragment:
                return std::pair{IRShaderStageFragment, "fragment"};
            case RM::ShaderType::Mesh:
                return std::pair{IRShaderStageMesh, "mesh"};
            case RM::ShaderType::Amplification:
                return std::pair{IRShaderStageAmplification, "amplification"};
        }
        return std::unexpected("Unsupported shader type");
    }

    std::string MetalError(const std::string& operation, const NS::Error* error)
    {
        const auto* description = error ? error->localizedDescription() : nullptr;
        const char* message = description ? description->utf8String() : nullptr;
        return std::string(operation + ": " +
                           (message ? message : "Metal returned no error description"));
    }
}

void RM::Metal::ShaderReflectionDeleter::operator()(IRShaderReflection* reflection) const noexcept
{
    IRShaderReflectionDestroy(reflection);
}

RM::Result<RM::Metal::CompiledShader> RM::Metal::CompileShader(Device* device,
                                                               const Shader* shader,
                                                               ShaderType type)
{
    auto stage_result = ShaderStage(type);
    if (!stage_result) return std::unexpected(stage_result.error());
    const auto [stage, stage_name] = *stage_result;
    const std::string shader_name = std::string(stage_name) + " shader";
    if (!device) return std::unexpected(shader_name + " creation requires a valid device");
    if (!shader) return std::unexpected(shader_name + " requires a compiled shader");
    if (shader->GetType() != type)
        return std::unexpected("Compiled shader does not match the " + shader_name + " stage");

    const std::string entry_point(shader->GetEntryPoint());
    std::unique_ptr<IRCompiler, decltype(&IRCompilerDestroy)> compiler(
        IRCompilerCreate(), IRCompilerDestroy);
    if (!compiler) return std::unexpected("Failed to create Metal shader converter compiler");
    IRCompilerSetEntryPointName(compiler.get(), entry_point.c_str());
    if (type == ShaderType::Vertex)
        IRCompilerSetStageInGenerationMode(compiler.get(), IRStageInCodeGenerationModeUseMetalVertexFetch);

    std::unique_ptr<IRObject, decltype(&IRObjectDestroy)> dxil(
        IRObjectCreateFromDXIL(static_cast<const uint8_t*>(shader->GetBytecode()),
                               shader->GetBytecodeSize(), IRBytecodeOwnershipNone),
        IRObjectDestroy);
    if (!dxil) return std::unexpected("Failed to create DXIL object");
    const auto* root_sig = static_cast<IRRootSignature*>(device->GetNativeRootSignature());
    IRCompilerSetGlobalRootSignature(compiler.get(), root_sig);

    IRError* converter_error = nullptr;
    std::unique_ptr<IRObject, decltype(&IRObjectDestroy)> ir(
        IRCompilerAllocCompileAndLink(compiler.get(), nullptr, dxil.get(), &converter_error),
        IRObjectDestroy);
    std::unique_ptr<IRError, decltype(&IRErrorDestroy)> error(converter_error, IRErrorDestroy);
    if (!ir)
        return std::unexpected(
            "DXIL " + shader_name + " conversion failed (converter error " +
            (error ? std::to_string(IRErrorGetCode(error.get())) : "unavailable") + ")");
    if (IRObjectGetMetalIRShaderStage(ir.get()) != stage)
        return std::unexpected("DXIL must contain a " + shader_name);

    CompiledShader result;
    result.reflection.reset(IRShaderReflectionCreate());
    if (!result.reflection)
        return std::unexpected("Failed to create " + shader_name + " reflection");
    if (!IRObjectGetReflection(ir.get(), stage, result.reflection.get()))
        return std::unexpected("Failed to obtain " + shader_name + " reflection");

    if (type == ShaderType::Compute)
    {
        IRVersionedCSInfo compute_info{};
        if (!IRShaderReflectionCopyComputeInfo(result.reflection.get(), IRReflectionVersion_1_0,
                                                &compute_info))
            return std::unexpected("Failed to obtain compute shader threadgroup size");
        result.compute_dispatch_group = {compute_info.info_1_0.tg_size[0],
                                         compute_info.info_1_0.tg_size[1],
                                         compute_info.info_1_0.tg_size[2]};
        IRShaderReflectionReleaseComputeInfo(&compute_info);
        const auto group = result.compute_dispatch_group;
        if (!group.x || !group.y || !group.z)
            return std::unexpected("Compute shader has an invalid threadgroup size");
    }
    else if (type == ShaderType::Mesh)
    {
        IRVersionedMSInfo mesh_info{};
        if (!IRShaderReflectionCopyMeshInfo(result.reflection.get(), IRReflectionVersion_1_0,
                                            &mesh_info))
            return std::unexpected("Failed to obtain mesh shader threadgroup size");
        result.compute_dispatch_group = {.x = mesh_info.info_1_0.num_threads[0],
                                         .y = mesh_info.info_1_0.num_threads[1],
                                         .z = mesh_info.info_1_0.num_threads[2]};
        IRShaderReflectionReleaseMeshInfo(&mesh_info);
        const auto group = result.compute_dispatch_group;
        if (!group.x || !group.y || !group.z)
            return std::unexpected("Mesh shader has an invalid threadgroup size");
    }
    else if (type == ShaderType::Amplification)
    {
        IRVersionedASInfo ampl_info{};
        if (!IRShaderReflectionCopyAmplificationInfo(result.reflection.get(), IRReflectionVersion_1_0,
                                                     &ampl_info))
            return std::unexpected("Failed to obtain amplification shader threadgroup size");
        result.compute_dispatch_group = {.x = ampl_info.info_1_0.num_threads[0],
                                         .y = ampl_info.info_1_0.num_threads[1],
                                         .z = ampl_info.info_1_0.num_threads[2]};
        result.payload_size = ampl_info.info_1_0.max_payload_size_in_bytes;
        IRShaderReflectionReleaseAmplificationInfo(&ampl_info);
        const auto group = result.compute_dispatch_group;
        if (!group.x || !group.y || !group.z)
            return std::unexpected("Amplification shader has an invalid threadgroup size");
    }

    const char* metal_entry_point = IRShaderReflectionGetEntryPointFunctionName(result.reflection.get());
    if (!metal_entry_point || !*metal_entry_point)
        return std::unexpected(shader_name + " reflection has no entry point");

    std::unique_ptr<IRMetalLibBinary, decltype(&IRMetalLibBinaryDestroy)> binary(
        IRMetalLibBinaryCreate(), IRMetalLibBinaryDestroy);
    if (!binary) return std::unexpected("Failed to create " + shader_name + " metallib object");
    if (!IRObjectGetMetalLibBinary(ir.get(), stage, binary.get()))
        return std::unexpected("Failed to extract " + shader_name + " metallib");
    const auto bytecode = IRMetalLibGetBytecodeData(binary.get());
    if (!bytecode || !IRMetalLibGetBytecodeSize(binary.get()))
        return std::unexpected(shader_name + " metallib is empty");

    auto* native_device = static_cast<MTL::Device*>(device->GetNativeHandle());
    auto pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    NS::Error* metal_error = nullptr;
    auto library = NS::TransferPtr(native_device->newLibrary(bytecode, &metal_error));
    if (!library)
        return std::unexpected(
            MetalError("Failed to load " + shader_name + " metallib", metal_error));
    result.function = NS::TransferPtr(
        library->newFunction(NS::String::string(metal_entry_point, NS::UTF8StringEncoding)));
    if (!result.function)
        return std::unexpected("Metal " + std::string(stage_name) +
                               " entry point not found: " + metal_entry_point);
    return result;
}

RM::Result<MTL::BlendFactor> RM::Metal::ToBlendFactor(BlendFactor factor)
{
    switch (factor)
    {
        case BlendFactor::Zero: return MTL::BlendFactorZero;
        case BlendFactor::One: return MTL::BlendFactorOne;
        case BlendFactor::SrcColor: return MTL::BlendFactorSourceColor;
        case BlendFactor::InvSrcColor: return MTL::BlendFactorOneMinusSourceColor;
        case BlendFactor::SrcAlpha: return MTL::BlendFactorSourceAlpha;
        case BlendFactor::InvSrcAlpha: return MTL::BlendFactorOneMinusSourceAlpha;
        case BlendFactor::DestColor: return MTL::BlendFactorDestinationColor;
        case BlendFactor::InvDestColor: return MTL::BlendFactorOneMinusDestinationColor;
        case BlendFactor::DestAlpha: return MTL::BlendFactorDestinationAlpha;
        case BlendFactor::InvDestAlpha: return MTL::BlendFactorOneMinusDestinationAlpha;
        case BlendFactor::SrcAlphaSat: return MTL::BlendFactorSourceAlphaSaturated;
        case BlendFactor::ColorFactor: return MTL::BlendFactorBlendColor;
        case BlendFactor::InvColorFactor: return MTL::BlendFactorOneMinusBlendColor;
        case BlendFactor::Src1Color: return MTL::BlendFactorSource1Color;
        case BlendFactor::InvSrc1Color: return MTL::BlendFactorOneMinusSource1Color;
        case BlendFactor::Src1Alpha: return MTL::BlendFactorSource1Alpha;
        case BlendFactor::InvSrc1Alpha: return MTL::BlendFactorOneMinusSource1Alpha;
        case BlendFactor::AlphaFactor: return MTL::BlendFactorBlendAlpha;
        case BlendFactor::InvAlphaFactor: return MTL::BlendFactorOneMinusBlendAlpha;
    }
    return std::unexpected("Unsupported Metal BlendFactor");
}

RM::Result<MTL::BlendOperation> RM::Metal::ToBlendOperation(BlendOp operation)
{
    switch (operation)
    {
        case BlendOp::Add: return MTL::BlendOperationAdd;
        case BlendOp::Subtract: return MTL::BlendOperationSubtract;
        case BlendOp::RevSubtract: return MTL::BlendOperationReverseSubtract;
        case BlendOp::Min: return MTL::BlendOperationMin;
        case BlendOp::Max: return MTL::BlendOperationMax;
    }
    return std::unexpected("Unsupported Metal BlendOp");
}

RM::Result<MTL::StencilOperation> RM::Metal::ToStencilOperation(StencilOp operation)
{
    switch (operation)
    {
        case StencilOp::Keep: return MTL::StencilOperationKeep;
        case StencilOp::Zero: return MTL::StencilOperationZero;
        case StencilOp::Replace: return MTL::StencilOperationReplace;
        case StencilOp::IncreaseSat: return MTL::StencilOperationIncrementClamp;
        case StencilOp::DecreaseSat: return MTL::StencilOperationDecrementClamp;
        case StencilOp::Invert: return MTL::StencilOperationInvert;
        case StencilOp::Increment: return MTL::StencilOperationIncrementWrap;
        case StencilOp::Decrement: return MTL::StencilOperationDecrementWrap;
    }
    return std::unexpected("Unsupported Metal StencilOp");
}

RM::Result<NS::SharedPtr<MTL::StencilDescriptor>> RM::Metal::CreateStencilDescriptor(
    const StencilOpState& state, const DepthStencilState& depth)
{
    auto stencil = NS::TransferPtr(MTL::StencilDescriptor::alloc()->init());
    stencil->setStencilCompareFunction(ToCompareFunction(state.StencilFunc));
    auto fail = ToStencilOperation(state.StencilFailOp);
    if (!fail) return std::unexpected(fail.error());
    stencil->setStencilFailureOperation(*fail);
    auto depth_fail = ToStencilOperation(state.StencilDepthFailOp);
    if (!depth_fail) return std::unexpected(depth_fail.error());
    stencil->setDepthFailureOperation(*depth_fail);
    auto pass = ToStencilOperation(state.StencilPassOp);
    if (!pass) return std::unexpected(pass.error());
    stencil->setDepthStencilPassOperation(*pass);
    stencil->setReadMask(depth.stencil_read_mask);
    stencil->setWriteMask(depth.stencil_write_mask);
    return stencil;
}

RM::Result<MTL::PrimitiveTopologyClass> RM::Metal::ToPrimitiveTopologyClass(PolygonMode mode)
{
    switch (mode)
    {
        case PolygonMode::Point: return MTL::PrimitiveTopologyClassPoint;
        case PolygonMode::Line: return MTL::PrimitiveTopologyClassLine;
        case PolygonMode::Triangle: return MTL::PrimitiveTopologyClassTriangle;
    }
    return std::unexpected("Unsupported Metal PolygonMode");
}

RM::Result<MTL::CullMode> RM::Metal::ToCullMode(CullMode mode)
{
    switch (mode)
    {
        case CullMode::Back: return MTL::CullModeBack;
        case CullMode::Front: return MTL::CullModeFront;
        case CullMode::None: return MTL::CullModeNone;
    }
    return std::unexpected("Unsupported Metal CullMode");
}

RM::Result<MTL::Winding> RM::Metal::ToWinding(FrontFace face)
{
    switch (face)
    {
        case FrontFace::Clockwise: return MTL::WindingClockwise;
        case FrontFace::CounterClockwise: return MTL::WindingCounterClockwise;
    }
    return std::unexpected("Unsupported Metal FrontFace");
}

RM::Result<MTL::TriangleFillMode> RM::Metal::ToTriangleFillMode(FillMode mode)
{
    switch (mode)
    {
        case FillMode::Wireframe: return MTL::TriangleFillModeLines;
        case FillMode::Solid: return MTL::TriangleFillModeFill;
    }
    return std::unexpected("Unsupported Metal FillMode");
}

RM::Result<MTL::VertexStepFunction> RM::Metal::ToVertexStepFunction(VertexInputRate rate)
{
    switch (rate)
    {
        case VertexInputRate::Vertex: return MTL::VertexStepFunctionPerVertex;
        case VertexInputRate::Instance: return MTL::VertexStepFunctionPerInstance;
    }
    return std::unexpected("Unsupported Metal VertexInputRate");
}

RM::Result<MTL::VertexFormat> RM::Metal::ToVertexFormat(Format format)
{
    switch (format)
    {
        case Format::R8_UNorm: return MTL::VertexFormatUCharNormalized;
        case Format::R8_SNorm: return MTL::VertexFormatCharNormalized;
        case Format::R8_UInt: return MTL::VertexFormatUChar;
        case Format::R8_SInt: return MTL::VertexFormatChar;
        case Format::R16_UNorm: return MTL::VertexFormatUShortNormalized;
        case Format::R16_SNorm: return MTL::VertexFormatShortNormalized;
        case Format::R16_UInt: return MTL::VertexFormatUShort;
        case Format::R16_SInt: return MTL::VertexFormatShort;
        case Format::R16_Float: return MTL::VertexFormatHalf;
        case Format::R32_UInt: return MTL::VertexFormatUInt;
        case Format::R32_SInt: return MTL::VertexFormatInt;
        case Format::R32_Float: return MTL::VertexFormatFloat;
        case Format::RG8_UNorm: return MTL::VertexFormatUChar2Normalized;
        case Format::RG8_SNorm: return MTL::VertexFormatChar2Normalized;
        case Format::RG8_UInt: return MTL::VertexFormatUChar2;
        case Format::RG8_SInt: return MTL::VertexFormatChar2;
        case Format::RG16_UNorm: return MTL::VertexFormatUShort2Normalized;
        case Format::RG16_SNorm: return MTL::VertexFormatShort2Normalized;
        case Format::RG16_UInt: return MTL::VertexFormatUShort2;
        case Format::RG16_SInt: return MTL::VertexFormatShort2;
        case Format::RG16_Float: return MTL::VertexFormatHalf2;
        case Format::RG32_UInt: return MTL::VertexFormatUInt2;
        case Format::RG32_SInt: return MTL::VertexFormatInt2;
        case Format::RG32_Float: return MTL::VertexFormatFloat2;
        case Format::RGBA8_UNorm: return MTL::VertexFormatUChar4Normalized;
        case Format::RGBA8_SNorm: return MTL::VertexFormatChar4Normalized;
        case Format::RGBA8_UInt: return MTL::VertexFormatUChar4;
        case Format::RGBA8_SInt: return MTL::VertexFormatChar4;
        case Format::RGBA16_UNorm: return MTL::VertexFormatUShort4Normalized;
        case Format::RGBA16_SNorm: return MTL::VertexFormatShort4Normalized;
        case Format::RGBA16_UInt: return MTL::VertexFormatUShort4;
        case Format::RGBA16_SInt: return MTL::VertexFormatShort4;
        case Format::RGBA16_Float: return MTL::VertexFormatHalf4;
        case Format::RGBA32_UInt: return MTL::VertexFormatUInt4;
        case Format::RGBA32_SInt: return MTL::VertexFormatInt4;
        case Format::RGBA32_Float: return MTL::VertexFormatFloat4;
        case Format::BGRA8_UNorm: return MTL::VertexFormatUChar4Normalized_BGRA;
        case Format::RGB10A2_UNorm: return MTL::VertexFormatUInt1010102Normalized;
        case Format::RG11B10_Float: return MTL::VertexFormatFloatRG11B10;
        case Format::RGB9E5_Float: return MTL::VertexFormatFloatRGB9E5;
        default: break;
    }
    return std::unexpected("Unsupported Metal Format");
}

RM::Result<NS::SharedPtr<MTL::VertexDescriptor>> RM::Metal::CreateVertexDescriptor(
    const VertexInputState& input,
    const IRShaderReflection* reflection)
{
    if (!reflection) return std::unexpected("Vertex layout requires shader reflection");
    auto descriptor = NS::TransferPtr(MTL::VertexDescriptor::alloc()->init());
    // Metal has 31 vertex buffer slots and 31 attributes. The converter reserves
    // the first kIRVertexBufferBindPoint buffers and kIRStageInAttributeStartIndex attributes.
    constexpr size_t slot_count = 31;
    std::array<bool, slot_count> bindings{};
    for (const auto& binding : input.bindings)
    {
        if (binding.binding >= slot_count - kIRVertexBufferBindPoint)
            return std::unexpected("Vertex buffer binding exceeds Metal's available slots");
        const auto index = binding.binding + kIRVertexBufferBindPoint;
        if (bindings[index]) return std::unexpected("Duplicate vertex buffer binding");
        bindings[index] = true;
        auto* layout = descriptor->layouts()->object(index);
        layout->setStride(binding.stride);
        auto conversion_0 = ToVertexStepFunction(binding.input_rate);
        if (!conversion_0) return std::unexpected(conversion_0.error());
        layout->setStepFunction(*conversion_0);
        layout->setStepRate(1);
    }

    const auto semantic_name = [](std::string name)
    {
        for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!name.empty() && !std::isdigit(static_cast<unsigned char>(name.back()))) name += '0';
        return name;
    };
    std::vector<std::string> semantics;
    semantics.reserve(input.attributes.size());
    for (const auto& attribute : input.attributes)
    {
        if (attribute.binding >= slot_count - kIRVertexBufferBindPoint ||
            !bindings[attribute.binding + kIRVertexBufferBindPoint])
            return std::unexpected("Vertex attribute references a missing binding");
        auto conversion_1 = ToVertexFormat(attribute.format);
        if (!conversion_1) return std::unexpected(conversion_1.error());

        if (attribute.semantic.empty() && attribute.location >= slot_count - kIRStageInAttributeStartIndex)
            return std::unexpected("Vertex attribute location exceeds Metal's available slots");
        semantics.push_back(semantic_name(attribute.semantic));
    }

    IRVersionedVSInfo vertex_info{};
    if (!IRShaderReflectionCopyVertexInfo(reflection, IRReflectionVersion_1_0, &vertex_info))
        return std::unexpected("Failed to obtain vertex shader inputs");
    const auto release_info = [](IRVersionedVSInfo* info) { IRShaderReflectionReleaseVertexInfo(info); };
    const std::unique_ptr<IRVersionedVSInfo, decltype(release_info)> guard(&vertex_info, release_info);
    std::array<bool, slot_count> attributes{};
    for (size_t i = 0; i < vertex_info.info_1_0.num_vertex_inputs; ++i)
    {
        const auto& shader_input = vertex_info.info_1_0.vertex_inputs[i];
        if (shader_input.attributeIndex >= slot_count - kIRStageInAttributeStartIndex)
            return std::unexpected("Reflected vertex attribute exceeds Metal's available slots");
        const auto name = semantic_name(shader_input.name ? shader_input.name : "");
        const VertexAttribute* match = nullptr;
        for (size_t j = 0; j < input.attributes.size(); ++j)
        {
            const auto& attribute = input.attributes[j];
            // Named HLSL inputs use their semantic; unnamed attributes use location.
            if (semantics[j].empty() ? attribute.location == shader_input.attributeIndex : semantics[j] == name)
            {
                if (match)
                    return std::unexpected("Duplicate vertex attributes for shader input " + name);
                match = &attribute;
            }
        }
        if (!match) return std::unexpected("Missing vertex attribute for shader input " + name);
        const auto index = shader_input.attributeIndex + kIRStageInAttributeStartIndex;
        if (attributes[index]) return std::unexpected("Duplicate reflected vertex attribute");
        attributes[index] = true;
        auto* attribute = descriptor->attributes()->object(index);
        auto conversion_2 = ToVertexFormat(match->format);
        if (!conversion_2) return std::unexpected(conversion_2.error());
        attribute->setFormat(*conversion_2);
        attribute->setOffset(match->offset);
        attribute->setBufferIndex(match->binding + kIRVertexBufferBindPoint);
    }
    return descriptor;
}

MTL::PixelFormat RM::Metal::ToPixelFormat(Format format)
{
    switch (format)
    {
        case Format::A8_UNorm:
            return MTL::PixelFormatA8Unorm;
        case Format::R8_UNorm:
            return MTL::PixelFormatR8Unorm;
        case Format::R8_UNorm_sRGB:
            return MTL::PixelFormatR8Unorm_sRGB;
        case Format::R8_SNorm:
            return MTL::PixelFormatR8Snorm;
        case Format::R8_UInt:
            return MTL::PixelFormatR8Uint;
        case Format::R8_SInt:
            return MTL::PixelFormatR8Sint;
        case Format::R16_UNorm:
            return MTL::PixelFormatR16Unorm;
        case Format::R16_SNorm:
            return MTL::PixelFormatR16Snorm;
        case Format::R16_UInt:
            return MTL::PixelFormatR16Uint;
        case Format::R16_SInt:
            return MTL::PixelFormatR16Sint;
        case Format::R16_Float:
            return MTL::PixelFormatR16Float;
        case Format::RG8_UNorm:
            return MTL::PixelFormatRG8Unorm;
        case Format::RG8_UNorm_sRGB:
            return MTL::PixelFormatRG8Unorm_sRGB;
        case Format::RG8_SNorm:
            return MTL::PixelFormatRG8Snorm;
        case Format::RG8_UInt:
            return MTL::PixelFormatRG8Uint;
        case Format::RG8_SInt:
            return MTL::PixelFormatRG8Sint;
        case Format::B5G6R5_UNorm:
            return MTL::PixelFormatB5G6R5Unorm;
        case Format::A1BGR5_UNorm:
            return MTL::PixelFormatA1BGR5Unorm;
        case Format::ABGR4_UNorm:
            return MTL::PixelFormatABGR4Unorm;
        case Format::BGR5A1_UNorm:
            return MTL::PixelFormatBGR5A1Unorm;
        case Format::R32_UInt:
            return MTL::PixelFormatR32Uint;
        case Format::R32_SInt:
            return MTL::PixelFormatR32Sint;
        case Format::R32_Float:
            return MTL::PixelFormatR32Float;
        case Format::RG16_UNorm:
            return MTL::PixelFormatRG16Unorm;
        case Format::RG16_SNorm:
            return MTL::PixelFormatRG16Snorm;
        case Format::RG16_UInt:
            return MTL::PixelFormatRG16Uint;
        case Format::RG16_SInt:
            return MTL::PixelFormatRG16Sint;
        case Format::RG16_Float:
            return MTL::PixelFormatRG16Float;
        case Format::RGBA8_UNorm:
            return MTL::PixelFormatRGBA8Unorm;
        case Format::RGBA8_UNorm_sRGB:
            return MTL::PixelFormatRGBA8Unorm_sRGB;
        case Format::RGBA8_SNorm:
            return MTL::PixelFormatRGBA8Snorm;
        case Format::RGBA8_UInt:
            return MTL::PixelFormatRGBA8Uint;
        case Format::RGBA8_SInt:
            return MTL::PixelFormatRGBA8Sint;
        case Format::BGRA8_UNorm:
            return MTL::PixelFormatBGRA8Unorm;
        case Format::BGRA8_UNorm_sRGB:
            return MTL::PixelFormatBGRA8Unorm_sRGB;
        case Format::RGB10A2_UNorm:
            return MTL::PixelFormatRGB10A2Unorm;
        case Format::RGB10A2_UInt:
            return MTL::PixelFormatRGB10A2Uint;
        case Format::RG11B10_Float:
            return MTL::PixelFormatRG11B10Float;
        case Format::RGB9E5_Float:
            return MTL::PixelFormatRGB9E5Float;
        case Format::BGR10A2_UNorm:
            return MTL::PixelFormatBGR10A2Unorm;
        case Format::BGR10_XR:
            return MTL::PixelFormatBGR10_XR;
        case Format::BGR10_XR_sRGB:
            return MTL::PixelFormatBGR10_XR_sRGB;
        case Format::BGRA10_XR:
            return MTL::PixelFormatBGRA10_XR;
        case Format::BGRA10_XR_sRGB:
            return MTL::PixelFormatBGRA10_XR_sRGB;
        case Format::RG32_UInt:
            return MTL::PixelFormatRG32Uint;
        case Format::RG32_SInt:
            return MTL::PixelFormatRG32Sint;
        case Format::RG32_Float:
            return MTL::PixelFormatRG32Float;
        case Format::RGBA16_UNorm:
            return MTL::PixelFormatRGBA16Unorm;
        case Format::RGBA16_SNorm:
            return MTL::PixelFormatRGBA16Snorm;
        case Format::RGBA16_UInt:
            return MTL::PixelFormatRGBA16Uint;
        case Format::RGBA16_SInt:
            return MTL::PixelFormatRGBA16Sint;
        case Format::RGBA16_Float:
            return MTL::PixelFormatRGBA16Float;
        case Format::RGBA32_UInt:
            return MTL::PixelFormatRGBA32Uint;
        case Format::RGBA32_SInt:
            return MTL::PixelFormatRGBA32Sint;
        case Format::RGBA32_Float:
            return MTL::PixelFormatRGBA32Float;
        case Format::BC1_RGBA_UNorm:
            return MTL::PixelFormatBC1_RGBA;
        case Format::BC1_RGBA_UNorm_sRGB:
            return MTL::PixelFormatBC1_RGBA_sRGB;
        case Format::BC2_RGBA_UNorm:
            return MTL::PixelFormatBC2_RGBA;
        case Format::BC2_RGBA_UNorm_sRGB:
            return MTL::PixelFormatBC2_RGBA_sRGB;
        case Format::BC3_RGBA_UNorm:
            return MTL::PixelFormatBC3_RGBA;
        case Format::BC3_RGBA_UNorm_sRGB:
            return MTL::PixelFormatBC3_RGBA_sRGB;
        case Format::BC4_R_UNorm:
            return MTL::PixelFormatBC4_RUnorm;
        case Format::BC4_R_SNorm:
            return MTL::PixelFormatBC4_RSnorm;
        case Format::BC5_RG_UNorm:
            return MTL::PixelFormatBC5_RGUnorm;
        case Format::BC5_RG_SNorm:
            return MTL::PixelFormatBC5_RGSnorm;
        case Format::BC6H_RGB_Float:
            return MTL::PixelFormatBC6H_RGBFloat;
        case Format::BC6H_RGB_UFloat:
            return MTL::PixelFormatBC6H_RGBUfloat;
        case Format::BC7_RGBA_UNorm:
            return MTL::PixelFormatBC7_RGBAUnorm;
        case Format::BC7_RGBA_UNorm_sRGB:
            return MTL::PixelFormatBC7_RGBAUnorm_sRGB;
        case Format::Depth16_UNorm:
            return MTL::PixelFormatDepth16Unorm;
        case Format::Depth32_Float:
            return MTL::PixelFormatDepth32Float;
        case Format::Stencil8:
            return MTL::PixelFormatStencil8;
        case Format::Depth24_UNorm_Stencil8:
            return MTL::PixelFormatDepth24Unorm_Stencil8;
        case Format::Depth32_Float_Stencil8:
            return MTL::PixelFormatDepth32Float_Stencil8;
        case Format::Unknown:
            return MTL::PixelFormatInvalid;
    }
    return MTL::PixelFormatInvalid;
}

RM::Format RM::Metal::FromPixelFormat(MTL::PixelFormat format)
{
    switch (format)
    {
        case MTL::PixelFormatInvalid:
            return Format::Unknown;
        case MTL::PixelFormatA8Unorm:
            return Format::A8_UNorm;
        case MTL::PixelFormatR8Unorm:
            return Format::R8_UNorm;
        case MTL::PixelFormatR8Unorm_sRGB:
            return Format::R8_UNorm_sRGB;
        case MTL::PixelFormatR8Snorm:
            return Format::R8_SNorm;
        case MTL::PixelFormatR8Uint:
            return Format::R8_UInt;
        case MTL::PixelFormatR8Sint:
            return Format::R8_SInt;
        case MTL::PixelFormatR16Unorm:
            return Format::R16_UNorm;
        case MTL::PixelFormatR16Snorm:
            return Format::R16_SNorm;
        case MTL::PixelFormatR16Uint:
            return Format::R16_UInt;
        case MTL::PixelFormatR16Sint:
            return Format::R16_SInt;
        case MTL::PixelFormatR16Float:
            return Format::R16_Float;
        case MTL::PixelFormatRG8Unorm:
            return Format::RG8_UNorm;
        case MTL::PixelFormatRG8Unorm_sRGB:
            return Format::RG8_UNorm_sRGB;
        case MTL::PixelFormatRG8Snorm:
            return Format::RG8_SNorm;
        case MTL::PixelFormatRG8Uint:
            return Format::RG8_UInt;
        case MTL::PixelFormatRG8Sint:
            return Format::RG8_SInt;
        case MTL::PixelFormatB5G6R5Unorm:
            return Format::B5G6R5_UNorm;
        case MTL::PixelFormatA1BGR5Unorm:
            return Format::A1BGR5_UNorm;
        case MTL::PixelFormatABGR4Unorm:
            return Format::ABGR4_UNorm;
        case MTL::PixelFormatBGR5A1Unorm:
            return Format::BGR5A1_UNorm;
        case MTL::PixelFormatR32Uint:
            return Format::R32_UInt;
        case MTL::PixelFormatR32Sint:
            return Format::R32_SInt;
        case MTL::PixelFormatR32Float:
            return Format::R32_Float;
        case MTL::PixelFormatRG16Unorm:
            return Format::RG16_UNorm;
        case MTL::PixelFormatRG16Snorm:
            return Format::RG16_SNorm;
        case MTL::PixelFormatRG16Uint:
            return Format::RG16_UInt;
        case MTL::PixelFormatRG16Sint:
            return Format::RG16_SInt;
        case MTL::PixelFormatRG16Float:
            return Format::RG16_Float;
        case MTL::PixelFormatRGBA8Unorm:
            return Format::RGBA8_UNorm;
        case MTL::PixelFormatRGBA8Unorm_sRGB:
            return Format::RGBA8_UNorm_sRGB;
        case MTL::PixelFormatRGBA8Snorm:
            return Format::RGBA8_SNorm;
        case MTL::PixelFormatRGBA8Uint:
            return Format::RGBA8_UInt;
        case MTL::PixelFormatRGBA8Sint:
            return Format::RGBA8_SInt;
        case MTL::PixelFormatBGRA8Unorm:
            return Format::BGRA8_UNorm;
        case MTL::PixelFormatBGRA8Unorm_sRGB:
            return Format::BGRA8_UNorm_sRGB;
        case MTL::PixelFormatRGB10A2Unorm:
            return Format::RGB10A2_UNorm;
        case MTL::PixelFormatRGB10A2Uint:
            return Format::RGB10A2_UInt;
        case MTL::PixelFormatRG11B10Float:
            return Format::RG11B10_Float;
        case MTL::PixelFormatRGB9E5Float:
            return Format::RGB9E5_Float;
        case MTL::PixelFormatBGR10A2Unorm:
            return Format::BGR10A2_UNorm;
        case MTL::PixelFormatBGR10_XR:
            return Format::BGR10_XR;
        case MTL::PixelFormatBGR10_XR_sRGB:
            return Format::BGR10_XR_sRGB;
        case MTL::PixelFormatBGRA10_XR:
            return Format::BGRA10_XR;
        case MTL::PixelFormatBGRA10_XR_sRGB:
            return Format::BGRA10_XR_sRGB;
        case MTL::PixelFormatRG32Uint:
            return Format::RG32_UInt;
        case MTL::PixelFormatRG32Sint:
            return Format::RG32_SInt;
        case MTL::PixelFormatRG32Float:
            return Format::RG32_Float;
        case MTL::PixelFormatRGBA16Unorm:
            return Format::RGBA16_UNorm;
        case MTL::PixelFormatRGBA16Snorm:
            return Format::RGBA16_SNorm;
        case MTL::PixelFormatRGBA16Uint:
            return Format::RGBA16_UInt;
        case MTL::PixelFormatRGBA16Sint:
            return Format::RGBA16_SInt;
        case MTL::PixelFormatRGBA16Float:
            return Format::RGBA16_Float;
        case MTL::PixelFormatRGBA32Uint:
            return Format::RGBA32_UInt;
        case MTL::PixelFormatRGBA32Sint:
            return Format::RGBA32_SInt;
        case MTL::PixelFormatRGBA32Float:
            return Format::RGBA32_Float;
        case MTL::PixelFormatBC1_RGBA:
            return Format::BC1_RGBA_UNorm;
        case MTL::PixelFormatBC1_RGBA_sRGB:
            return Format::BC1_RGBA_UNorm_sRGB;
        case MTL::PixelFormatBC2_RGBA:
            return Format::BC2_RGBA_UNorm;
        case MTL::PixelFormatBC2_RGBA_sRGB:
            return Format::BC2_RGBA_UNorm_sRGB;
        case MTL::PixelFormatBC3_RGBA:
            return Format::BC3_RGBA_UNorm;
        case MTL::PixelFormatBC3_RGBA_sRGB:
            return Format::BC3_RGBA_UNorm_sRGB;
        case MTL::PixelFormatBC4_RUnorm:
            return Format::BC4_R_UNorm;
        case MTL::PixelFormatBC4_RSnorm:
            return Format::BC4_R_SNorm;
        case MTL::PixelFormatBC5_RGUnorm:
            return Format::BC5_RG_UNorm;
        case MTL::PixelFormatBC5_RGSnorm:
            return Format::BC5_RG_SNorm;
        case MTL::PixelFormatBC6H_RGBFloat:
            return Format::BC6H_RGB_Float;
        case MTL::PixelFormatBC6H_RGBUfloat:
            return Format::BC6H_RGB_UFloat;
        case MTL::PixelFormatBC7_RGBAUnorm:
            return Format::BC7_RGBA_UNorm;
        case MTL::PixelFormatBC7_RGBAUnorm_sRGB:
            return Format::BC7_RGBA_UNorm_sRGB;
        case MTL::PixelFormatDepth16Unorm:
            return Format::Depth16_UNorm;
        case MTL::PixelFormatDepth32Float:
            return Format::Depth32_Float;
        case MTL::PixelFormatStencil8:
            return Format::Stencil8;
        case MTL::PixelFormatDepth24Unorm_Stencil8:
            return Format::Depth24_UNorm_Stencil8;
        case MTL::PixelFormatDepth32Float_Stencil8:
            return Format::Depth32_Float_Stencil8;
        default:
            return Format::Unknown;
    }
}

MTL::PrimitiveType RM::Metal::ToPrimitiveType(RM::PrimitiveTopology topology)
{
    switch (topology) {
        case PrimitiveTopology::PointList:
            return MTL::PrimitiveTypePoint;
        case PrimitiveTopology::LineList:
            return MTL::PrimitiveTypeLine;
        case PrimitiveTopology::LineStrip:
            return MTL::PrimitiveTypeLineStrip;
        case PrimitiveTopology::TriangleList:
            return MTL::PrimitiveTypeTriangle;
        case PrimitiveTopology::TriangleStrip:
            return MTL::PrimitiveTypeTriangleStrip;
    }
    return MTL::PrimitiveTypeTriangle;
}

MTL::TextureUsage RM::Metal::ToTextureUsage(EnumFlags<TextureUsage> flags)
{
    MTL::TextureUsage usage = MTL::TextureUsageUnknown;
    if (flags.contains(TextureUsage::ShaderRead))
        usage |= MTL::TextureUsageShaderRead;
    if (flags.contains(TextureUsage::ShaderWrite))
        usage |= MTL::TextureUsageShaderWrite;
    if (flags.contains(TextureUsage::RenderTarget))
        usage |= MTL::TextureUsageRenderTarget;
    return usage;
}

MTL::SamplerMinMagFilter RM::Metal::ToSamplerMinMagFilter(Filter filter)
{
    switch (filter)
    {
        case Filter::Nearest:
        case Filter::NearestMipNearest:
        case Filter::NearestMipLinear:
            return MTL::SamplerMinMagFilterNearest;
        case Filter::Linear:
        case Filter::LinearMipNearest:
        case Filter::LinearMipLinear:
            return MTL::SamplerMinMagFilterLinear;
    }
    return MTL::SamplerMinMagFilterLinear;
}

MTL::SamplerMipFilter RM::Metal::ToSamplerMipFilter(Filter filter)
{
    switch (filter)
    {
        case Filter::Nearest:
        case Filter::Linear:
            return MTL::SamplerMipFilterNotMipmapped;
        case Filter::NearestMipNearest:
        case Filter::LinearMipNearest:
            return MTL::SamplerMipFilterNearest;
        case Filter::NearestMipLinear:
        case Filter::LinearMipLinear:
            return MTL::SamplerMipFilterLinear;
    }
    return MTL::SamplerMipFilterNotMipmapped;
}

MTL::SamplerAddressMode RM::Metal::ToSamplerAddressMode(Wrap wrap)
{
    switch (wrap)
    {
        case Wrap::Repeat:
            return MTL::SamplerAddressModeRepeat;
        case Wrap::MirroredRepeat:
            return MTL::SamplerAddressModeMirrorRepeat;
        case Wrap::ClampToEdge:
            return MTL::SamplerAddressModeClampToEdge;
        case Wrap::Border:
            return MTL::SamplerAddressModeClampToBorderColor;
    }
    return MTL::SamplerAddressModeRepeat;
}

MTL::SamplerBorderColor RM::Metal::ToSamplerBorderColor(BorderColor color)
{
    switch (color)
    {
        case BorderColor::Black:
            return MTL::SamplerBorderColorOpaqueBlack;
        case BorderColor::White:
            return MTL::SamplerBorderColorOpaqueWhite;
        case BorderColor::TransparentBlack:
            return MTL::SamplerBorderColorTransparentBlack;
    }
    return MTL::SamplerBorderColorOpaqueBlack;
}

MTL::CompareFunction RM::Metal::ToCompareFunction(ComparisonFunc function)
{
    switch (function)
    {
        case ComparisonFunc::Always:
            return MTL::CompareFunctionAlways;
        case ComparisonFunc::Never:
            return MTL::CompareFunctionNever;
        case ComparisonFunc::Less:
            return MTL::CompareFunctionLess;
        case ComparisonFunc::LessEqual:
            return MTL::CompareFunctionLessEqual;
        case ComparisonFunc::Greater:
            return MTL::CompareFunctionGreater;
        case ComparisonFunc::GreaterEqual:
            return MTL::CompareFunctionGreaterEqual;
        case ComparisonFunc::Equal:
            return MTL::CompareFunctionEqual;
        case ComparisonFunc::NotEqual:
            return MTL::CompareFunctionNotEqual;
    }
    return MTL::CompareFunctionNever;
}

MTL::SamplerReductionMode RM::Metal::ToSamplerReductionMode(ReductionType type)
{
    switch (type)
    {
        case ReductionType::Standard:
        case ReductionType::Comparison:
            return MTL::SamplerReductionModeWeightedAverage;
        case ReductionType::Minimum:
            return MTL::SamplerReductionModeMinimum;
        case ReductionType::Maximum:
            return MTL::SamplerReductionModeMaximum;
    }
    return MTL::SamplerReductionModeWeightedAverage;
}

bool RM::Metal::SupportsHDR(void* window)
{
    auto ns_window = (__bridge NSWindow*)window;
    NSScreen* screen = ns_window.screen;
    return screen &&
        screen.maximumPotentialExtendedDynamicRangeColorComponentValue > 1.0;
}

RM::DisplayInfo RM::Metal::GetDisplayInfo(void* window)
{
    auto ns_window = (__bridge NSWindow*)window;
    NSScreen* screen = ns_window.screen;
    if (!screen || screen.maximumPotentialExtendedDynamicRangeColorComponentValue <= 1.0)
        return {};
    return {.hdr = true,
            .headroom = std::max(1.f,
                                 static_cast<float>(
                                     screen.maximumExtendedDynamicRangeColorComponentValue)),
            .sdr_white_scale = 1.f};
}

void RM::Metal::ConfigureSwapLayer(CA::MetalLayer* swap_layer, bool hdr)
{
    swap_layer->setPixelFormat(hdr ? MTL::PixelFormat::PixelFormatRGBA16Float
                                   : MTL::PixelFormat::PixelFormatBGRA8Unorm);
    CGColorSpaceRef color_space =
        CGColorSpaceCreateWithName(hdr ? kCGColorSpaceExtendedLinearSRGB : kCGColorSpaceSRGB);
    [(__bridge CAMetalLayer*)swap_layer setColorspace:color_space];
    CGColorSpaceRelease(color_space);
    swap_layer->setWantsExtendedDynamicRangeContent(hdr);
}

void RM::Metal::UpdateSwapLayerScale(CA::MetalLayer* swap_layer, void* window)
{
    auto* ns_window = (__bridge NSWindow*)window;
    [(__bridge CAMetalLayer*)swap_layer setContentsScale:ns_window.backingScaleFactor];
}

NS::SharedPtr<CA::MetalLayer> RM::Metal::CreateSwapLayer(NS::SharedPtr<MTL::Device>& device, const SwapchainCreateInfo& info) {
    auto swap_layer = NS::RetainPtr(CA::MetalLayer::layer());
    auto* ns_window = (NSWindow*)info.window;
    NSView* ns_view = [ns_window contentView];
    [ns_view setWantsLayer:YES];
    [ns_view setLayer:(__bridge CAMetalLayer*)swap_layer.get()];
    swap_layer->setDevice(device.get());
    ConfigureSwapLayer(swap_layer.get(), SupportsHDR(info.window));
    UpdateSwapLayerScale(swap_layer.get(), info.window);
    swap_layer->setFramebufferOnly(false);
    swap_layer->setDrawableSize(CGSizeMake((float)info.width, (float)info.height));
    return swap_layer;
}

MTL::Stages RM::Metal::ToBarrierStages(RM::BarrierStage stages)
{
    const RM::EnumFlags flags(stages);
    MTL::Stages result = 0;
    if (flags.intersects(RM::BarrierStage::VertexShading) ||
        flags.intersects(RM::BarrierStage::IndexInput) ||
        flags.intersects(RM::BarrierStage::ExecuteIndirect))
        result |= MTL::StageVertex;
    if (flags.intersects(RM::BarrierStage::PixelShading) ||
        flags.intersects(RM::BarrierStage::RenderTarget) ||
        flags.intersects(RM::BarrierStage::DepthStencil))
        result |= MTL::StageFragment;
    if (flags.intersects(RM::BarrierStage::ComputeShading) ||
        flags.intersects(RM::BarrierStage::ExecuteIndirect))
        result |= MTL::StageDispatch;
    if (flags.intersects(RM::BarrierStage::Copy)) result |= MTL::StageBlit;
    if (flags.intersects(RM::BarrierStage::AmplificationShading) ||
        flags.intersects(RM::BarrierStage::ExecuteIndirect))
        result |= MTL::StageObject;
    if (flags.intersects(RM::BarrierStage::MeshShading) ||
        flags.intersects(RM::BarrierStage::ExecuteIndirect))
        result |= MTL::StageMesh;
    if (flags.intersects(RM::BarrierStage::AccelerationStructureBuild) ||
        flags.intersects(RM::BarrierStage::AccelerationStructureCopy))
        result |= MTL::StageAccelerationStructure;
    return result;
}

MTL::StorageMode RM::Metal::ToStorageMode(MemoryType memory)
{
    switch (memory)
    {
        case MemoryType::DeviceLocal:
            return MTL::StorageModePrivate;
        case MemoryType::HostVisible:
            return MTL::StorageModeShared;
    }
    return MTL::StorageModePrivate;
}

NS::SharedPtr<MTL::TextureDescriptor> RM::Metal::CreateTextureDescriptor(
    const TextureCreateInfo& info)
{
    auto descriptor = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
    descriptor->setPixelFormat(ToPixelFormat(info.format));
    descriptor->setHeight(info.height);
    descriptor->setWidth(info.width);
    descriptor->setAllowGPUOptimizedContents(true);
    descriptor->setArrayLength(info.array_layers);
    descriptor->setMipmapLevelCount(info.mip_levels);
    descriptor->setTextureType(info.array_layers > 1 ? MTL::TextureType2DArray
                                                     : MTL::TextureType2D);
    auto usage = ToTextureUsage(info.usage);
    usage |= MTL::TextureUsagePixelFormatView;
    descriptor->setUsage(usage);
    return descriptor;
}
