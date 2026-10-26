#include "common.hlsl"

cbuffer RootConstants : register(b3)
{
    uint uv_index;
    uint gradients_index;
    uint tangent_frame_index;
    uint visibility_index;
    uint albedo_metal_index;
    uint normal_index;
    uint rough_occl_index;
    uint emissive_index;
}

[numthreads(8, 8, 1)]
void CSMain(uint2 id : SV_DispatchThreadID)
{
    Texture2D<uint> visibility_texture = ResourceDescriptorHeap[visibility_index];
    uint width, height;
    visibility_texture.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
 
    uint vis_id = visibility_texture.Load(int3(id.xy, 0));
    if(vis_id == 0) return; 
    vis_id = vis_id - 1;
 
    Texture2D<uint> uv_texture = ResourceDescriptorHeap[uv_index];
    Texture2D<float4> gradient_texture = ResourceDescriptorHeap[gradients_index];
    Texture2D<uint4> tangent_frame_texture = ResourceDescriptorHeap[tangent_frame_index];   
    uint packed_uv = uv_texture.Load(int3(id, 0));
    float2 uv = UnpackUV(packed_uv);
     
    float4 gradients = gradient_texture.Load(int3(id, 0));
     
    uint4 packed_tangent = tangent_frame_texture.Load(int3(id, 0));
    TangentFrame tangent_frame = UnpackTangentFrame(packed_tangent);
    
    uint draw_id = vis_id >> 7;
    
    ByteAddressBuffer draws = ResourceDescriptorHeap[draw_buffer_index];
    DrawData draw = LoadElement<DrawData>(draws, draw_id);
    ByteAddressBuffer materials = ResourceDescriptorHeap[material_buffer_index];
    Material material = LoadElement<Material>(materials, draw.material_index);
    
    // The alpha is discarded for now, maybe handle this later?
    float4 albedo = material.base_color * SampleBaseColorTexture(material, uv, gradients.xy, gradients.zw);
    float3 normal_map = SampleNormalTexture(material, uv, gradients.xy, gradients.zw);
    normal_map.xy *= material.normal_scale;
    normal_map = normalize(normal_map);
    
    float3 normal = normalize(tangent_frame.tangent * normal_map.x +
                                    tangent_frame.bitangent * normal_map.y +
                                    tangent_frame.normal * normal_map.z);

    float2 metal_rough_tex = SampleMetallicRoughnessTexture(material, uv, gradients.xy, gradients.zw); 
    float rough = material.roughness * metal_rough_tex.r;
    float metal = material.metallic * metal_rough_tex.g;
    
    float occlusion_tex = SampleOcclusionTexture(material, uv, gradients.xy, gradients.zw);
    float occlusion = occlusion_tex * material.occlusion_strength;
    
    float3 emissive_tex = SampleEmissiveTexture(material, uv, gradients.xy, gradients.zw);
    float3 emissive = emissive_tex * material.emissive;
    
    RWTexture2D<float4> albedo_metal = ResourceDescriptorHeap[albedo_metal_index];
    albedo_metal[id] = float4(albedo.rgb, metal);
    RWTexture2D<float2> normal_out = ResourceDescriptorHeap[normal_index];
    normal_out[id] = OctEncode(normal) * 0.5 + 0.5;
    RWTexture2D<float2> rough_occl_out = ResourceDescriptorHeap[rough_occl_index];
    rough_occl_out[id] = float2(rough, occlusion);
    RWTexture2D<float3> emissive_out = ResourceDescriptorHeap[emissive_index];
    emissive_out[id] = emissive;
}
