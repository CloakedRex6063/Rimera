#pragma once
#include "rimera_device.hpp"

namespace RM::Metal
{
    MTL::PixelFormat ToPixelFormat(Format format);
    MTL::TextureUsage ToTextureUsage(EnumFlags<TextureUsage> flags);
    bool SupportsHDR(void* window);
    NS::SharedPtr<CA::MetalLayer> CreateSwapLayer(NS::SharedPtr<MTL::Device>& device,
                                                  const DeviceCreateInfo& info);
}  // namespace RM::Metal
