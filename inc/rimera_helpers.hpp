#pragma once
#include "dxc/dxcapi.h"
#include "rimera_shader.hpp"

namespace RM
{
    class Device;

    namespace Helpers
    {
        struct FormatBlockInfo
        {
            uint32_t bytes = 0;
            uint32_t width = 1;
            uint32_t height = 1;
        };

        [[nodiscard]] FormatBlockInfo GetFormatBlockInfo(Format format) noexcept;
        [[nodiscard]] Result<Pipeline*> CreateBlitShader(Device* device, Format format);
        [[nodiscard]] Result<void> CheckDxc(HRESULT result, const char* operation);
        [[nodiscard]] bool IsDepthStencilFormat(Format format) noexcept;
        bool IsEmpty(const Rect& r);
    };
} // namespace RM