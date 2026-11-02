#include "mesh.hpp"
#include "meshoptimizer.h"

RM::Result<Mesh> Mesh::Create(SceneGeometry& geometry,
                              std::span<const Vertex> vertices,
                              std::span<const uint32_t> indices,
                              uint32_t material_index,
                              uint16_t texture_index)
{
    if (vertices.empty() || indices.empty() || indices.size() % 3 ||
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
    const size_t base_vertex = geometry.positions.size();
    const size_t base_vertex_index = geometry.vertex_indices.size();
    const size_t base_triangle = geometry.triangles.size();
    const auto& last = meshlets.back();
    if (base_vertex + count > UINT32_MAX ||
        base_vertex_index + last.vertex_offset + last.vertex_count > UINT32_MAX ||
        base_triangle + last.triangle_offset + last.triangle_count * 3 > UINT32_MAX ||
        geometry.meshlets.size() + meshlet_count > UINT32_MAX)
        return std::unexpected("Scene geometry exceeds addressable range");
    Mesh mesh{.first_meshlet = static_cast<uint32_t>(geometry.meshlets.size()),
              .meshlet_count = static_cast<uint32_t>(meshlet_count),
              .vertex_count = static_cast<uint32_t>(count),
              .triangle_count = static_cast<uint32_t>(indices.size() / 3),
              .material_index = material_index,
              .texture_index = texture_index};
    for (const auto& m : meshlets)
    {
        meshopt_optimizeMeshlet(vertex_indices.data() + m.vertex_offset,
                                triangles.data() + m.triangle_offset,
                                m.triangle_count,
                                m.vertex_count);
        const auto bounds = meshopt_computeMeshletBounds(vertex_indices.data() + m.vertex_offset,
                                                         triangles.data() + m.triangle_offset,
                                                         m.triangle_count,
                                                         &optimized[0].position.x,
                                                         count,
                                                         sizeof(Vertex));
        geometry.bounds.push_back(
            {.center = {bounds.center[0], bounds.center[1], bounds.center[2]},
             .radius = bounds.radius,
             .cone_apex = {bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2]},
             .cone_axis = {bounds.cone_axis_s8[0], bounds.cone_axis_s8[1], bounds.cone_axis_s8[2]},
             .cone_cutoff = bounds.cone_cutoff_s8});
        geometry.meshlets.push_back(
            {.vertex_offset = static_cast<uint32_t>(base_vertex_index + m.vertex_offset),
             .triangle_offset = static_cast<uint32_t>(base_triangle + m.triangle_offset),
             .vertex_count = static_cast<uint8_t>(m.vertex_count),
             .triangle_count = static_cast<uint8_t>(m.triangle_count)});
    }
    for (size_t i = 0; i < last.vertex_offset + last.vertex_count; ++i)
        geometry.vertex_indices.push_back(static_cast<uint32_t>(base_vertex + vertex_indices[i]));
    geometry.triangles.insert(geometry.triangles.end(),
                              triangles.begin(),
                              triangles.begin() + last.triangle_offset + last.triangle_count * 3);
    for (size_t i = 0; i < count; ++i)
    {
        geometry.positions.push_back(optimized[i].position);
        geometry.normals.push_back(optimized[i].normal);
        geometry.uvs.push_back(optimized[i].uv);
        geometry.tangents.push_back(optimized[i].tangent);
    }
    return mesh;
}
