#include "rimera_shader.hpp"
#include "dxc/dxcapi.h"
#define IR_RUNTIME_METALCPP
#define IR_PRIVATE_IMPLEMENTATION
#include "metal_irconverter/metal_irconverter.h"
#include "limits"
#include "rimera_device.hpp"
#include "stdexcept"
#include "string"

namespace
{
    void CheckDxc(HRESULT result, const char* operation)
    {
        if (FAILED(result))
            throw std::runtime_error(std::string(operation) + " (HRESULT " +
                                     std::to_string(result) + ")");
    }

    CComPtr<IDxcBlob> CompileHlsl(std::string_view source, std::string_view entry_point)
    {
        CComPtr<IDxcUtils> utils;
        CheckDxc(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)),
                 "Failed to create DXC utilities");
        CComPtr<IDxcCompiler3> compiler;
        CheckDxc(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)),
                 "Failed to create HLSL compiler");

        if (entry_point.size() > std::numeric_limits<UINT32>::max() ||
            entry_point.find('\0') != std::string_view::npos)
            throw std::invalid_argument("Invalid HLSL entry point");

        CComPtr<IDxcBlobEncoding> entry_utf8;
        CheckDxc(utils->CreateBlob(entry_point.data(),
                                   static_cast<UINT32>(entry_point.size()),
                                   DXC_CP_UTF8,
                                   &entry_utf8),
                 "Failed to copy HLSL entry point");
        CComPtr<IDxcBlobWide> entry_wide;
        CheckDxc(utils->GetBlobAsWide(entry_utf8, &entry_wide),
                 "Failed to convert HLSL entry point");

        const wchar_t* arguments[] = {
            L"-E",
            entry_wide->GetStringPointer(),
            L"-T",
            L"cs_6_6",
            L"-HV",
            L"2021",
        };
        const DxcBuffer buffer{source.data(), source.size(), DXC_CP_UTF8};
        CComPtr<IDxcResult> result;
        CheckDxc(compiler->Compile(&buffer,
                                   arguments,
                                   std::size(arguments),
                                   nullptr,
                                   IID_PPV_ARGS(&result)),
                 "Failed to invoke HLSL compiler");

        CComPtr<IDxcBlobUtf8> diagnostics;
        CheckDxc(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&diagnostics), nullptr),
                 "Failed to obtain HLSL diagnostics");
        HRESULT status = S_OK;
        CheckDxc(result->GetStatus(&status), "Failed to obtain HLSL compilation status");
        if (FAILED(status))
        {
            const std::string message =
                diagnostics && diagnostics->GetStringLength()
                    ? std::string(diagnostics->GetStringPointer(), diagnostics->GetStringLength())
                    : "DXC returned no diagnostics";
            throw std::runtime_error("HLSL compute shader compilation failed: " + message);
        }

        CComPtr<IDxcBlob> dxil;
        CheckDxc(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&dxil), nullptr),
                 "Failed to obtain compiled DXIL");
        if (!dxil || !dxil->GetBufferSize())
            throw std::runtime_error("HLSL compiler returned empty DXIL");
        return dxil;
    }

    std::runtime_error MetalError(const char* operation, const NS::Error* error)
    {
        const auto* description = error ? error->localizedDescription() : nullptr;
        const char* message = description ? description->utf8String() : nullptr;
        return std::runtime_error(std::string(operation) + ": " +
                                  (message ? message : "Metal returned no error description"));
    }
}  // namespace

void* RM::Shader::GetNativeHandle() const
{
    switch (m_type)
    {
        case ShaderType::eCompute:
            return m_compute_pso.get();
        case ShaderType::eRender:
            return m_render_pso.get();
    }
    return m_render_pso.get();
}

RM::Shader::Shader(Device* device, const ComputeShaderCreateInfo& info)
    : device(device), m_type(ShaderType::eCompute)
{
    if (!device) throw std::invalid_argument("Compute shader creation requires a Metal device");
    if (info.code.empty()) throw std::invalid_argument("Compute shader HLSL must not be empty");

    auto* native_device = static_cast<MTL::Device*>(device->GetNativeHandle());

    const std::string entry_point(info.entry_point.empty() ? "main" : info.entry_point);

    auto dxil_blob = CompileHlsl(info.code, entry_point);
    IRCompiler* compiler = nullptr;
    IRObject* dxil = nullptr;
    IRObject* ir = nullptr;
    IRError* error = nullptr;
    IRShaderReflection* reflection = nullptr;
    IRMetalLibBinary* binary = nullptr;

    try
    {
        compiler = IRCompilerCreate();
        if (!compiler) throw std::runtime_error("Failed to create Metal shader converter compiler");
        IRCompilerSetEntryPointName(compiler, entry_point.c_str());

        dxil = IRObjectCreateFromDXIL(static_cast<const uint8_t*>(dxil_blob->GetBufferPointer()),
                                      dxil_blob->GetBufferSize(),
                                      IRBytecodeOwnershipNone);
        if (!dxil) throw std::runtime_error("Failed to create DXIL object");

        const auto* root_sig = static_cast<IRRootSignature*>(device->GetNativeRootSignature());
        IRCompilerSetGlobalRootSignature(compiler, root_sig);

        ir = IRCompilerAllocCompileAndLink(compiler, nullptr, dxil, &error);
        if (!ir)
            throw std::runtime_error(
                "DXIL compute shader conversion failed (converter error " +
                (error ? std::to_string(IRErrorGetCode(error)) : "unavailable") + ")");
        if (IRObjectGetMetalIRShaderStage(ir) != IRShaderStageCompute)
            throw std::runtime_error("DXIL must contain a compute shader");

        reflection = IRShaderReflectionCreate();
        if (!reflection) throw std::runtime_error("Failed to create compute shader reflection");
        if (!IRObjectGetReflection(ir, IRShaderStageCompute, reflection))
            throw std::runtime_error("Failed to obtain compute shader reflection");

        IRVersionedCSInfo compute_info{};
        if (!IRShaderReflectionCopyComputeInfo(reflection, IRReflectionVersion_1_0, &compute_info))
            throw std::runtime_error("Failed to obtain compute shader threadgroup size");
        const DispatchGroup group{compute_info.info_1_0.tg_size[0],
                                  compute_info.info_1_0.tg_size[1],
                                  compute_info.info_1_0.tg_size[2]};
        IRShaderReflectionReleaseComputeInfo(&compute_info);
        if (!group.x || !group.y || !group.z)
            throw std::runtime_error("Compute shader has an invalid threadgroup size");

        const char* metal_entry_point = IRShaderReflectionGetEntryPointFunctionName(reflection);
        if (!metal_entry_point || !*metal_entry_point)
            throw std::runtime_error("Compute shader reflection has no entry point");

        binary = IRMetalLibBinaryCreate();
        if (!binary) throw std::runtime_error("Failed to create compute shader metallib object");
        if (!IRObjectGetMetalLibBinary(ir, IRShaderStageCompute, binary))
            throw std::runtime_error("Failed to extract compute shader metallib");

        const auto bytecode = IRMetalLibGetBytecodeData(binary);
        if (!bytecode || !IRMetalLibGetBytecodeSize(binary))
            throw std::runtime_error("Compute shader metallib is empty");

        auto pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        NS::Error* metal_error = nullptr;
        auto library = NS::TransferPtr(native_device->newLibrary(bytecode, &metal_error));
        if (!library) throw MetalError("Failed to load compute shader metallib", metal_error);
        auto function = NS::TransferPtr(
            library->newFunction(NS::String::string(metal_entry_point, NS::UTF8StringEncoding)));
        if (!function)
            throw std::runtime_error(std::string("Metal compute entry point not found: ") +
                                     metal_entry_point);

        metal_error = nullptr;
        auto pipeline =
            NS::TransferPtr(native_device->newComputePipelineState(function.get(), &metal_error));
        if (!pipeline) throw MetalError("Failed to create compute pipeline", metal_error);

        m_compute_pso = std::move(pipeline);
        m_compute_dispatch_group = group;
    }
    catch (...)
    {
        if (binary) IRMetalLibBinaryDestroy(binary);
        if (reflection) IRShaderReflectionDestroy(reflection);
        if (ir) IRObjectDestroy(ir);
        if (error) IRErrorDestroy(error);
        if (dxil) IRObjectDestroy(dxil);
        if (compiler) IRCompilerDestroy(compiler);
        throw;
    }

    IRMetalLibBinaryDestroy(binary);
    IRShaderReflectionDestroy(reflection);
    IRObjectDestroy(ir);
    if (error) IRErrorDestroy(error);
    IRObjectDestroy(dxil);
    IRCompilerDestroy(compiler);
}
