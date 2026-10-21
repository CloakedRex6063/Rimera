struct Transform
{
    column_major float4x4 world;
    column_major float4x4 normal;
};

struct DrawData
{
    uint meshlet_index;
    uint vertex_index;
    uint vertex_indices_index;
    uint triangle_index;
    uint meshlet_buffer_index;
    uint transform_index;
    uint material_index;
    uint padding;
};

struct Meshlet
{
    uint vertex_offset;
    uint triangle_offset;
    uint vertex_count;
    uint triangle_count;
};

struct Vertex
{
    float3 position;
    uint normal;
    uint tangent;
    uint bitangent;
    uint2 uv;
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
    uint uv_sets;
};

cbuffer Scene : register(b0)
{
    column_major float4x4 view_projection;
    uint draw_buffer_index;
    uint transform_buffer_index;
    uint material_buffer_index;
    uint texture_buffer_index;
    uint sampler_index;
    uint3 padding;
};

struct MeshOutput
{
    float4 position : SV_Position;
};

uint LoadByte(ByteAddressBuffer buffer, uint offset)
{
    return (buffer.Load(offset & ~3u) >> ((offset & 3u) * 8u)) & 0xffu;
}

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

    StructuredBuffer<Vertex> vertices =
        ResourceDescriptorHeap[NonUniformResourceIndex(draw.vertex_index)];
    StructuredBuffer<uint> vertex_indices =
        ResourceDescriptorHeap[NonUniformResourceIndex(draw.vertex_indices_index)];
    ByteAddressBuffer triangles =
        ResourceDescriptorHeap[NonUniformResourceIndex(draw.triangle_index)];
    StructuredBuffer<Transform> transforms = ResourceDescriptorHeap[transform_buffer_index];
    Transform transform = transforms.Load(draw.transform_index);

    if (group_index < meshlet.vertex_count)
    {
        Vertex vertex = vertices.Load(vertex_indices.Load(meshlet.vertex_offset + group_index));
        MeshOutput output;
        output.position = mul(view_projection, mul(transform.world, float4(vertex.position, 1)));
        vertices_out[group_index] = output;
    }

    for (uint primitive = group_index; primitive < meshlet.triangle_count; primitive += 64)
    {
        uint offset = meshlet.triangle_offset + primitive * 3;
        triangles_out[primitive] = uint3(LoadByte(triangles, offset),
                                        LoadByte(triangles, offset + 1),
                                        LoadByte(triangles, offset + 2));
        prims[primitive].vis_id = ((group_id.x << 7) | primitive) + 1;
    }
}

uint PSMain(MeshOutput, PrimOut p) : SV_Target0
{
    return p.vis_id;
}
