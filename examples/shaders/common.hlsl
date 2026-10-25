#pragma once

struct Transform
{
    column_major float4x4 world;
    column_major float4x4 normal;
};

struct DrawData
{
    uint meshlet_index;
    uint position_index;
    uint normal_index;
    uint uv_index;
    uint tangent_index;
    uint vertex_indices_index;
    uint triangle_index;
    uint meshlet_buffer_index;
    uint transform_index;
    uint material_index;
};

struct Meshlet
{
    uint vertex_offset;
    uint triangle_offset;
    uint vertex_count;
    uint triangle_count;
};

struct Material
{
    float4 base_color;
    float3 emissive;
    float metallic;
    float roughness;
    float normal_scale;
    float occlusion_strength;
    float alpha_cutoff;
    uint16_t textures[5];
    uint16_t flags;
};

static const uint TEXTURE_SLOT_BASE_COLOR = 0;
static const uint TEXTURE_SLOT_METALLIC_ROUGHNESS = 1;
static const uint TEXTURE_SLOT_NORMAL = 2;
static const uint TEXTURE_SLOT_OCCLUSION = 3;
static const uint TEXTURE_SLOT_EMISSIVE = 4;
static const uint TEXTURE_SLOT_COUNT = 5;
static const uint TEXTURE_NONE = 0xffff;

bool HasTexture(Material material, uint slot)
{
    return material.textures[slot] != TEXTURE_NONE;
}

cbuffer Scene : register(b0)
{
    uint draw_buffer_index;
    uint transform_buffer_index;
    uint material_buffer_index;
    uint texture_buffer_index;
    uint sampler_index;
    uint3 padding;
    column_major float4x4 view_projection;
}

uint LoadByte(ByteAddressBuffer buffer, uint offset)
{
    return (buffer.Load(offset & ~3u) >> ((offset & 3u) * 8u)) & 0xffu;
}

struct TangentFrame
{
    float3 normal;
    float3 tangent;
    float3 bitangent;
};

uint PackDirection10(float3 direction)
{
    int3 quantized = int3(round(clamp(direction, -1.0, 1.0) * 511.0)) & 0x3ff;
    return uint(quantized.x) | (uint(quantized.y) << 10) | (uint(quantized.z) << 20);
}

float3 UnpackDirection10(uint packed)
{
    int3 quantized = asint(uint3(packed << 22, packed << 12, packed << 2)) >> 22;
    return max(float3(quantized) / 511.0, -1.0);
}

uint PackTangent(float3 tangent, float handedness)
{
    return PackDirection10(tangent) | ((handedness < 0.0 ? 3u : 1u) << 30);
}

float4 UnpackTangent(uint packed)
{
    float handedness = (packed >> 30) == 3u ? -1.0 : 1.0;
    return float4(UnpackDirection10(packed), handedness);
}

uint PackUV(float2 uv)
{
    return f32tof16(uv.x) | (f32tof16(uv.y) << 16);
}

float2 UnpackUV(uint packed)
{
    return float2(f16tof32(packed & 0xffffu), f16tof32(packed >> 16));
}

float2 OctEncode(float3 direction)
{
    direction /= abs(direction.x) + abs(direction.y) + abs(direction.z);
    float2 encoded = direction.xy;
    if (direction.z < 0.0)
    {
        float2 sign_xy = select(encoded >= 0.0, float2(1.0, 1.0), float2(-1.0, -1.0));
        encoded = (1.0 - abs(encoded.yx)) * sign_xy;
    }
    return encoded;
}

float3 OctDecode(float2 encoded)
{
    float3 direction = float3(encoded.x, encoded.y, 1.0 - abs(encoded.x) - abs(encoded.y));
    float fold = saturate(-direction.z);
    direction.xy += select(direction.xy >= 0.0, float2(-fold, -fold), float2(fold, fold));
    return normalize(direction);
}

uint Quantize16(float value)
{
    return uint(round(saturate(value * 0.5 + 0.5) * 65535.0));
}

float Dequantize16(uint value)
{
    return float(value) * (2.0 / 65535.0) - 1.0;
}

uint4 PackTangentFrame(float3 normal, float3 tangent, float handedness)
{
    float2 encoded_normal = OctEncode(normal);
    float2 encoded_tangent = OctEncode(tangent);
    uint4 packed = uint4(Quantize16(encoded_normal.x),
                         Quantize16(encoded_normal.y),
                         Quantize16(encoded_tangent.x),
                         Quantize16(encoded_tangent.y));
    packed.w = (packed.w & ~1u) | (handedness < 0.0 ? 1u : 0u);
    return packed;
}

TangentFrame UnpackTangentFrame(uint4 packed)
{
    TangentFrame frame;
    frame.normal = OctDecode(float2(Dequantize16(packed.x), Dequantize16(packed.y)));
    frame.tangent = OctDecode(float2(Dequantize16(packed.z), Dequantize16(packed.w)));
    float handedness = (packed.w & 1u) != 0u ? -1.0 : 1.0;
    frame.bitangent = cross(frame.normal, frame.tangent) * handedness;
    return frame;
}

uint GetTextureDescriptor(Material material, uint slot)
{
    StructuredBuffer<uint> texture_table = ResourceDescriptorHeap[texture_buffer_index];
    return texture_table[material.textures[slot]];
}

float4 SampleBaseColorTexture(Material material, float2 uv, float2 uv_dx, float2 uv_dy)
{
    float4 base_color;
    if (HasTexture(material, TEXTURE_SLOT_BASE_COLOR))
    {
        Texture2D<float4> texture =
            ResourceDescriptorHeap[NonUniformResourceIndex(GetTextureDescriptor(material, TEXTURE_SLOT_BASE_COLOR))];
        SamplerState texture_sampler = SamplerDescriptorHeap[sampler_index];
        base_color = texture.SampleGrad(texture_sampler, uv, uv_dx, uv_dy);
    }
    else
    {
        base_color = float4(1.0, 1.0, 1.0, 1.0);
    }
    return base_color;
}

float4 SampleMetallicRoughnessTexture(Material material, float2 uv, float2 uv_dx, float2 uv_dy)
{
    float4 metallic_roughness;
    if (HasTexture(material, TEXTURE_SLOT_METALLIC_ROUGHNESS))
    {
        Texture2D<float4> texture =
            ResourceDescriptorHeap[NonUniformResourceIndex(GetTextureDescriptor(material, TEXTURE_SLOT_METALLIC_ROUGHNESS))];
        SamplerState texture_sampler = SamplerDescriptorHeap[sampler_index];
        metallic_roughness = texture.SampleGrad(texture_sampler, uv, uv_dx, uv_dy);
    }
    else
    {
        metallic_roughness = float4(1.0, 1.0, 1.0, 1.0);
    }
    return metallic_roughness;
}

float3 SampleNormalTexture(Material material, float2 uv, float2 uv_dx, float2 uv_dy)
{
    float3 tangent_space_normal;
    if (HasTexture(material, TEXTURE_SLOT_NORMAL))
    {
        Texture2D<float4> texture =
            ResourceDescriptorHeap[NonUniformResourceIndex(GetTextureDescriptor(material, TEXTURE_SLOT_NORMAL))];
        SamplerState texture_sampler = SamplerDescriptorHeap[sampler_index];
        tangent_space_normal = texture.SampleGrad(texture_sampler, uv, uv_dx, uv_dy).xyz * 2.0 - 1.0;
    }
    else
    {
        tangent_space_normal = float3(0.0, 0.0, 1.0);
    }
    return tangent_space_normal;
}

float SampleOcclusionTexture(Material material, float2 uv, float2 uv_dx, float2 uv_dy)
{
    float occlusion;
    if (HasTexture(material, TEXTURE_SLOT_OCCLUSION))
    {
        Texture2D<float4> texture =
            ResourceDescriptorHeap[NonUniformResourceIndex(GetTextureDescriptor(material, TEXTURE_SLOT_OCCLUSION))];
        SamplerState texture_sampler = SamplerDescriptorHeap[sampler_index];
        occlusion = texture.SampleGrad(texture_sampler, uv, uv_dx, uv_dy).r;
    }
    else
    {
        occlusion = 1.0;
    }
    return occlusion;
}

float3 SampleEmissiveTexture(Material material, float2 uv, float2 uv_dx, float2 uv_dy)
{
    float3 emissive;
    if (HasTexture(material, TEXTURE_SLOT_EMISSIVE))
    {
        Texture2D<float4> texture =
            ResourceDescriptorHeap[NonUniformResourceIndex(GetTextureDescriptor(material, TEXTURE_SLOT_EMISSIVE))];
        SamplerState texture_sampler = SamplerDescriptorHeap[sampler_index];
        emissive = texture.SampleGrad(texture_sampler, uv, uv_dx, uv_dy).rgb;
    }
    else
    {
        emissive = float3(1.0, 1.0, 1.0);
    }
    return emissive;
}
