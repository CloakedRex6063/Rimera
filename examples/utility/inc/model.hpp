#pragma once
#include "mesh.hpp"
#include "texture.hpp"
#include "glm/glm.hpp"
#include "fastgltf/types.hpp"
#include "string"
#include "string_view"
#include "vector"

struct SMikkTSpaceContext;

struct ModelNode
{
    std::string name;
    uint32_t parent = UINT32_MAX;
    std::vector<uint32_t> children;
    std::vector<uint32_t> meshes;
    glm::mat4 local_transform{1}, world_transform{1};
};

class Model
{
public:
    ~Model();
    [[nodiscard]] static RM::Result<Model*> Create(RM::Device* device,
                                                   const std::filesystem::path& path);
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    Model(Model&&) = delete;
    Model& operator=(Model&&) = delete;
    [[nodiscard]] const std::vector<ModelNode>& GetNodes() const { return m_nodes; }
    [[nodiscard]] const std::vector<Texture*>& GetTextures() const { return m_textures; }
    [[nodiscard]] const std::vector<Mesh*>& GetMeshes() const { return m_meshes; }
    [[nodiscard]] const std::vector<Material>& GetMaterials() const { return m_materials; }
    [[nodiscard]] const std::vector<uint32_t>& GetSceneRoots() const { return m_scene_roots; }
private:
    struct SourceVertex;
    struct TangentData;

    static RM::Result<std::span<const std::byte>> Bytes(const fastgltf::DataSource& source);
    static RM::Result<void> CheckData(const fastgltf::Asset& asset);
    template <class T, class F>
    static RM::Result<bool> ReadAttribute(const fastgltf::Asset& asset,
                                         const fastgltf::Primitive& primitive,
                                         std::string_view name,
                                         size_t count,
                                         fastgltf::AccessorType type,
                                         F write);
    static RM::Result<void> ValidateVertex(const SourceVertex& vertex);
    static float PackDirection(const float* direction, glm::vec3 fallback);
    static RM::Result<Vertex> PackVertex(const SourceVertex& source);
    static SourceVertex& Corner(const SMikkTSpaceContext* context, int face, int vertex);
    static RM::Result<void> GenerateTangents(std::vector<SourceVertex>& vertices, unsigned uv);
    static RM::Result<Mesh*> LoadMesh(RM::Device* device,
                                     const fastgltf::Asset& asset,
                                     const fastgltf::Primitive& primitive,
                                     uint32_t material_index,
                                     const Material& material);
    Model() = default;
    RM::Result<void> Initialize(RM::Device* device, const std::filesystem::path& path);

    std::vector<ModelNode> m_nodes;
    std::vector<Texture*> m_textures;
    std::vector<Mesh*> m_meshes;
    std::vector<Material> m_materials;
    std::vector<uint32_t> m_scene_roots;
};
