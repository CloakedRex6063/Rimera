#include "common.hlsl"
#include "visibility_math.hlsl"

cbuffer RootConstants : register(b3)
{
    uint2 size;
    uint visibility_index;
    uint bary_coords_index;
    uint bary_derivatives_index;
    uint tangent_frame_index;
}

[numthreads(8, 8, 1)]
void CSMain(uint2 id : SV_DispatchThreadID)
{
    if (id.x >= size.x || id.y >= size.y) return;
    Texture2D<uint> visibility = ResourceDescriptorHeap[visibility_index];
    uint vis_id = visibility.Load(int3(id.xy, 0));
    
    RWTexture2D<uint> uv_texture = ResourceDescriptorHeap[bary_coords_index];
    RWTexture2D<float4> uv_gradient = ResourceDescriptorHeap[bary_derivatives_index];
    RWTexture2D<uint4> tangent_frame = ResourceDescriptorHeap[tangent_frame_index];
    
    if(vis_id == 0)
    {
        uv_texture[id] = 0;
        uv_gradient[id] = float4(0, 0, 0, 0);
        tangent_frame[id] = uint4(0, 0, 0, 0);
        return;
    }
    
    vis_id = vis_id - 1;
    uint draw_id = vis_id >> 7;
    uint primitive = vis_id & 127;
    
    ByteAddressBuffer draws = ResourceDescriptorHeap[draw_buffer_index];
    DrawData draw = LoadElement<DrawData>(draws, draw_id);

    ByteAddressBuffer triangles =
        ResourceDescriptorHeap[NonUniformResourceIndex(draw.triangle_index)];

    ByteAddressBuffer meshlets =
        ResourceDescriptorHeap[NonUniformResourceIndex(draw.meshlet_buffer_index)];
    Meshlet meshlet = LoadElement<Meshlet>(meshlets, draw.meshlet_index);

    ByteAddressBuffer vertex_indices =
        ResourceDescriptorHeap[NonUniformResourceIndex(draw.vertex_indices_index)];

    ByteAddressBuffer positions =
        ResourceDescriptorHeap[NonUniformResourceIndex(draw.position_index)];
    ByteAddressBuffer normals =
        ResourceDescriptorHeap[NonUniformResourceIndex(draw.normal_index)];
    ByteAddressBuffer uvs = ResourceDescriptorHeap[NonUniformResourceIndex(draw.uv_index)];
    ByteAddressBuffer tangents =
        ResourceDescriptorHeap[NonUniformResourceIndex(draw.tangent_index)];

    uint offset = meshlet.triangle_offset + primitive * 3;
    uint index0 = LoadByte(triangles, offset);
    uint index1 = LoadByte(triangles, offset + 1);
    uint index2 = LoadByte(triangles, offset + 2);

    uint v0 = LoadElement<uint>(vertex_indices, meshlet.vertex_offset + index0);
    uint v1 = LoadElement<uint>(vertex_indices, meshlet.vertex_offset + index1);
    uint v2 = LoadElement<uint>(vertex_indices, meshlet.vertex_offset + index2);

    ByteAddressBuffer transforms = ResourceDescriptorHeap[NonUniformResourceIndex(transform_buffer_index)];
    Transform transform = LoadElement<Transform>(transforms, draw.transform_index);
    float4x4 mvp = mul(view_projection, transform.world);

    float4 pos0 = mul(mvp, float4(LoadElement<float3>(positions, v0), 1.f));
    float4 pos1 = mul(mvp, float4(LoadElement<float3>(positions, v1), 1.f));
    float4 pos2 = mul(mvp, float4(LoadElement<float3>(positions, v2), 1.f));
    
    float2 pixel_ndc = (float2(id.xy) + 0.5) / float2(size) * 2.0 - 1.0;
    pixel_ndc.y = -pixel_ndc.y;
    
    BarycentricDeriv bary_coords = CalcFullBary(pos0, pos1, pos2, pixel_ndc, float2(size)); 
    
    float2 uv0 = UnpackUV(LoadElement<uint>(uvs, v0));
    float2 uv1 = UnpackUV(LoadElement<uint>(uvs, v1));
    float2 uv2 = UnpackUV(LoadElement<uint>(uvs, v2));
    InterpResult2D uv_interp = InterpolateWithDeriv(bary_coords, uv0, uv1, uv2);
    uint packed_uv = PackUV(uv_interp.interp);
    
    float3 normal0 = UnpackDirection10(LoadElement<uint>(normals, v0));
    float3 normal1 = UnpackDirection10(LoadElement<uint>(normals, v1));
    float3 normal2 = UnpackDirection10(LoadElement<uint>(normals, v2));
    InterpResult3D normal_interp = InterpolateWithDeriv(bary_coords, normal0, normal1, normal2);
    float3x3 normal_3x3 = (float3x3)transform.normal;
    float3 normal_ws = normalize(mul(normal_3x3, normal_interp.interp));
    
    float4 tangent0 = UnpackTangent(LoadElement<uint>(tangents, v0));
    float4 tangent1 = UnpackTangent(LoadElement<uint>(tangents, v1));
    float4 tangent2 = UnpackTangent(LoadElement<uint>(tangents, v2));
    InterpResult3D tangent_interp = InterpolateWithDeriv(bary_coords, tangent0.xyz, tangent1.xyz, tangent2.xyz);
    float3x3 world_3x3 = (float3x3)transform.world;
    float3 tangent_ws = mul(world_3x3, tangent_interp.interp);
    tangent_ws = normalize(tangent_ws - normal_ws * dot(normal_ws, tangent_ws));
    float handedness = tangent0.w * (determinant(world_3x3) < 0.0 ? -1.0 : 1.0);
    
    uv_texture[id] = packed_uv;
    uv_gradient[id] = float4(uv_interp.dx.xy, uv_interp.dy.xy);
    tangent_frame[id] = PackTangentFrame(normal_ws, tangent_ws, handedness);
}
