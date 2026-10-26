#include "common.hlsl"

cbuffer Indices : register(b3)
{
    uint color_index;
    uint visibility_index;
    uint input_index;
    uint mode;
};

static const uint DEBUG_VISIBILITY_BUFFER = 0;
static const uint DEBUG_ALBEDO = 1;
static const uint DEBUG_METALLIC = 2;
static const uint DEBUG_ROUGHNESS = 3;
static const uint DEBUG_OCCLUSION = 4;
static const uint DEBUG_EMISSIVE = 5;
static const uint DEBUG_NORMAL = 6;

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    Texture2D<uint> visibility = ResourceDescriptorHeap[visibility_index];
    uint width, height;
    visibility.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    RWTexture2D<float4> color = ResourceDescriptorHeap[color_index];
    uint value = visibility.Load(int3(id.xy, 0));
    if (value == 0)
    {
        color[id.xy] = float4(0.392157, 0.584314, 0.929412, 1.0);
        return;
    }

    float3 result = float3(0.0, 0.0, 0.0);
    switch (mode)
    {
        case DEBUG_VISIBILITY_BUFFER:
        {
            uint hash = value * 2654435761u;
            hash ^= hash >> 16;
            result = float3(hash & 255, (hash >> 8) & 255, (hash >> 16) & 255) / 255.0;
            break;
        }
        case DEBUG_ALBEDO:
        {
            Texture2D<float4> albedo_metal = ResourceDescriptorHeap[input_index];
            result = albedo_metal.Load(int3(id.xy, 0)).rgb;
            break;
        }
        case DEBUG_METALLIC:
        {
            Texture2D<float4> albedo_metal = ResourceDescriptorHeap[input_index];
            result = albedo_metal.Load(int3(id.xy, 0)).a;
            break;
        }
        case DEBUG_ROUGHNESS:
        {
            Texture2D<float2> rough_occlusion = ResourceDescriptorHeap[input_index];
            result = rough_occlusion.Load(int3(id.xy, 0)).r;
            break;
        }
        case DEBUG_OCCLUSION:
        {
            Texture2D<float2> rough_occlusion = ResourceDescriptorHeap[input_index];
            result = rough_occlusion.Load(int3(id.xy, 0)).g;
            break;
        }
        case DEBUG_EMISSIVE:
        {
            Texture2D<float3> emissive = ResourceDescriptorHeap[input_index];
            result = emissive.Load(int3(id.xy, 0));
            break;
        }
        case DEBUG_NORMAL:
        {
            Texture2D<float2> normal_texture = ResourceDescriptorHeap[input_index];
            float3 normal = OctDecode(normal_texture.Load(int3(id.xy, 0)) * 2.0 - 1.0);
            result = normal * 0.5 + 0.5;
            break;
        }
    }
    color[id.xy] = float4(result, 1.0);
}
