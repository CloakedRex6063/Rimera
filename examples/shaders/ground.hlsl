#include "rimera.hlsl"
#include "terrain.hlsl"
#include "cull.hlsl"

cbuffer Scene : register(b0)
{
    float4x4 view_projection;
    float3 camera_position;
    uint grass_buffer_index;
    float4x4 cull_view_projection;
};

cbuffer Ground : register(b3)
{
    float2 origin;
    float2 extent;
    float cell_size;
    float terrain_height;
    float terrain_frequency;
    float terrain_exponent;
    uint tile_count_x;
    uint tile_count;
    uint cull;
    uint cull_z;
};

static const uint TILE_QUADS = 8;
static const uint TILE_VERTICES = TILE_QUADS + 1;
static const uint VERTEX_COUNT = TILE_VERTICES * TILE_VERTICES;
static const uint TRIANGLE_COUNT = TILE_QUADS * TILE_QUADS * 2;
static const uint GROUP_SIZE = 128;

struct Payload
{
    uint2 tiles[GROUP_SIZE];
};

struct Vertex
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 cull_distance : SV_ClipDistance;
};

groupshared Payload s_payload;
groupshared uint s_visible_count;

Sphere TileBounds(uint2 tile)
{
    float tile_size = TILE_QUADS * cell_size;
    float half_height = terrain_height * 0.5;
    float2 center = origin + (float2(tile) + 0.5) * tile_size;
    Sphere sphere = {float3(center.x, half_height, center.y),
                     length(float3(tile_size * 0.5, half_height, tile_size * 0.5))};
    return sphere;
}

[numthreads(GROUP_SIZE, 1, 1)]
void AMMain(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID)
{
    if (gtid == 0) s_visible_count = 0;
    GroupMemoryBarrierWithGroupSync();

    uint tile_index = gid * GROUP_SIZE + gtid;
    if (tile_index < tile_count)
    {
        uint2 tile = uint2(tile_index % tile_count_x, tile_index / tile_count_x);
        if (!cull || IsSphereInFrustum(TileBounds(tile), cull_view_projection, cull_z != 0))
        {
            uint index;
            InterlockedAdd(s_visible_count, 1, index);
            s_payload.tiles[index] = tile;
        }
    }

    GroupMemoryBarrierWithGroupSync();
    DispatchMesh(s_visible_count, 1, 1, s_payload);
}

[numthreads(GROUP_SIZE, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint gtid : SV_GroupThreadID,
            uint gid : SV_GroupID,
            in payload Payload payload,
            out vertices Vertex verts[VERTEX_COUNT],
            out indices uint3 triangles[TRIANGLE_COUNT])
{
    SetMeshOutputCounts(VERTEX_COUNT, TRIANGLE_COUNT);

    TerrainParams terrain = {terrain_height, terrain_frequency, terrain_exponent};
    uint2 tile = payload.tiles[gid];

    if (gtid < VERTEX_COUNT)
    {
        uint2 local = uint2(gtid % TILE_VERTICES, gtid / TILE_VERTICES);
        float2 offset = min(float2(tile * TILE_QUADS + local) * cell_size, extent);
        float2 xz = origin + offset;
        float3 position = float3(xz.x, TerrainHeight(xz, terrain), xz.y);

        Vertex vertex;
        vertex.position = mul(view_projection, float4(position, 1));
        vertex.normal = TerrainNormal(xz, terrain, cell_size * 0.5);
        vertex.cull_distance = cull && cull_z ? DepthClipDistance(cull_view_projection, position) : 1.0;
        verts[gtid] = vertex;
    }

    if (gtid < TRIANGLE_COUNT)
    {
        uint quad = gtid / 2;
        uint v0 = (quad / TILE_QUADS) * TILE_VERTICES + quad % TILE_QUADS;
        uint v1 = v0 + 1;
        uint v2 = v0 + TILE_VERTICES;
        uint v3 = v2 + 1;
        triangles[gtid] = (gtid & 1) == 0 ? uint3(v0, v2, v1) : uint3(v1, v2, v3);
    }
}

float4 PSMain(Vertex input) : SV_Target0
{
    float3 base_color = float3(0.12, 0.07, 0.03);
    float shade = lerp(0.5, 1.0, saturate(normalize(input.normal).y));
    return float4(base_color * shade, 1.0);
}
