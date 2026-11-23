// https://github.com/CloakedRex6063/SwiftRender/tree/master/examples/hello_grass
#include "rimera.hlsl"
#include "cull.hlsl"
#include "terrain.hlsl"

struct GrassPatch
{
    float3 position;
    float height;
};

cbuffer Scene : register(b0)
{
    float4x4 view_projection;
    float3 camera_position;
    uint grass_buffer_index;
    float4x4 cull_view_projection;
};

cbuffer Grass : register(b3)
{
    float wind_speed;
    float wind_strength;
    uint apply_view_space_thicken;
    float lod_distance;
    uint grass_count;
    float patch_radius;
    uint cull;
    uint cull_z;
    float blade_width;
    float time;
    float terrain_height;
    float terrain_frequency;
    float terrain_exponent;
    float lod_start;
    uint debug_patch_colors;
};

static const uint GROUP_SIZE = 128;
static const uint MAX_VERTICES_PER_BLADE_EDGE = 6;
static const uint MIN_VERTICES_PER_BLADE_EDGE = 2;
static const uint BLADES_PER_PATCH = GROUP_SIZE / MAX_VERTICES_PER_BLADE_EDGE;
static const uint MAX_VERTICES = BLADES_PER_PATCH * MAX_VERTICES_PER_BLADE_EDGE * 2;
static const uint MAX_TRIANGLES = BLADES_PER_PATCH * (MAX_VERTICES_PER_BLADE_EDGE - 1) * 2;
static const float PI = 3.14159265359;
static const float MAX_LOD_WIDEN = 7.0;

struct Payload
{
    uint patch_id[GROUP_SIZE];
    float lod[GROUP_SIZE];
};

struct Vertex
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 world_position : POSITION;
    float density : DENSITY;
    nointerpolation uint patch_id : PATCH_ID;
    float2 cull_distance : SV_ClipDistance;
};

GrassPatch LoadPatch(uint id)
{
    ByteAddressBuffer patches = ResourceDescriptorHeap[grass_buffer_index];
    return patches.Load<GrassPatch>(id * 16);
}

float3 Bezier(float3 p0, float3 p1, float3 p2, float t)
{
    return lerp(lerp(p0, p1, t), lerp(p1, p2, t), t);
}

float3 BezierDerivative(float3 p0, float3 p1, float3 p2, float t)
{
    return 2.0 * (1.0 - t) * (p1 - p0) + 2.0 * t * (p2 - p1);
}

float2 RandomPointInCircle(uint seed, float radius)
{
    float r = sqrt(Hash(seed)) * radius;
    float theta = Hash(seed + 1u) * 2.0 * PI;
    return float2(r * cos(theta), r * sin(theta));
}

float EaseIn(float t, float power)
{
    return pow(t, power);
}

float EaseOut(float x, float power)
{
    return 1.0 - pow(1.0 - x, power);
}

float Remap(float value, float from_min, float from_max, float to_min, float to_max)
{
    return to_min + (value - from_min) * (to_max - to_min) / (from_max - from_min);
}

float3 ApplyWind(float3 position)
{
    float wind_angle = Remap(Noise2D(position.xz * 0.05 + 0.05 * time), 0.0, 1.0, 0.0, 2.0 * PI);
    float wind_noise = Noise2D(position.xz * 0.25 + time * wind_speed);
    float wind_lean = EaseIn(sin(EaseIn(wind_noise, 2.0)), 2.0) * 1.25 * wind_strength;
    return float3(cos(wind_angle), 0, sin(wind_angle)) * wind_lean;
}

TerrainParams GetTerrain()
{
    TerrainParams terrain = {terrain_height, terrain_frequency, terrain_exponent};
    return terrain;
}

void MakePersistentLength(float3 v0, inout float3 v1, inout float3 v2, float height)
{
    float3 v01 = v1 - v0;
    float3 v12 = v2 - v1;
    float l1 = length(v01) + length(v12);
    float l0 = length(v2 - v0);
    float scale = height / ((2.0 * l0 + l1) / 3.0);
    v1 = v0 + v01 * scale;
    v2 = v1 + v12 * scale;
}

groupshared Payload s_payload;
groupshared uint s_visible_count;

[numthreads(GROUP_SIZE, 1, 1)]
void AMMain(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID)
{
    if (gtid == 0) s_visible_count = 0;
    GroupMemoryBarrierWithGroupSync();

    uint patch_id = gid * GROUP_SIZE + gtid;
    if (patch_id < grass_count)
    {
        GrassPatch grass_patch = LoadPatch(patch_id);
        float3 patch_center = PlaceOnTerrain(grass_patch.position, GetTerrain());
        Sphere bounds = {patch_center, patch_radius};
        if (!cull || IsSphereInFrustum(bounds, cull_view_projection, cull_z != 0))
        {
            uint index;
            InterlockedAdd(s_visible_count, 1, index);
            s_payload.patch_id[index] = patch_id;
            float distance_to_camera = distance(camera_position, patch_center);
            s_payload.lod[index] = smoothstep(lod_start, lod_distance, distance_to_camera);
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
            out vertices Vertex verts[MAX_VERTICES],
            out indices uint3 triangles[MAX_TRIANGLES])
{
    const float3 up = float3(0, 1, 0);

    uint patch_id = payload.patch_id[gid];
    float lod = payload.lod[gid];
    uint edge_vertices = uint(round(lerp(float(MAX_VERTICES_PER_BLADE_EDGE), float(MIN_VERTICES_PER_BLADE_EDGE), lod)));
    uint triangles_per_blade = (edge_vertices - 1) * 2;
    uint grass_id = gtid / edge_vertices;
    uint local_id = gtid % edge_vertices;

    GrassPatch grass_patch = LoadPatch(patch_id);
    float blade_count_f = lerp(float(BLADES_PER_PATCH), 5.0, lod);
    uint blade_count = uint(ceil(blade_count_f));

    SetMeshOutputCounts(blade_count * edge_vertices * 2, blade_count * triangles_per_blade);

    if (grass_id >= blade_count) return;

    uint seed_height = grass_id * 73856093u ^ patch_id * 19349663u;
    uint seed_position = grass_id * 83492791u ^ patch_id * 50331653u;
    uint seed_rotation = grass_id * 19349669u ^ patch_id * 73856093u;
    float2 offset = RandomPointInCircle(seed_position, patch_radius);
    float rand_height = Hash(seed_height);
    float rotation = Hash(seed_rotation) * 2.0 * PI;

    float lod_fade = saturate(blade_count_f - float(grass_id));
    float height = grass_patch.height * (1.0 + 0.5 * rand_height) * lod_fade;
    float3 position = PlaceOnTerrain(grass_patch.position + float3(offset.x, 0, offset.y), GetTerrain());
    float3 blade_direction = float3(cos(rotation), 0, sin(rotation));

    float3 p0 = position;
    float3 p1 = p0 + float3(0, height * 0.5, 0);
    float base_lean = 0.3 + 0.4 * rand_height;
    float3 p2 = p0 + up * height + blade_direction * height * base_lean + ApplyWind(position);
    MakePersistentLength(p0, p1, p2, height);

    float t = local_id / float(edge_vertices - 1);
    float3 tangent = normalize(BezierDerivative(p0, p1, p2, t));
    float3 vertex_position = Bezier(p0, p1, p2, t);
    float3 side = normalize(float3(blade_direction.y, 0, -blade_direction.x));
    float3 normal = cross(side, tangent);

    float width = blade_width * (1.0 - t) * min(BLADES_PER_PATCH / blade_count_f, MAX_LOD_WIDEN) * lod_fade;

    float3 left = vertex_position - blade_direction * width * 0.5;
    float3 right = vertex_position + blade_direction * width * 0.5;

    if (apply_view_space_thicken)
    {
        float3 view_direction = normalize(camera_position - vertex_position);
        float view_dot_normal = saturate(dot(normal.xz, view_direction.xz));
        float thicken = EaseOut(1.0 - view_dot_normal, 4.0);
        float3 adjustment = normalize(cross(view_direction, normal)) * thicken * width;
        left += adjustment;
        right += adjustment;
    }

    uint base_vertex = grass_id * edge_vertices * 2;
    uint vertex = base_vertex + local_id * 2;
    float density = float(blade_count) / float(BLADES_PER_PATCH);

    Vertex left_vertex;
    left_vertex.position = mul(view_projection, float4(left, 1));
    left_vertex.normal = normal;
    left_vertex.uv = float2(0.0, t);
    left_vertex.world_position = left;
    // Technically only needed for the debug view so can get rid of it later
    left_vertex.cull_distance = cull && cull_z ? DepthClipDistance(cull_view_projection, left) : 1.0;
    left_vertex.density = density;
    left_vertex.patch_id = patch_id;
    verts[vertex] = left_vertex;

    Vertex right_vertex;
    right_vertex.position = mul(view_projection, float4(right, 1));
    right_vertex.normal = normal;
    right_vertex.uv = float2(1.0, t);
    right_vertex.world_position = right;
    right_vertex.cull_distance = cull && cull_z ? DepthClipDistance(cull_view_projection, right) : 1.0;
    right_vertex.density = density;
    right_vertex.patch_id = patch_id;
    verts[vertex + 1] = right_vertex;

    if (local_id < edge_vertices - 1)
    {
        uint triangle_index = grass_id * triangles_per_blade + local_id * 2;
        triangles[triangle_index] = uint3(vertex, vertex + 1, vertex + 2);
        triangles[triangle_index + 1] = uint3(vertex + 1, vertex + 3, vertex + 2);
    }
}

float3 PatchColor(uint patch_id)
{
    float hue = Hash(patch_id * 2654435761u);
    float3 rgb = saturate(abs(frac(hue + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
    return lerp(float3(0.5, 0.5, 0.5), rgb, 0.8);
}

float4 PSMain(Vertex input) : SV_Target0
{
    if (debug_patch_colors)
    {
        return float4(PatchColor(input.patch_id) * lerp(0.35, 1.0, input.uv.y), 1.0);
    }

    float3 base_color = float3(0.05, 0.2, 0.01);
    float3 tip_color = float3(0.5, 0.5, 0.1);
    float3 grass_color = lerp(base_color, tip_color, EaseIn(input.uv.y, 4.0));

    float self_shadow = pow(input.uv.y, 1.5);
    float ao_for_density = lerp(0.25, 1.0, input.density);
    float ao = lerp(ao_for_density, 1.0, EaseIn(input.uv.y, 2.0));

    return float4(grass_color + 0.3 * ao * self_shadow, 1.0);
}
