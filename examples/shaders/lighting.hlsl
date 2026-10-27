#include "common.hlsl"
#include "brdf.hlsl"

struct DirLight
{
    float3 direction;
    float intensity;
    float3 color;
    float padding;
};

cbuffer RootConstants : register(b3)
{
    uint visibility_index;
    uint albedo_metal_index;
    uint normal_index;
    uint rough_occl_index;
    uint emissive_index;
    uint output_index;
    uint depth_index;
    uint lighting_padding;
    DirLight dir_light;
}

[numthreads(8, 8, 1)]
void CSMain(uint2 id : SV_DispatchThreadID)
{
    Texture2D<uint> visibility = ResourceDescriptorHeap[visibility_index];
    uint width, height;
    visibility.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;

    RWTexture2D<float4> output = ResourceDescriptorHeap[output_index];
    if (visibility.Load(int3(id, 0)) == 0)
    {
        output[id] = float4(0.392157, 0.584314, 0.929412, 1.0);
        return;
    }

    Texture2D<float4> albedo_metal_texture = ResourceDescriptorHeap[albedo_metal_index];
    Texture2D<float2> normal_texture = ResourceDescriptorHeap[normal_index];
    Texture2D<float2> rough_occl_texture = ResourceDescriptorHeap[rough_occl_index];
    Texture2D<float3> emissive_texture = ResourceDescriptorHeap[emissive_index];
    Texture2D<float> depth_texture = ResourceDescriptorHeap[depth_index];

    float4 albedo_metal = albedo_metal_texture.Load(int3(id, 0));
    float2 rough_occl = rough_occl_texture.Load(int3(id, 0));

    SurfaceData surface;
    surface.albedo = albedo_metal.rgb;
    surface.metallic = albedo_metal.a;
    surface.normal = OctDecode(normal_texture.Load(int3(id, 0)) * 2.0 - 1.0);
    surface.roughness = rough_occl.r;

    float2 pixel_ndc = (float2(id) + 0.5) / float2(width, height) * 2.0 - 1.0;
    pixel_ndc.y = -pixel_ndc.y;
    float depth = depth_texture.Load(int3(id, 0));
    float4 world_position = mul(inverse_view_projection, float4(pixel_ndc, depth, 1.0));
    world_position.xyz /= world_position.w;
    float3 v = normalize(camera_position - world_position.xyz);

    BrdfInputs inputs = MakeBrdfInputs(surface);
    float3 color = EvaluateDirectLight(inputs,
                                       surface.normal,
                                       v,
                                       normalize(dir_light.direction),
                                       dir_light.color * dir_light.intensity);
    color += emissive_texture.Load(int3(id, 0));

    output[id] = float4(color, 1.0);
}
