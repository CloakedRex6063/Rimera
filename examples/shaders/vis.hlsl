#include "common.hlsl"

struct MeshOutput
{
    float4 position : SV_Position;
};

struct PrimOut { uint vis_id : VIS_ID; };

[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint group_index : SV_GroupIndex,
            uint3 group_id : SV_GroupID,
            out vertices MeshOutput vertices_out[64],
            out indices uint3 triangles_out[124],
            out primitives PrimOut prims[124])
{
    StructuredBuffer<DrawData> draws = ResourceDescriptorHeap[draw_buffer_index];
    DrawData draw = draws.Load(group_id.x);
    StructuredBuffer<Meshlet> meshlets =
        ResourceDescriptorHeap[NonUniformResourceIndex(draw.meshlet_buffer_index)];
    Meshlet meshlet = meshlets.Load(draw.meshlet_index);
    SetMeshOutputCounts(meshlet.vertex_count, meshlet.triangle_count);

    StructuredBuffer<float3> positions =
        ResourceDescriptorHeap[NonUniformResourceIndex(draw.position_index)];
    StructuredBuffer<uint> vertex_indices =
        ResourceDescriptorHeap[NonUniformResourceIndex(draw.vertex_indices_index)];
    ByteAddressBuffer triangles =
        ResourceDescriptorHeap[NonUniformResourceIndex(draw.triangle_index)];
    StructuredBuffer<Transform> transforms = ResourceDescriptorHeap[transform_buffer_index];
    Transform transform = transforms.Load(draw.transform_index);

    if (group_index < meshlet.vertex_count)
    {
        float3 position = positions.Load(vertex_indices.Load(meshlet.vertex_offset + group_index));
        MeshOutput output;
        output.position = mul(view_projection, mul(transform.world, float4(position, 1)));
        vertices_out[group_index] = output;
    }

    for (uint primitive = group_index; primitive < meshlet.triangle_count; primitive += 64)
    {
        uint offset = meshlet.triangle_offset + primitive * 3;
        triangles_out[primitive] = uint3(LoadByte(triangles, offset),
                                        LoadByte(triangles, offset + 1),
                                        LoadByte(triangles, offset + 2));
        prims[primitive].vis_id = ((group_id.x << 7) | primitive) + 1; // 0 to identify background
    }
}

uint PSMain(MeshOutput, PrimOut p) : SV_Target0
{
    return p.vis_id;
}
