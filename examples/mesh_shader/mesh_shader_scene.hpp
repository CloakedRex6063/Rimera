#pragma once
#include "model.hpp"
#include "rimera_device.hpp"
#include "rimera_compiler.hpp"
#include "array"
#include "cstddef"
#include "filesystem"
#include "string_view"
#include "vector"
#include "glm/glm.hpp"

class MeshShaderScene
{
public:
    struct TonemapData
    {
        float headroom;
        float sdr_white_scale;
        uint32_t hdr;
    };

    static_assert(sizeof(TonemapData) == 12);

    struct DirLight
    {
        glm::vec3 direction;
        float intensity;
        glm::vec3 color;
        float padding{};
    };

    static_assert(sizeof(DirLight) == 32);

    static constexpr auto kVisibilityFormat = RM::Format::R32_UInt;
    static constexpr auto kColorFormat = RM::Format::RGBA16_Float;
    static constexpr auto kDepthFormat = RM::Format::Depth32_Float;
    static constexpr auto kUVFormat = RM::Format::R32_UInt;
    static constexpr auto kDerivFormat = RM::Format::RGBA16_Float;
    static constexpr auto kTangentFrameFormat = RM::Format::RGBA16_UInt;
    static constexpr auto kAlbedoFormat = RM::Format::RGBA8_UNorm_sRGB;
    static constexpr auto kNormalFormat = RM::Format::RG16_UNorm;
    static constexpr auto kRoughOcclusionFormat = RM::Format::RG8_UNorm;
    static constexpr auto kEmissiveFormat = RM::Format::RG11B10_Float;

    ~MeshShaderScene();
    [[nodiscard]] static RM::Result<MeshShaderScene*> Create(RM::Device* device,
                                                             const std::filesystem::path& path);
    MeshShaderScene(const MeshShaderScene&) = delete;
    MeshShaderScene& operator=(const MeshShaderScene&) = delete;
    MeshShaderScene(MeshShaderScene&&) = delete;
    MeshShaderScene& operator=(MeshShaderScene&&) = delete;

    RM::Buffer* UpdateScene(const glm::mat4& view_projection, const glm::vec3& camera_position);

    [[nodiscard]] RM::DispatchGroup GetMeshDispatch() const { return m_mesh_dispatch; }
    [[nodiscard]] RM::Pipeline* GetVisPipeline() const { return m_vis_pipeline; }
    [[nodiscard]] RM::Pipeline* GetInterpPipeline() const { return m_interp_pipeline; }
    [[nodiscard]] RM::Pipeline* GetShadingPipeline() const { return m_shading_pipeline; }
    [[nodiscard]] RM::Pipeline* GetLightingPipeline() const { return m_lighting_pipeline; }
    [[nodiscard]] RM::Pipeline* GetDebugPipeline() const { return m_debug_pipeline; }
    [[nodiscard]] RM::Pipeline* GetTonemapPipeline() const { return m_tonemap_pipeline; }

private:
    struct Transform
    {
        glm::mat4 world;
        glm::mat4 normal;
    };

    static_assert(sizeof(Transform) == 128);

    struct DrawData
    {
        uint32_t transform_index;
        uint32_t vertex_offset;
        uint32_t triangle_offset;

        uint16_t material_index;
        uint8_t vertex_count;
        uint8_t triangle_count;
    };

    static_assert(sizeof(DrawData) == 16);

    struct SceneData
    {
        uint32_t draw_buffer_index;
        uint32_t transform_buffer_index;
        uint32_t material_buffer_index;
        uint32_t texture_buffer_index;
        uint32_t sampler_index;
        uint32_t position_buffer;
        uint32_t uv_buffer;
        uint32_t normal_buffer;
        uint32_t tangent_buffer;
        uint32_t vertex_index_buffer;
        uint32_t triangle_buffer;
        uint32_t bounds_buffer;
        glm::vec3 camera_position{};
        uint32_t meshlet_count;
        glm::mat4 view_projection{1};
        glm::mat4 inverse_view_projection{1};
    };

    static_assert(sizeof(SceneData) == 192 && offsetof(SceneData, view_projection) == 64);

    static constexpr uint32_t kMaxGroupsPerDimension = 65535;
    static constexpr uint32_t kAmplificationGroupSize = 32;
    static constexpr uint32_t kMaxDraws = kMaxGroupsPerDimension * kAmplificationGroupSize;
    static constexpr uint32_t kFramesInFlight = 3;

    MeshShaderScene() = default;
    RM::Result<void> Initialize(RM::Device* device, const std::filesystem::path& path);
    RM::Result<void> CreatePipelines();
    template <class T>
    RM::Result<void> Upload(const std::vector<T>& values, RM::Buffer*& buffer);
    RM::Result<const RM::Shader*> CompileShader(std::string_view file,
                                                std::string_view entry_point,
                                                RM::ShaderType type);
    void ReleaseShaders();

    RM::Device* m_device = nullptr;
    RM::ResidencySet* m_residency = nullptr;
    Model* m_model = nullptr;
    SceneGeometry m_geometry;
    uint32_t m_meshlet_count = 0;
    RM::DispatchGroup m_mesh_dispatch{};
    std::array<RM::Buffer*, kFramesInFlight> m_scene_buffers{};
    std::array<SceneData*, kFramesInFlight> m_scene_data{};
    SceneData m_scene_constants{};
    uint32_t m_scene_frame = 0;
    RM::Buffer* m_transform_buffer = nullptr;
    RM::Buffer* m_material_buffer = nullptr;
    RM::Buffer* m_texture_buffer = nullptr;
    RM::Buffer* m_draw_buffer = nullptr;
    RM::Buffer* m_bounds_buffer = nullptr;
    RM::Buffer* m_position_buffer = nullptr;
    RM::Buffer* m_uv_buffer = nullptr;
    RM::Buffer* m_normal_buffer = nullptr;
    RM::Buffer* m_tangent_buffer = nullptr;
    RM::Buffer* m_vertex_index_buffer = nullptr;
    RM::Buffer* m_triangle_buffer = nullptr;
    RM::Pipeline* m_vis_pipeline = nullptr;
    RM::Pipeline* m_interp_pipeline = nullptr;
    RM::Pipeline* m_shading_pipeline = nullptr;
    RM::Pipeline* m_lighting_pipeline = nullptr;
    RM::Pipeline* m_debug_pipeline = nullptr;
    RM::Pipeline* m_tonemap_pipeline = nullptr;
    RM::Sampler* m_sampler = nullptr;
    RM::Compiler* m_compiler = nullptr;
    std::vector<const RM::Shader*> m_shaders;
};
