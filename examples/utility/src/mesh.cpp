#include "mesh.hpp"
#include "rimera_device.hpp"
#include "meshoptimizer.h"
#include "stdexcept"
#include "vector"

void BufferDeleter::operator()(RM::Buffer* buffer) const { if (buffer) device->DestroyBuffer(buffer); }

namespace
{
OwnedBuffer Upload(RM::Device* device, const void* data, size_t bytes)
{
    if (!bytes || bytes > UINT32_MAX) throw std::runtime_error("Invalid mesh buffer size");
    return OwnedBuffer(device->CreateBuffer({static_cast<uint32_t>(bytes), data}), BufferDeleter{device});
}
}

Mesh::Mesh(RM::Device* device, std::span<const Vertex> vertices, std::span<const uint32_t> indices,
           uint32_t material_index, uint16_t texture_index)
    : m_device(device), m_material_index(material_index), m_texture_index(texture_index)
{
    if (!device || vertices.empty() || indices.empty() || indices.size() % 3 ||
        vertices.size_bytes() > UINT32_MAX || indices.size_bytes() > UINT32_MAX)
        throw std::runtime_error("Invalid triangle mesh");
    for (auto i : indices) if (i >= vertices.size()) throw std::runtime_error("Mesh index out of range");
    std::vector<unsigned int> remap(vertices.size());
    auto count = meshopt_generateVertexRemap(remap.data(), indices.data(), indices.size(), vertices.data(), vertices.size(), sizeof(Vertex));
    std::vector<Vertex> optimized(count);
    std::vector<unsigned int> optimized_indices(indices.size());
    meshopt_remapVertexBuffer(optimized.data(), vertices.data(), vertices.size(), sizeof(Vertex), remap.data());
    meshopt_remapIndexBuffer(optimized_indices.data(), indices.data(), indices.size(), remap.data());
    meshopt_optimizeVertexCache(optimized_indices.data(), optimized_indices.data(), indices.size(), count);
    meshopt_optimizeVertexFetch(optimized.data(), optimized_indices.data(), indices.size(), optimized.data(), count, sizeof(Vertex));
    constexpr size_t max_vertices = 64, max_triangles = 124;
    std::vector<meshopt_Meshlet> meshlets(meshopt_buildMeshletsBound(indices.size(), max_vertices, max_triangles));
    std::vector<unsigned int> vertex_indices(indices.size());
    std::vector<unsigned char> triangles(indices.size());
    auto meshlet_count = meshopt_buildMeshlets(meshlets.data(), vertex_indices.data(), triangles.data(),
        optimized_indices.data(), indices.size(), &optimized[0].position.x, count, sizeof(Vertex), max_vertices, max_triangles, 0);
    meshlets.resize(meshlet_count);
    for (const auto& m : meshlets)
        meshopt_optimizeMeshlet(vertex_indices.data() + m.vertex_offset, triangles.data() + m.triangle_offset, m.triangle_count, m.vertex_count);
    const auto& last = meshlets.back();
    vertex_indices.resize(last.vertex_offset + last.vertex_count);
    triangles.resize(last.triangle_offset + last.triangle_count * 3);
    m_vertex_buffer = Upload(device, optimized.data(), optimized.size() * sizeof(Vertex));
    m_vertex_index_buffer = Upload(device, vertex_indices.data(), vertex_indices.size() * sizeof(unsigned int));
    m_triangle_buffer = Upload(device, triangles.data(), triangles.size());
    m_meshlet_buffer = Upload(device, meshlets.data(), meshlets.size() * sizeof(meshopt_Meshlet));
    m_vertex_count = static_cast<uint32_t>(count);
    m_triangle_count = static_cast<uint32_t>(indices.size() / 3);
    m_meshlet_count = static_cast<uint32_t>(meshlet_count);
}

RM::Buffer* Mesh::GenerateIndexBuffer() const
{
    const auto* meshlets = static_cast<const meshopt_Meshlet*>(m_meshlet_buffer->GetContents());
    const auto* vertices = static_cast<const uint32_t*>(m_vertex_index_buffer->GetContents());
    const auto* triangles = static_cast<const uint8_t*>(m_triangle_buffer->GetContents());
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(m_triangle_count) * 3);
    for (uint32_t i = 0; i < m_meshlet_count; ++i)
    {
        const auto& m = meshlets[i];
        for (uint32_t j = 0; j < m.triangle_count * 3; ++j)
            indices.push_back(vertices[m.vertex_offset + triangles[m.triangle_offset + j]]);
    }
    meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), m_vertex_count);
    return Upload(m_device, indices.data(), indices.size() * sizeof(uint32_t)).release();
}
