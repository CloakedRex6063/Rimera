#pragma once
#include "rimera_buffer.hpp"
#include "array"
#include "memory"
#include "span"
#include "type_traits"
#include "cstddef"
#include "glm/glm.hpp"

namespace RM { class Device; }

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
    // Bit i selects TEXCOORD_1 for texture slot i; otherwise TEXCOORD_0.
    uint32_t uv_sets = 0;
};
static_assert(sizeof(Material) == 64 && std::is_trivially_copyable_v<Material>);
static_assert(offsetof(Material, textures) == 48 && offsetof(Material, flags) == 58 && offsetof(Material, uv_sets) == 60);

struct Vertex
{
    glm::vec3 position{};
    // 10_10_10
    float normal{};
    // 10_10_10
    float tangent{};
    // B = (bitangent ? -1 : 1) * cross(N, T).
    uint32_t bitangent{};
    // UV0 in x, UV1 in y
    glm::uvec2 uv{};
};
static_assert(sizeof(Vertex) == 32 && std::is_trivially_copyable_v<Vertex>);
static_assert(offsetof(Vertex, position) == 0 && offsetof(Vertex, normal) == 12 &&
              offsetof(Vertex, tangent) == 16 && offsetof(Vertex, bitangent) == 20 &&
              offsetof(Vertex, uv) == 24);

class Mesh
{
public:
    ~Mesh();
    [[nodiscard]] static RM::Result<Mesh*> Create(RM::Device* device,
                                                  std::span<const Vertex> vertices,
                                                  std::span<const uint32_t> indices,
                                                  uint32_t material_index,
                                                  uint16_t texture_index = UINT16_MAX);
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) = delete;
    Mesh& operator=(Mesh&&) = delete;

    [[nodiscard]] RM::Buffer* GetVertexBuffer() const { return m_vertex_buffer; }
    [[nodiscard]] RM::Buffer* GetVertexIndexBuffer() const { return m_vertex_index_buffer; }
    [[nodiscard]] RM::Buffer* GetTriangleBuffer() const { return m_triangle_buffer; }
    [[nodiscard]] RM::Buffer* GetMeshletBuffer() const { return m_meshlet_buffer; }
    [[nodiscard]] uint32_t GetVertexCount() const { return m_vertex_count; }
    [[nodiscard]] uint32_t GetTriangleCount() const { return m_triangle_count; }
    [[nodiscard]] uint32_t GetMeshletCount() const { return m_meshlet_count; }
    [[nodiscard]] uint32_t GetMaterialIndex() const { return m_material_index; }
    [[nodiscard]] uint16_t GetTextureIndex() const { return m_texture_index; }
    [[nodiscard]] RM::Result<RM::Buffer*> GenerateIndexBuffer() const;

private:
    Mesh() = default;
    RM::Result<RM::Buffer*> Upload(const void* data, size_t bytes) const;
    RM::Result<void> Initialize(RM::Device* device,
                                std::span<const Vertex> vertices,
                                std::span<const uint32_t> indices,
                                uint32_t material_index,
                                uint16_t texture_index);

    RM::Device* m_device = nullptr;
    RM::Buffer* m_vertex_buffer = nullptr;
    RM::Buffer* m_vertex_index_buffer = nullptr;
    RM::Buffer* m_triangle_buffer = nullptr;
    RM::Buffer* m_meshlet_buffer = nullptr;
    uint32_t m_vertex_count{}, m_triangle_count{}, m_meshlet_count{}, m_material_index{};
    uint16_t m_texture_index{};
};
