#include "common.hlsl"

cbuffer RootConstants : register(b3)
{
    uint2 size;
    uint uv_index;
    uint gradients_index;
    uint tangent_frame_index;
    uint visibility_index;
    uint output_texture_index;
}

[numthreads(8, 8, 1)]
void CSMain(uint2 id : SV_DispatchThreadID)
{
    Texture2D<uint> uv_texture = ResourceDescriptorHeap[uv_index];
    Texture2D<float4> gradient_texture = ResourceDescriptorHeap[gradients_index];
    Texture2D<uint4> tangent_frame_texture = ResourceDescriptorHeap[tangent_frame_index];
    Texture2D<uint> visibility_texture = ResourceDescriptorHeap[visibility_index];
    
    uint packed_uv = uv_texture.Load(int3(id, 0));
    float2 uv = UnpackUV(packed_uv);
    
    float4 gradients = gradient_texture.Load(int3(id, 0));
    
    uint4 packed_tangent = tangent_frame_texture.Load(int3(id, 0));
    TangentFrame tangent_frame = UnpackTangentFrame(packed_tangent);

    uint vis_id = visibility_texture.Load(int3(id.xy, 0));
    vis_id = vis_id - 1;
    uint draw_id = vis_id >> 7;
    uint primitive = vis_id & 127;
    
    ByteAddressBuffer draws = ResourceDescriptorHeap[draw_buffer_index];
    DrawData draw = LoadElement<DrawData>(draws, draw_id);
    ByteAddressBuffer materials = ResourceDescriptorHeap[material_buffer_index];
    Material material = LoadElement<Material>(materials, draw.material_index);
    
    float4 albedo = material.base_color * SampleBaseColorTexture(material, uv, gradients.xy, gradients.zw);
    
    
    RWTexture2D<float4> output_texture = ResourceDescriptorHeap[output_texture_index];
    output_texture[id] = albedo;
}
