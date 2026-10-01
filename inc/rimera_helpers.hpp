#pragma once
#include "dxc/dxcapi.h"
#include "rimera_shader.hpp"

namespace RM
{
    class Device;

    namespace Helpers
    {
        Pipeline* CreateBlitShader(Device* device, Format format);
        void CheckDxc(HRESULT result, const char* operation);
        IDxcBlob* CompileHlsl(std::string_view source,
                              std::string_view entry_point,
                              ShaderType type);
        [[nodiscard]] bool IsDepthStencilFormat(Format format) noexcept;
        bool IsEmpty(const Rect& r);
    };
} // namespace RM