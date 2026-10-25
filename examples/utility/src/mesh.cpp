#include "mesh.hpp"
#include "rimera_device.hpp"
#include "meshoptimizer.h"
#include "vector"

RM::Result<RM::Buffer*> Mesh::Upload(const void* data, size_t bytes) const
{
    if (!bytes || bytes > UINT32_MAX) return std::unexpected("Invalid mesh buffer size");
    return m_device->CreateBuffer({.size = static_cast<uint32_t>(bytes), .data = data});
}

RM::Result<Mesh*> Mesh::Create(RM::Device* device,
                               std::span<const Vertex> vertices,
                               std::span<const uint32_t> indices,
                               uint32_t material_index,
                               uint16_t texture_index)
{
    auto* object = new Mesh();
    if (!object) return std::unexpected("Object allocation failed");
    auto result = object->Initialize(device, vertices, indices, material_index, texture_index);
    if (!result)
    {
        auto error = std::move(result.error());
        delete object;
        return std::unexpected(std::move(error));
    }
    return object;
}

RM::Result<void> Mesh::Initialize(RM::Device* device,
                                  std::span<const Vertex> vertices,
                                  std::span<const uint32_t> indices,
                                  uint32_t material_index,
                                  uint16_t texture_index)
{
    m_device = device;
    m_material_index = material_index;
    m_texture_index = texture_index;
    if (!device || vertices.empty() || indices.empty() || indices.size() % 3 ||
        vertices.size_bytes() > UINT32_MAX || indices.size_bytes() > UINT32_MAX)
        return std::unexpected("Invalid triangle mesh");
    for (auto i : indices)
        if (i >= vertices.size()) return std::unexpected("Mesh index out of range");
    std::vector<unsigned int> remap(vertices.size());
    auto count = meshopt_generateVertexRemap(remap.data(),
                                             indices.data(),
                                             indices.size(),
                                             vertices.data(),
                                             vertices.size(),
                                             sizeof(Vertex));
    std::vector<Vertex> optimized(count);
    std::vector<unsigned int> optimized_indices(indices.size());
    meshopt_remapVertexBuffer(optimized.data(),
                              vertices.data(),
                              vertices.size(),
                              sizeof(Vertex),
                              remap.data());
    meshopt_remapIndexBuffer(optimized_indices.data(),
                             indices.data(),
                             indices.size(),
                             remap.data());
    meshopt_optimizeVertexCache(optimized_indices.data(),
                                optimized_indices.data(),
                                indices.size(),
                                count);
    meshopt_optimizeVertexFetch(optimized.data(),
                                optimized_indices.data(),
                                indices.size(),
                                optimized.data(),
                                count,
                                sizeof(Vertex));
    constexpr size_t max_vertices = 64, max_triangles = 124;
    std::vector<meshopt_Meshlet> meshlets(
        meshopt_buildMeshletsBound(indices.size(), max_vertices, max_triangles));
    std::vector<unsigned int> vertex_indices(indices.size());
    std::vector<unsigned char> triangles(indices.size());
    auto meshlet_count = meshopt_buildMeshlets(meshlets.data(),
                                               vertex_indices.data(),
                                               triangles.data(),
                                               optimized_indices.data(),
                                               indices.size(),
                                               &optimized[0].position.x,
                                               count,
                                               sizeof(Vertex),
                                               max_vertices,
                                               max_triangles,
                                               0);
    meshlets.resize(meshlet_count);
    for (const auto& m : meshlets)
        meshopt_optimizeMeshlet(vertex_indices.data() + m.vertex_offset,
                                triangles.data() + m.triangle_offset,
                                m.triangle_count,
                                m.vertex_count);
    const auto& last = meshlets.back();
    vertex_indices.resize(last.vertex_offset + last.vertex_count);
    triangles.resize(last.triangle_offset + last.triangle_count * 3);
    triangles.resize((triangles.size() + 3) & ~size_t{3});
    std::vector<glm::vec3> positions(count);
    std::vector<uint32_t> normals(count);
    std::vector<uint32_t> uvs(count);
    std::vector<uint32_t> tangents(count);
    for (size_t i = 0; i < count; ++i)
    {
        positions[i] = optimized[i].position;
        normals[i] = optimized[i].normal;
        uvs[i] = optimized[i].uv;
        tangents[i] = optimized[i].tangent;
    }
    auto position_buffer_result = Upload(positions.data(), positions.size() * sizeof(glm::vec3));
    if (!position_buffer_result) return std::unexpected(position_buffer_result.error());
    m_position_buffer = std::move(*position_buffer_result);
    auto normal_buffer_result = Upload(normals.data(), normals.size() * sizeof(uint32_t));
    if (!normal_buffer_result) return std::unexpected(normal_buffer_result.error());
    m_normal_buffer = std::move(*normal_buffer_result);
    auto uv_buffer_result = Upload(uvs.data(), uvs.size() * sizeof(uint32_t));
    if (!uv_buffer_result) return std::unexpected(uv_buffer_result.error());
    m_uv_buffer = std::move(*uv_buffer_result);
    auto tangent_buffer_result = Upload(tangents.data(), tangents.size() * sizeof(uint32_t));
    if (!tangent_buffer_result) return std::unexpected(tangent_buffer_result.error());
    m_tangent_buffer = std::move(*tangent_buffer_result);
    auto vertex_index_buffer_result =
        Upload(vertex_indices.data(), vertex_indices.size() * sizeof(unsigned int));
    if (!vertex_index_buffer_result) return std::unexpected(vertex_index_buffer_result.error());
    m_vertex_index_buffer = std::move(*vertex_index_buffer_result);
    auto triangle_buffer_result = Upload(triangles.data(), triangles.size());
    if (!triangle_buffer_result) return std::unexpected(triangle_buffer_result.error());
    m_triangle_buffer = std::move(*triangle_buffer_result);
    auto meshlet_buffer_result =
        Upload(meshlets.data(), meshlets.size() * sizeof(meshopt_Meshlet));
    if (!meshlet_buffer_result) return std::unexpected(meshlet_buffer_result.error());
    m_meshlet_buffer = std::move(*meshlet_buffer_result);
    m_vertex_count = static_cast<uint32_t>(count);
    m_triangle_count = static_cast<uint32_t>(indices.size() / 3);
    m_meshlet_count = static_cast<uint32_t>(meshlet_count);
    return {};
}

RM::Result<RM::Buffer*> Mesh::GenerateIndexBuffer() const
{
    auto meshlets_mapped = m_meshlet_buffer->Map();
    if (!meshlets_mapped) return std::unexpected(meshlets_mapped.error());
    const auto* meshlets = static_cast<const meshopt_Meshlet*>(*meshlets_mapped);
    auto vertices_mapped = m_vertex_index_buffer->Map();
    if (!vertices_mapped) return std::unexpected(vertices_mapped.error());
    const auto* vertices = static_cast<const uint32_t*>(*vertices_mapped);
    auto triangles_mapped = m_triangle_buffer->Map();
    if (!triangles_mapped) return std::unexpected(triangles_mapped.error());
    const auto* triangles = static_cast<const uint8_t*>(*triangles_mapped);
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(m_triangle_count) * 3);
    for (uint32_t i = 0; i < m_meshlet_count; ++i)
    {
        const auto& m = meshlets[i];
        for (uint32_t j = 0; j < m.triangle_count * 3; ++j)
            indices.push_back(vertices[m.vertex_offset + triangles[m.triangle_offset + j]]);
    }
    meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), m_vertex_count);
    return Upload(indices.data(), indices.size() * sizeof(uint32_t));
}

Mesh::~Mesh()
{
    if (!m_device) return;
    m_device->DestroyImmediate(m_position_buffer);
    m_device->DestroyImmediate(m_normal_buffer);
    m_device->DestroyImmediate(m_uv_buffer);
    m_device->DestroyImmediate(m_tangent_buffer);
    m_device->DestroyImmediate(m_vertex_index_buffer);
    m_device->DestroyImmediate(m_triangle_buffer);
    m_device->DestroyImmediate(m_meshlet_buffer);
}
