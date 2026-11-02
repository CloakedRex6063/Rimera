#include "mesh_shader_scene.hpp"
#include "fileio.hpp"
#include "rimera_buffer.hpp"
#include "cstring"
#include "rimera_structs.hpp"
#include "expected"

RM::Result<MeshShaderScene*> MeshShaderScene::Create(RM::Device* device,
                                                     const std::filesystem::path& path)
{
    auto* object = new (std::nothrow) MeshShaderScene();
    if (!object) return std::unexpected("Object allocation failed");
    auto result = object->Initialize(device, path);
    if (!result)
    {
        auto error = std::move(result.error());
        delete object;
        return std::unexpected(std::move(error));
    }
    return object;
}

template <class T>
RM::Result<void> MeshShaderScene::Upload(const std::vector<T>& values, RM::Buffer*& buffer)
{
    if (values.empty() || values.size() > UINT32_MAX / sizeof(T))
        return std::unexpected("Invalid model buffer size");
    auto result = m_device->CreateBuffer({
        .size = static_cast<uint32_t>(values.size() * sizeof(T)),
        .data = values.data(),
    });
    if (!result) return std::unexpected(result.error());
    buffer = *result;
    m_residency->AddAllocation(buffer);
    return {};
}

RM::Result<void> MeshShaderScene::Initialize(RM::Device* device, const std::filesystem::path& path)
{
    if (!device) return std::unexpected("Scene requires a device");
    m_device = device;
    m_residency = device->GetCoreResidencySet();
    auto model_result = Model::Create(m_device, path, m_geometry);
    if (!model_result) return std::unexpected(model_result.error());
    m_model = *model_result;
    for (auto* image : m_model->GetTextures()) m_residency->AddAllocation(image->GetTexture());
    auto sampler_result = m_device->CreateSampler({});
    if (!sampler_result) return std::unexpected(sampler_result.error());
    m_sampler = *sampler_result;

    std::vector<Transform> transforms;
    transforms.reserve(m_model->GetNodes().size());
    for (const auto& node : m_model->GetNodes())
        transforms.push_back({.world = node.world_transform,
                              .normal = glm::transpose(glm::inverse(node.world_transform))});
    std::vector<DrawData> draw_data;
    std::vector<Bounds> bounds;
    auto visit_result = m_model->ForEachMeshlet(
        [&](const Mesh& mesh, uint32_t meshlet, uint32_t node_index) -> RM::Result<void>
        {
            const auto& source = m_geometry.meshlets[meshlet];
            draw_data.push_back({.transform_index = node_index,
                                 .vertex_offset = source.vertex_offset,
                                 .triangle_offset = source.triangle_offset,
                                 .material_index = static_cast<uint16_t>(mesh.material_index),
                                 .vertex_count = source.vertex_count,
                                 .triangle_count = source.triangle_count});
            bounds.push_back(m_geometry.bounds[meshlet]);
            return {};
        });
    if (!visit_result) return visit_result;
    if (draw_data.empty() || draw_data.size() > kMaxDraws)
        return std::unexpected("Model requires between one and 2097120 meshlets");
    if (m_model->GetMaterials().size() > UINT16_MAX)
        return std::unexpected("Model has too many materials");
    m_meshlet_count = static_cast<uint32_t>(draw_data.size());
    m_mesh_dispatch = {
        .x = (m_meshlet_count + kAmplificationGroupSize - 1) / kAmplificationGroupSize,
        .y = 1,
        .z = 1};
    m_geometry.triangles.resize((m_geometry.triangles.size() + 3) & ~size_t{3});
    if (auto result = Upload(draw_data, m_draw_buffer); !result) return result;
    if (auto result = Upload(bounds, m_bounds_buffer); !result) return result;
    if (auto result = Upload(m_geometry.positions, m_position_buffer); !result) return result;
    if (auto result = Upload(m_geometry.uvs, m_uv_buffer); !result) return result;
    if (auto result = Upload(m_geometry.normals, m_normal_buffer); !result) return result;
    if (auto result = Upload(m_geometry.tangents, m_tangent_buffer); !result) return result;
    if (auto result = Upload(m_geometry.vertex_indices, m_vertex_index_buffer); !result)
        return result;
    if (auto result = Upload(m_geometry.triangles, m_triangle_buffer); !result) return result;
    m_geometry = {};
    if (auto result = Upload(transforms, m_transform_buffer); !result) return result;
    if (auto result = Upload(m_model->GetMaterials(), m_material_buffer); !result) return result;

    std::vector<uint32_t> texture_indices;
    for (auto* image : m_model->GetTextures())
        texture_indices.push_back(image->GetTexture()->GetResourceIndex());
    if (texture_indices.empty()) texture_indices.push_back(UINT32_MAX);
    if (auto result = Upload(texture_indices, m_texture_buffer); !result) return result;

    m_scene_constants = {.draw_buffer_index = m_draw_buffer->GetResourceIndex(),
                         .transform_buffer_index = m_transform_buffer->GetResourceIndex(),
                         .material_buffer_index = m_material_buffer->GetResourceIndex(),
                         .texture_buffer_index = m_texture_buffer->GetResourceIndex(),
                         .sampler_index = m_sampler->GetSamplerIndex(),
                         .position_buffer = m_position_buffer->GetResourceIndex(),
                         .uv_buffer = m_uv_buffer->GetResourceIndex(),
                         .normal_buffer = m_normal_buffer->GetResourceIndex(),
                         .tangent_buffer = m_tangent_buffer->GetResourceIndex(),
                         .vertex_index_buffer = m_vertex_index_buffer->GetResourceIndex(),
                         .triangle_buffer = m_triangle_buffer->GetResourceIndex(),
                         .bounds_buffer = m_bounds_buffer->GetResourceIndex(),
                         .meshlet_count = m_meshlet_count};
    for (uint32_t i = 0; i < kFramesInFlight; ++i)
    {
        auto scene_buffer_result =
            m_device->CreateBuffer({.size = sizeof(SceneData),
                                    .data = &m_scene_constants,
                                    .memory_type = RM::MemoryType::HostVisible});
        if (!scene_buffer_result) return std::unexpected(scene_buffer_result.error());
        m_scene_buffers[i] = *scene_buffer_result;
        auto mapped = m_scene_buffers[i]->Map();
        if (!mapped) return std::unexpected(mapped.error());
        m_scene_data[i] = static_cast<SceneData*>(*mapped);
        m_residency->AddAllocation(m_scene_buffers[i]);
    }

    if (auto result = CreatePipelines(); !result) return result;
    m_residency->Commit();
    return {};
}

RM::Result<void> MeshShaderScene::CreatePipelines()
{
    const std::string shaders_dir = FileIO::ResourcePath() + "shaders";
    const std::string_view include_paths[] = {shaders_dir};
    auto compiler_result = RM::CreateCompiler({.include_paths = include_paths});
    if (!compiler_result) return std::unexpected(compiler_result.error());
    m_compiler = *compiler_result;

    auto vis_amp_shader = CompileShader("vis.hlsl", "AMMain", RM::ShaderType::Amplification);
    if (!vis_amp_shader) return std::unexpected(vis_amp_shader.error());
    auto vis_mesh_shader = CompileShader("vis.hlsl", "MSMain", RM::ShaderType::Mesh);
    if (!vis_mesh_shader) return std::unexpected(vis_mesh_shader.error());
    auto vis_fragment_shader = CompileShader("vis.hlsl", "PSMain", RM::ShaderType::Fragment);
    if (!vis_fragment_shader) return std::unexpected(vis_fragment_shader.error());
    auto interp_shader = CompileShader("interp.hlsl", "CSMain", RM::ShaderType::Compute);
    if (!interp_shader) return std::unexpected(interp_shader.error());
    auto shading_shader = CompileShader("shading.hlsl", "CSMain", RM::ShaderType::Compute);
    if (!shading_shader) return std::unexpected(shading_shader.error());
    auto lighting_shader = CompileShader("lighting.hlsl", "CSMain", RM::ShaderType::Compute);
    if (!lighting_shader) return std::unexpected(lighting_shader.error());
    auto debug_shader = CompileShader("debug.hlsl", "CSMain", RM::ShaderType::Compute);
    if (!debug_shader) return std::unexpected(debug_shader.error());
    auto tonemap_shader = CompileShader("tonemap.hlsl", "CSMain", RM::ShaderType::Compute);
    if (!tonemap_shader) return std::unexpected(tonemap_shader.error());

    RM::MeshGraphicsPipelineCreateInfo pipeline_info{
        .ampl_shader = *vis_amp_shader,
        .mesh_shader = *vis_mesh_shader,
        .fragment_shader = *vis_fragment_shader,
        .rtv_formats = {kVisibilityFormat},
        .dsv_format = kDepthFormat,
        .depth_stencil_state =
            {
                .depth_enable = true,
                .depth_write_enable = true,
                .depth_test = RM::ComparisonFunc::Less,
            },
        .rasterizer_state = {.cull_mode = RM::CullMode::None},
        .polygon_mode = RM::PolygonMode::Triangle,
    };
    auto pipeline_result = m_device->CreatePipeline(pipeline_info);
    if (!pipeline_result) return std::unexpected(pipeline_result.error());
    m_vis_pipeline = *pipeline_result;

    auto interp_result = m_device->CreatePipeline(RM::ComputePipelineCreateInfo{
        .shader = *interp_shader,
    });
    if (!interp_result) return std::unexpected(interp_result.error());
    m_interp_pipeline = *interp_result;

    auto shading_result = m_device->CreatePipeline(RM::ComputePipelineCreateInfo{
        .shader = *shading_shader,
    });
    if (!shading_result) return std::unexpected(shading_result.error());
    m_shading_pipeline = *shading_result;

    auto lighting_result = m_device->CreatePipeline(RM::ComputePipelineCreateInfo{
        .shader = *lighting_shader,
    });
    if (!lighting_result) return std::unexpected(lighting_result.error());
    m_lighting_pipeline = *lighting_result;

    auto debug_result = m_device->CreatePipeline(RM::ComputePipelineCreateInfo{
        .shader = *debug_shader,
    });
    if (!debug_result) return std::unexpected(debug_result.error());
    m_debug_pipeline = *debug_result;

    auto tonemap_result = m_device->CreatePipeline(RM::ComputePipelineCreateInfo{
        .shader = *tonemap_shader,
    });
    if (!tonemap_result) return std::unexpected(tonemap_result.error());
    m_tonemap_pipeline = *tonemap_result;
    ReleaseShaders();
    return {};
}

RM::Result<const RM::Shader*> MeshShaderScene::CompileShader(std::string_view file,
                                                             std::string_view entry_point,
                                                             RM::ShaderType type)
{
    auto source = FileIO::Read(FileIO::ResourcePath() + "shaders/" + std::string(file));
    if (!source) return std::unexpected(source.error());
    auto shader = m_compiler->Compile({.code = *source, .entry_point = entry_point}, type);
    if (!shader) return std::unexpected(std::string(file) + ": " + shader.error());
    m_shaders.push_back(*shader);
    return *shader;
}

void MeshShaderScene::ReleaseShaders()
{
    for (const auto* shader : m_shaders) RM::DestroyShader(shader);
    m_shaders.clear();
    if (m_compiler) RM::DestroyCompiler(m_compiler);
    m_compiler = nullptr;
}

RM::Buffer* MeshShaderScene::UpdateScene(const glm::mat4& view_projection,
                                         const glm::vec3& camera_position)
{
    m_scene_frame = (m_scene_frame + 1) % kFramesInFlight;
    m_scene_constants.view_projection = view_projection;
    m_scene_constants.inverse_view_projection = glm::inverse(view_projection);
    m_scene_constants.camera_position = camera_position;
    std::memcpy(m_scene_data[m_scene_frame], &m_scene_constants, sizeof(SceneData));
    return m_scene_buffers[m_scene_frame];
}

MeshShaderScene::~MeshShaderScene()
{
    ReleaseShaders();
    if (!m_device) return;
    for (auto* buffer : {m_draw_buffer,
                         m_transform_buffer,
                         m_material_buffer,
                         m_texture_buffer,
                         m_bounds_buffer,
                         m_position_buffer,
                         m_uv_buffer,
                         m_normal_buffer,
                         m_tangent_buffer,
                         m_vertex_index_buffer,
                         m_triangle_buffer})
        if (buffer) m_device->DestroyImmediate(buffer);
    for (auto* buffer : m_scene_buffers)
        if (buffer) m_device->DestroyImmediate(buffer);
    for (auto* pipeline : {m_vis_pipeline,
                           m_interp_pipeline,
                           m_shading_pipeline,
                           m_lighting_pipeline,
                           m_debug_pipeline,
                           m_tonemap_pipeline})
        if (pipeline) m_device->DestroyImmediate(pipeline);
    if (m_sampler) m_device->DestroyImmediate(m_sampler);
    delete m_model;
}
