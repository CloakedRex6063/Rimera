#pragma once
#include "rimera_shader.hpp"

namespace RM
{
    class Device;
    namespace Helpers
    {
        Shader* CreateBlitShader(Device* device);
    };
}  // namespace RM
