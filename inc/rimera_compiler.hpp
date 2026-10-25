#pragma once
#include "rimera_structs.hpp"
#include "cstddef"
#include "span"
#include "string"
#include "string_view"
#include "vector"

namespace RM
{
    struct ShaderDefine
    {
        std::string_view name;
        std::string_view value;
    };

    struct ShaderModel
    {
        uint32_t major = 6;
        uint32_t minor = 8;
    };

    struct CompilerCreateInfo
    {
        std::span<const std::string_view> include_paths;
        std::span<const ShaderDefine> defines;
        std::span<const std::string_view> arguments;
        ShaderModel shader_model;
        uint32_t hlsl_version = 2021;
        bool enable_16bit_types = true;
    };

    class Shader
    {
    public:
        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;

        [[nodiscard]] const void* GetBytecode() const noexcept { return m_bytecode.data(); }
        [[nodiscard]] size_t GetBytecodeSize() const noexcept { return m_bytecode.size(); }
        [[nodiscard]] ShaderType GetType() const noexcept { return m_type; }
        [[nodiscard]] std::string_view GetEntryPoint() const noexcept { return m_entry_point; }

    private:
        Shader() = default;
        friend class Compiler;
        std::vector<std::byte> m_bytecode;
        ShaderType m_type = ShaderType::Compute;
        std::string m_entry_point;
    };

    class Compiler
    {
    public:
        Compiler(const Compiler&) = delete;
        Compiler& operator=(const Compiler&) = delete;
        ~Compiler();

        [[nodiscard]] Result<Shader*> Compile(const ShaderCreateInfo& info, ShaderType type) const;

    private:
        struct Data;
        Compiler() = default;
        friend Result<Compiler*> CreateCompiler(const CompilerCreateInfo& info);
        [[nodiscard]] Result<void> Initialize(const CompilerCreateInfo& info);
        Data* m_data = nullptr;
    };

    [[nodiscard]] Result<Compiler*> CreateCompiler(const CompilerCreateInfo& info = {});
    void DestroyCompiler(const Compiler* compiler);
    void DestroyShader(const Shader* shader);
} // namespace RM
