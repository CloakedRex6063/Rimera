struct Transform
{
    column_major float4x4 world;
    column_major float4x4 normal;
};

struct DrawData
{
    uint vertex_index;
    uint transform_index;
    uint material_index;
    uint padding;
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

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint material_index : TEXCOORD1;
    float2 emissive_uv : TEXCOORD2;
};

VSOut VSMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    StructuredBuffer<DrawData> draws = ResourceDescriptorHeap[draw_buffer_index];
    DrawData draw = draws.Load(instance_id);
    StructuredBuffer<Transform> transforms = ResourceDescriptorHeap[transform_buffer_index];
    Transform transform = transforms.Load(draw.transform_index);
    StructuredBuffer<Material> materials = ResourceDescriptorHeap[material_buffer_index];
    Material material = materials.Load(draw.material_index);
    StructuredBuffer<Vertex> vertices = ResourceDescriptorHeap[draw.vertex_index];
    Vertex vertex = vertices.Load(vertex_id);
    uint packed_uv = vertex.uv[material.uv_sets & 1];
    VSOut result;
    result.position = mul(view_projection, mul(transform.world, float4(vertex.position, 1)));
    result.uv = float2(f16tof32(packed_uv & 0xffff), f16tof32(packed_uv >> 16));
    result.material_index = draw.material_index;
    uint packed_emissive_uv = vertex.uv[(material.uv_sets >> 4) & 1];
    result.emissive_uv = float2(f16tof32(packed_emissive_uv & 0xffff),
                                f16tof32(packed_emissive_uv >> 16));
    return result;
}

float4 PSMain(VSOut input) : SV_Target
{
    StructuredBuffer<Material> materials = ResourceDescriptorHeap[material_buffer_index];
    Material material = materials.Load(input.material_index);
    float4 color = material.base_color;
    float3 emissive = material.emissive;
    StructuredBuffer<uint> textures = ResourceDescriptorHeap[texture_buffer_index];
    SamplerState linear_sampler = SamplerDescriptorHeap[sampler_index];
    uint base_color_texture_index = material.textures[0];
    if (base_color_texture_index != 0xffff)
    {
        uint texture_index = textures.Load(base_color_texture_index);
        Texture2D<float4> base_color = ResourceDescriptorHeap[NonUniformResourceIndex(texture_index)];
        color *= base_color.Sample(linear_sampler, input.uv);
    }
    uint emissive_texture_index = material.textures[4];
    if (emissive_texture_index != 0xffff)
    {
        uint texture_index = textures.Load(emissive_texture_index);
        Texture2D<float4> emissive_texture = ResourceDescriptorHeap[NonUniformResourceIndex(texture_index)];
        emissive *= emissive_texture.Sample(linear_sampler, input.emissive_uv).rgb;
    }
    if ((material.flags & 3) == 1)
        clip(color.a - material.alpha_cutoff);
    return float4(color.rgb + emissive, color.a);
}
