#pragma once
#include "dxcapi.h"
#include "rimera_shader.hpp"

namespace RM
{
    class Device;

    namespace Helpers
    {
        Pipeline* CreateBlitShader(Device* device);
        void CheckDxc(HRESULT result, const char* operation);
        ComPtr<IDxcBlob> CompileHlsl(std::string_view source,
                                     std::string_view entry_point,
                                     ShaderType type);
        [[nodiscard]] bool IsDepthStencilFormat(Format format) noexcept;
    };
} // namespace RM