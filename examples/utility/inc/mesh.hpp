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

struct BufferDeleter
{
    RM::Device* device{};
    void operator()(RM::Buffer* buffer) const;
};
using OwnedBuffer = std::unique_ptr<RM::Buffer, BufferDeleter>;

class Mesh
{
public:
    Mesh(RM::Device* device, std::span<const Vertex> vertices, std::span<const uint32_t> indices,
         uint32_t material_index, uint16_t texture_index = UINT16_MAX);
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) noexcept = default;
    Mesh& operator=(Mesh&&) noexcept = default;

    [[nodiscard]] RM::Buffer* GetVertexBuffer() const { return m_vertex_buffer.get(); }
    [[nodiscard]] RM::Buffer* GetVertexIndexBuffer() const { return m_vertex_index_buffer.get(); }
    [[nodiscard]] RM::Buffer* GetTriangleBuffer() const { return m_triangle_buffer.get(); }
    [[nodiscard]] RM::Buffer* GetMeshletBuffer() const { return m_meshlet_buffer.get(); }
    [[nodiscard]] uint32_t GetVertexCount() const { return m_vertex_count; }
    [[nodiscard]] uint32_t GetTriangleCount() const { return m_triangle_count; }
    [[nodiscard]] uint32_t GetMeshletCount() const { return m_meshlet_count; }
    [[nodiscard]] uint32_t GetMaterialIndex() const { return m_material_index; }
    [[nodiscard]] uint16_t GetTextureIndex() const { return m_texture_index; }
    [[nodiscard]] RM::Buffer* GenerateIndexBuffer() const;
private:
    RM::Device* m_device;
    OwnedBuffer m_vertex_buffer, m_vertex_index_buffer, m_triangle_buffer, m_meshlet_buffer;
    uint32_t m_vertex_count{}, m_triangle_count{}, m_meshlet_count{}, m_material_index{};
    uint16_t m_texture_index{};
};
