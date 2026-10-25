#include "rimera_compiler.hpp"
#include "rimera_helpers.hpp"
#include "dxc/dxcapi.h"
#include "cstring"
#include "limits"

#ifdef RIMERA_MAC
template <typename T>
using ComPtr = CComPtr<T>;
#endif

namespace
{
    std::wstring_view GetProfilePrefix(RM::ShaderType type)
    {
        switch (type)
        {
            case RM::ShaderType::Compute:
                return L"cs";
            case RM::ShaderType::Vertex:
                return L"vs";
            case RM::ShaderType::Fragment:
                return L"ps";
            case RM::ShaderType::Mesh:
                return L"ms";
            case RM::ShaderType::Amplification:
                return L"as";
        }
        return {};
    }

    std::string_view GetStageName(RM::ShaderType type)
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
} // namespace

struct RM::Compiler::Data
{
    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    ComPtr<IDxcIncludeHandler> include_handler;
    IDxcUtils* raw_utils = nullptr;
    IDxcCompiler3* raw_compiler = nullptr;
    IDxcIncludeHandler* raw_include_handler = nullptr;
    ShaderModel shader_model;
    std::vector<std::wstring> arguments;

    [[nodiscard]] Result<std::wstring> ToWide(std::string_view text) const
    {
        if (text.size() > std::numeric_limits<UINT32>::max())
            return std::unexpected("Compiler argument is too long");
        IDxcBlobEncoding* raw_utf8 = nullptr;
        if (auto result = Helpers::CheckDxc(raw_utils->CreateBlob(text.data(),
                                                                  static_cast<UINT32>(text.size()),
                                                                  DXC_CP_UTF8,
                                                                  &raw_utf8),
                                            "Failed to copy compiler argument");
            !result)
            return std::unexpected(result.error());
        ComPtr<IDxcBlobEncoding> utf8;
        utf8.Attach(raw_utf8);
        IDxcBlobWide* raw_wide = nullptr;
        if (auto result = Helpers::CheckDxc(raw_utils->GetBlobAsWide(raw_utf8, &raw_wide),
                                            "Failed to convert compiler argument");
            !result)
            return std::unexpected(result.error());
        ComPtr<IDxcBlobWide> wide;
        wide.Attach(raw_wide);
        return std::wstring(raw_wide->GetStringPointer(), raw_wide->GetStringLength());
    }
};

RM::Compiler::~Compiler()
{
    delete m_data;
}

RM::Result<void> RM::Compiler::Initialize(const CompilerCreateInfo& info)
{
    if (info.shader_model.major < 6) return std::unexpected("Shader model must be 6.0 or newer");
    m_data = new (std::nothrow) Data();
    if (!m_data) return std::unexpected("Object allocation failed");
    auto& data = *m_data;
    data.shader_model = info.shader_model;

    IDxcUtils* raw_utils = nullptr;
    if (auto result = Helpers::CheckDxc(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&raw_utils)),
                                        "Failed to create DXC utilities");
        !result)
        return std::unexpected(result.error());
    data.utils.Attach(raw_utils);
    data.raw_utils = raw_utils;

    IDxcCompiler3* raw_compiler = nullptr;
    if (auto result =
            Helpers::CheckDxc(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&raw_compiler)),
                              "Failed to create HLSL compiler");
        !result)
        return std::unexpected(result.error());
    data.compiler.Attach(raw_compiler);
    data.raw_compiler = raw_compiler;

    IDxcIncludeHandler* raw_include_handler = nullptr;
    if (auto result = Helpers::CheckDxc(raw_utils->CreateDefaultIncludeHandler(&raw_include_handler),
                                        "Failed to create include handler");
        !result)
        return std::unexpected(result.error());
    data.include_handler.Attach(raw_include_handler);
    data.raw_include_handler = raw_include_handler;

    data.arguments.push_back(L"-HV");
    data.arguments.push_back(std::to_wstring(info.hlsl_version));
    if (info.enable_16bit_types) data.arguments.push_back(L"-enable-16bit-types");
#ifdef RIMERA_VULKAN
    for (const auto* argument : {L"-spirv",
                                 L"-fspv-target-env=vulkan1.3",
                                 L"-fvk-use-dx-layout",
                                 L"-fvk-bind-resource-heap",
                                 L"0",
                                 L"1",
                                 L"-fvk-bind-sampler-heap",
                                 L"0",
                                 L"2",
                                 L"-fspv-extension=SPV_EXT_mesh_shader",
                                 L"-fspv-extension=SPV_EXT_descriptor_indexing"})
        data.arguments.push_back(argument);
#endif

    for (const auto path : info.include_paths)
    {
        if (path.empty()) return std::unexpected("Include path must not be empty");
        auto wide = data.ToWide(path);
        if (!wide) return std::unexpected(wide.error());
        data.arguments.push_back(L"-I");
        data.arguments.push_back(std::move(*wide));
    }
    for (const auto& define : info.defines)
    {
        if (define.name.empty()) return std::unexpected("Shader define must have a name");
        std::string text(define.name);
        if (!define.value.empty())
        {
            text += '=';
            text += define.value;
        }
        auto wide = data.ToWide(text);
        if (!wide) return std::unexpected(wide.error());
        data.arguments.push_back(L"-D");
        data.arguments.push_back(std::move(*wide));
    }
    for (const auto argument : info.arguments)
    {
        auto wide = data.ToWide(argument);
        if (!wide) return std::unexpected(wide.error());
        data.arguments.push_back(std::move(*wide));
    }
    return {};
}

RM::Result<RM::Shader*> RM::Compiler::Compile(const ShaderCreateInfo& info, ShaderType type) const
{
    const auto& data = *m_data;
    const auto prefix = GetProfilePrefix(type);
    if (prefix.empty()) return std::unexpected("Invalid shader type");
    if (info.code.empty()) return std::unexpected("HLSL source must not be empty");
    const std::string entry_point(info.entry_point.empty() ? "main" : info.entry_point);
    if (entry_point.find('\0') != std::string::npos)
        return std::unexpected("Invalid HLSL entry point");

    auto entry_wide = data.ToWide(entry_point);
    if (!entry_wide) return std::unexpected(entry_wide.error());
    const std::wstring profile = std::wstring(prefix) + L"_" +
                                 std::to_wstring(data.shader_model.major) + L"_" +
                                 std::to_wstring(data.shader_model.minor);

    std::vector<const wchar_t*> arguments{L"-E", entry_wide->c_str(), L"-T", profile.c_str()};
    for (const auto& argument : data.arguments) arguments.push_back(argument.c_str());

    const DxcBuffer buffer{info.code.data(), info.code.size(), DXC_CP_UTF8};
    IDxcResult* raw_result = nullptr;
    if (auto result = Helpers::CheckDxc(data.raw_compiler->Compile(&buffer,
                                                                   arguments.data(),
                                                                   static_cast<UINT32>(arguments.size()),
                                                                   data.raw_include_handler,
                                                                   IID_PPV_ARGS(&raw_result)),
                                        "Failed to invoke HLSL compiler");
        !result)
        return std::unexpected(result.error());
    ComPtr<IDxcResult> compile_result;
    compile_result.Attach(raw_result);

    HRESULT status = S_OK;
    if (auto result = Helpers::CheckDxc(raw_result->GetStatus(&status),
                                        "Failed to obtain HLSL compilation status");
        !result)
        return std::unexpected(result.error());
    if (FAILED(status))
    {
        IDxcBlobUtf8* raw_diagnostics = nullptr;
        if (auto result = Helpers::CheckDxc(raw_result->GetOutput(DXC_OUT_ERRORS,
                                                                  IID_PPV_ARGS(&raw_diagnostics),
                                                                  nullptr),
                                            "Failed to obtain HLSL diagnostics");
            !result)
            return std::unexpected(result.error());
        ComPtr<IDxcBlobUtf8> diagnostics;
        diagnostics.Attach(raw_diagnostics);
        const std::string message =
            raw_diagnostics && raw_diagnostics->GetStringLength()
                ? std::string(raw_diagnostics->GetStringPointer(),
                              raw_diagnostics->GetStringLength())
                : "DXC returned no diagnostics";
        return std::unexpected("HLSL " + std::string(GetStageName(type)) +
                               " shader compilation failed: " + message);
    }

    IDxcBlob* raw_dxil = nullptr;
    if (auto result = Helpers::CheckDxc(
            raw_result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&raw_dxil), nullptr),
            "Failed to obtain compiled DXIL");
        !result)
        return std::unexpected(result.error());
    ComPtr<IDxcBlob> dxil;
    dxil.Attach(raw_dxil);
    if (!raw_dxil || !raw_dxil->GetBufferSize())
        return std::unexpected("HLSL compiler returned empty DXIL");

    auto* shader = new (std::nothrow) Shader();
    if (!shader) return std::unexpected("Object allocation failed");
    shader->m_bytecode.resize(raw_dxil->GetBufferSize());
    std::memcpy(shader->m_bytecode.data(), raw_dxil->GetBufferPointer(), shader->m_bytecode.size());
    shader->m_type = type;
    shader->m_entry_point = entry_point;
    return shader;
}

RM::Result<RM::Compiler*> RM::CreateCompiler(const CompilerCreateInfo& info)
{
    auto* compiler = new (std::nothrow) Compiler();
    if (!compiler) return std::unexpected("Object allocation failed");
    if (auto result = compiler->Initialize(info); !result)
    {
        auto error = std::move(result.error());
        delete compiler;
        return std::unexpected(std::move(error));
    }
    return compiler;
}

void RM::DestroyCompiler(const Compiler* compiler)
{
    delete compiler;
}

void RM::DestroyShader(const Shader* shader)
{
    delete shader;
}
