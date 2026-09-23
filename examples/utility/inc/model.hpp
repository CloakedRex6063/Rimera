#pragma once
#include "mesh.hpp"
#include "texture.hpp"
#include "glm/glm.hpp"
#include "string"
#include "vector"

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
    Model(RM::Device* device, const std::filesystem::path& path);
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    Model(Model&&) noexcept = default;
    Model& operator=(Model&&) noexcept = default;
    [[nodiscard]] const std::vector<ModelNode>& GetNodes() const { return m_nodes; }
    [[nodiscard]] const std::vector<Texture>& GetTextures() const { return m_textures; }
    [[nodiscard]] const std::vector<Mesh>& GetMeshes() const { return m_meshes; }
    [[nodiscard]] const std::vector<Material>& GetMaterials() const { return m_materials; }
    [[nodiscard]] const std::vector<uint32_t>& GetSceneRoots() const { return m_scene_roots; }
private:
    std::vector<ModelNode> m_nodes;
    std::vector<Texture> m_textures;
    std::vector<Mesh> m_meshes;
    std::vector<Material> m_materials;
    std::vector<uint32_t> m_scene_roots;
};
