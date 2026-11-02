#pragma once
#include "rimera_buffer.hpp"
#include "array"
#include "memory"
#include "span"
#include "vector"
#include "type_traits"
#include "cstddef"
#include "glm/glm.hpp"

struct Material
{
    glm::vec4 base_color{1, 1, 1, 1};
    
    glm::vec3 emissive{};
    float metallic = 1; 
    
    float roughness = 1;
    float normal_scale = 1;
    float occlusion_strength = 1;
    float alpha_cutoff = .5f;
    
    // Base color: 0, Metallic/Roughness: 1, Normal: 2, Occlusion: 3, Emissive: 4
    uint16_t textures[5]{UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX};
    
    // Bits 0-1: opaque=0, mask=1, blend=2; bit 2: double sided.
    uint16_t flags = 0;
};
static_assert(sizeof(Material) == 60 && std::is_trivially_copyable_v<Material>);
static_assert(offsetof(Material, textures) == 48 && offsetof(Material, flags) == 58);

struct Vertex
{
    glm::vec3 position{};
    // 10_10_10
    uint32_t normal{};
    // 10_10_10_2
    uint32_t tangent{};
    uint32_t uv{};
};
static_assert(sizeof(Vertex) == 24 && std::is_trivially_copyable_v<Vertex>);
static_assert(offsetof(Vertex, position) == 0 && offsetof(Vertex, normal) == 12 &&
              offsetof(Vertex, tangent) == 16 && offsetof(Vertex, uv) == 20);

struct Bounds
{
    glm::vec3 center{};
    float radius{};

    glm::vec3 cone_apex{};
    // x / 127.0 to get original value
    int8_t cone_axis[3]{};
    int8_t cone_cutoff{};
};
static_assert(sizeof(Bounds) == 32 && std::is_trivially_copyable_v<Bounds>);

struct Meshlet
{
    uint32_t vertex_offset{};
    uint32_t triangle_offset{};
    uint8_t vertex_count{};
    uint8_t triangle_count{};
};

struct SceneGeometry
{
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> normals;
    std::vector<uint32_t> uvs;
    std::vector<uint32_t> tangents;
    std::vector<uint32_t> vertex_indices;
    std::vector<uint8_t> triangles;
    std::vector<Meshlet> meshlets;
    std::vector<Bounds> bounds;
};

struct Mesh
{
    [[nodiscard]] static RM::Result<Mesh> Create(SceneGeometry& geometry,
                                                 std::span<const Vertex> vertices,
                                                 std::span<const uint32_t> indices,
                                                 uint32_t material_index,
                                                 uint16_t texture_index = UINT16_MAX);

    uint32_t first_meshlet{}, meshlet_count{}, vertex_count{}, triangle_count{}, material_index{};
    uint16_t texture_index{};
};
