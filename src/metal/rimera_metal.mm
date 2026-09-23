#include "Metal/Metal.h"
#include "QuartzCore/QuartzCore.h"
#include "Foundation/Foundation.h"
#include "AppKit/AppKit.h"
#include "rimera_metal.hpp"

MTL::PixelFormat RM::Metal::ToPixelFormat(Format format)
{
    switch (format)
    {
        case Format::Invalid:
            return MTL::PixelFormatInvalid;
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
    }
    return MTL::PixelFormatInvalid;
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

bool RM::Metal::SupportsHDR(void* window)
{
    auto ns_window = (__bridge NSWindow*)window;
    NSScreen* screen = ns_window.screen;
    return screen &&
        screen.maximumPotentialExtendedDynamicRangeColorComponentValue > 1.0;
}

NS::SharedPtr<CA::MetalLayer> RM::Metal::CreateSwapLayer(NS::SharedPtr<MTL::Device>& device, const DeviceCreateInfo& info) {
    auto swap_layer = NS::TransferPtr(CA::MetalLayer::layer());
    auto* ns_window = (NSWindow*)info.window;
    NSView* ns_view = [ns_window contentView];
    [ns_view setWantsLayer:YES];
    [ns_view setLayer:(__bridge CAMetalLayer*)swap_layer.get()];
    bool supports_hdr = SupportsHDR(info.window);
    swap_layer->setDevice(device.get());
    swap_layer->setPixelFormat(supports_hdr ? MTL::PixelFormat::PixelFormatBGRA10_XR_sRGB : MTL::PixelFormat::PixelFormatBGRA8Unorm);
    swap_layer->setWantsExtendedDynamicRangeContent(supports_hdr);
    swap_layer->setFramebufferOnly(false);
    swap_layer->setDrawableSize(CGSizeMake((float)info.width, (float)info.height));
    return swap_layer;
}