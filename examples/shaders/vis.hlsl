#include "common.hlsl"
#include "rimera.hlsl"

struct MeshOutput
{
    float4 position : SV_Position;
};

struct AmPayload
{
    uint meshlet_id[32];
};

groupshared AmPayload s_payload;

[numthreads(32,1,1)]
void AMMain(uint dtid: SV_DispatchThreadID, uint group_id : SV_GroupID)
{
    bool visible = false;
    if(dtid < meshlet_count)
    {
        visible = true;
    }
    
    if(visible)
    {
        uint index = WavePrefixCountBits(visible);
        s_payload.meshlet_id[index] = dtid;
    }
    
    uint visible_count = WaveActiveCountBits(visible);
    
    DispatchMesh(visible_count, 1, 1, s_payload);
}

struct PrimOut { PER_PRIMITIVE uint vis_id : VIS_ID; };

[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint group_index : SV_GroupIndex,
            uint group_id : SV_GroupID,
            out vertices MeshOutput vertices_out[64],
            out indices uint3 triangles_out[124],
            out primitives PrimOut prims[124],
            in payload AmPayload payload)
{
    uint draw_id = payload.meshlet_id[group_id];
    DrawData draw = LoadDraw(draw_id);
    SetMeshOutputCounts(draw.vertex_count, draw.triangle_count);

    ByteAddressBuffer positions = ResourceDescriptorHeap[position_buffer_index];
    ByteAddressBuffer vertex_indices = ResourceDescriptorHeap[vertex_index_buffer_index];
    ByteAddressBuffer triangles = ResourceDescriptorHeap[triangle_buffer_index];
    ByteAddressBuffer transforms = ResourceDescriptorHeap[transform_buffer_index];
    Transform transform = LoadElement<Transform>(transforms, draw.transform_index);

    if (group_index < draw.vertex_count)
    {
        float3 position = LoadElement<float3>(
            positions, LoadElement<uint>(vertex_indices, draw.vertex_offset + group_index));
        MeshOutput output;
        output.position = mul(view_projection, mul(transform.world, float4(position, 1)));
        vertices_out[group_index] = output;
    }

    for (uint primitive = group_index; primitive < draw.triangle_count; primitive += 64)
    {
        uint offset = draw.triangle_offset + primitive * 3;
        triangles_out[primitive] = uint3(LoadByte(triangles, offset),
                                        LoadByte(triangles, offset + 1),
                                        LoadByte(triangles, offset + 2));
        prims[primitive].vis_id = ((draw_id << 7) | primitive) + 1; // 0 to identify background
    }
}

MESH_FRAGMENT uint PSMain(MeshOutput, PrimOut p) : SV_Target0
{
    return p.vis_id;
}
