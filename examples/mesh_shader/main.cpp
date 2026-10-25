#include "imgui.h"
#include "rimera_device.hpp"
#include "rimera_compiler.hpp"
#include "window.hpp"
#include "camera.hpp"
#include "model.hpp"
#include "fileio.hpp"
#include "rimera_builders.hpp"
#include "rimera_render_graph.hpp"
#include "chrono"
#include "imgui_layer.hpp"

struct MeshShaderScene
{
    struct Transform
    {
        glm::mat4 world;
        glm::mat4 normal;
    };

    static_assert(sizeof(Transform) == 128);

    struct DrawData
    {
        uint32_t meshlet_index;
        uint32_t position_index;
        uint32_t normal_index;
        uint32_t uv_index;
        uint32_t tangent_index;
        uint32_t vertex_indices_index;
        uint32_t triangle_index;
        uint32_t meshlet_buffer_index;
        uint32_t transform_index;
        uint32_t material_index;
    };

    static_assert(sizeof(DrawData) == 40);

    struct SceneData
    {
        uint32_t draw_buffer_index;
        uint32_t transform_buffer_index;
        uint32_t material_buffer_index;
        uint32_t texture_buffer_index;
        uint32_t sampler_index;
        uint32_t padding[3]{};
        glm::mat4 view_projection{1};
    };

    static_assert(sizeof(SceneData) == 96);

    static constexpr uint32_t kFramesInFlight = 3;

    struct TonemapData
    {
        glm::uvec2 size;
        float headroom;
        float sdr_white_scale;
        uint32_t hdr;
    };

    static_assert(sizeof(TonemapData) == 20);

    static constexpr auto kVisibilityFormat = RM::Format::R32_UInt;
    static constexpr auto kColorFormat = RM::Format::RGBA16_Float;
    static constexpr auto kDepthFormat = RM::Format::Depth32_Float;
    static constexpr auto kUVFormat = RM::Format::R32_UInt;
    static constexpr auto kDerivFormat = RM::Format::RGBA16_Float;
    static constexpr auto kTangentFrameFormat = RM::Format::RGBA16_UInt;
    static constexpr auto kAlbedoFormat = RM::Format::RGBA16_Float;

    MeshShaderScene(RM::Device* device, Model* model, glm::uvec2 size)
        : device(device), model(model), size(size), residency(device->GetCoreResidencySet())
    {
    }

    RM::Device* device;
    Model* model;
    glm::uvec2 size;
    RM::ResidencySet* residency;
    uint32_t meshlet_count = 0;
    RM::Buffer* draw_buffer = nullptr;
    std::array<RM::Buffer*, kFramesInFlight> scene_buffers{};
    std::array<SceneData*, kFramesInFlight> scene_data{};
    SceneData scene_constants{};
    uint32_t scene_frame = 0;
    RM::Buffer* transform_buffer = nullptr;
    RM::Buffer* material_buffer = nullptr;
    RM::Buffer* texture_buffer = nullptr;
    RM::Pipeline* vis_pipeline = nullptr;
    RM::Pipeline* interp_pipeline = nullptr;
    RM::Pipeline* shading_pipeline = nullptr;
    RM::Pipeline* debug_pipeline = nullptr;
    RM::Pipeline* tonemap_pipeline = nullptr;
    RM::Sampler* sampler = nullptr;
    RM::Compiler* compiler = nullptr;
    std::vector<const RM::Shader*> shaders;

    template <class T>
    RM::Result<void> Upload(const std::vector<T>& values, RM::Buffer*& buffer)
    {
        if (values.empty() || values.size() > UINT32_MAX / sizeof(T))
            return std::unexpected("Invalid model buffer size");
        auto result = device->CreateBuffer({
            .size = static_cast<uint32_t>(values.size() * sizeof(T)),
            .data = values.data(),
        });
        if (!result) return std::unexpected(result.error());
        buffer = *result;
        residency->AddAllocation(buffer);
        return {};
    }

    RM::Result<void> Setup()
    {
        for (auto* mesh : model->GetMeshes())
        {
            residency->AddAllocation(mesh->GetPositionBuffer());
            residency->AddAllocation(mesh->GetNormalBuffer());
            residency->AddAllocation(mesh->GetUVBuffer());
            residency->AddAllocation(mesh->GetTangentBuffer());
            residency->AddAllocation(mesh->GetVertexIndexBuffer());
            residency->AddAllocation(mesh->GetTriangleBuffer());
            residency->AddAllocation(mesh->GetMeshletBuffer());
        }
        for (auto* image : model->GetTextures())
            residency->AddAllocation(image->GetTexture());
        auto sampler_result = device->CreateSampler({});
        if (!sampler_result) return std::unexpected(sampler_result.error());
        sampler = *sampler_result;

        std::vector<Transform> transforms;
        transforms.reserve(model->GetNodes().size());
        for (const auto& node : model->GetNodes())
            transforms.push_back({.world = node.world_transform,
                                  .normal = glm::transpose(glm::inverse(node.world_transform))});
        std::vector<DrawData> draw_data;
        auto visit_result = model->ForEachMeshlet(
            [&](const Mesh& mesh, uint32_t meshlet, uint32_t node_index) -> RM::Result<void>
            {
                draw_data.push_back({
                    .meshlet_index = meshlet,
                    .position_index = mesh.GetPositionBuffer()->GetResourceIndex(),
                    .normal_index = mesh.GetNormalBuffer()->GetResourceIndex(),
                    .uv_index = mesh.GetUVBuffer()->GetResourceIndex(),
                    .tangent_index = mesh.GetTangentBuffer()->GetResourceIndex(),
                    .vertex_indices_index = mesh.GetVertexIndexBuffer()->GetResourceIndex(),
                    .triangle_index = mesh.GetTriangleBuffer()->GetResourceIndex(),
                    .meshlet_buffer_index = mesh.GetMeshletBuffer()->GetResourceIndex(),
                    .transform_index = node_index,
                    .material_index = mesh.GetMaterialIndex(),
                });
                return {};
            });
        if (!visit_result) return visit_result;
        if (draw_data.empty() || draw_data.size() > 65535)
            return std::unexpected("Model requires between one and 65535 meshlets");
        meshlet_count = static_cast<uint32_t>(draw_data.size());
        if (auto result = Upload(draw_data, draw_buffer); !result) return result;
        if (auto result = Upload(transforms, transform_buffer); !result) return result;
        if (auto result = Upload(model->GetMaterials(), material_buffer); !result) return result;

        std::vector<uint32_t> texture_indices;
        for (auto* image : model->GetTextures())
            texture_indices.push_back(image->GetTexture()->GetResourceIndex());
        if (texture_indices.empty()) texture_indices.push_back(UINT32_MAX);
        if (auto result = Upload(texture_indices, texture_buffer); !result) return result;

        scene_constants = {.draw_buffer_index = draw_buffer->GetResourceIndex(),
                           .transform_buffer_index = transform_buffer->GetResourceIndex(),
                           .material_buffer_index = material_buffer->GetResourceIndex(),
                           .texture_buffer_index = texture_buffer->GetResourceIndex(),
                           .sampler_index = sampler->GetSamplerIndex()};
        for (uint32_t i = 0; i < kFramesInFlight; ++i)
        {
            auto scene_buffer_result =
                device->CreateBuffer({.size = sizeof(SceneData), .data = &scene_constants});
            if (!scene_buffer_result) return std::unexpected(scene_buffer_result.error());
            scene_buffers[i] = *scene_buffer_result;
            auto mapped = scene_buffers[i]->Map();
            if (!mapped) return std::unexpected(mapped.error());
            scene_data[i] = static_cast<SceneData*>(*mapped);
            residency->AddAllocation(scene_buffers[i]);
        }

        const std::string shaders_dir = FileIO::ResourcePath() + "shaders";
        const std::string_view include_paths[] = {shaders_dir};
        auto compiler_result = RM::CreateCompiler({.include_paths = include_paths});
        if (!compiler_result) return std::unexpected(compiler_result.error());
        compiler = *compiler_result;

        auto vis_mesh_shader = CompileShader("vis.hlsl", "MSMain", RM::ShaderType::Mesh);
        if (!vis_mesh_shader) return std::unexpected(vis_mesh_shader.error());
        auto vis_fragment_shader = CompileShader("vis.hlsl", "PSMain", RM::ShaderType::Fragment);
        if (!vis_fragment_shader) return std::unexpected(vis_fragment_shader.error());
        auto interp_shader = CompileShader("interp.hlsl", "CSMain", RM::ShaderType::Compute);
        if (!interp_shader) return std::unexpected(interp_shader.error());
        auto shading_shader = CompileShader("shading.hlsl", "CSMain", RM::ShaderType::Compute);
        if (!shading_shader) return std::unexpected(shading_shader.error());
        auto debug_shader = CompileShader("debug.hlsl", "CSMain", RM::ShaderType::Compute);
        if (!debug_shader) return std::unexpected(debug_shader.error());
        auto tonemap_shader = CompileShader("tonemap.hlsl", "CSMain", RM::ShaderType::Compute);
        if (!tonemap_shader) return std::unexpected(tonemap_shader.error());

        RM::MeshGraphicsPipelineCreateInfo pipeline_info{
            .mesh_shader = *vis_mesh_shader,
            .fragment_shader = *vis_fragment_shader,
            .rtv_formats = {kVisibilityFormat},
            .dsv_format = kDepthFormat,
            .depth_stencil_state = {
                .depth_enable = true,
                .depth_write_enable = true,
                .depth_test = RM::ComparisonFunc::Less,
            },
            .rasterizer_state = {.cull_mode = RM::CullMode::None},
            .polygon_mode = RM::PolygonMode::Triangle,
        };
        auto pipeline_result = device->CreatePipeline(pipeline_info);
        if (!pipeline_result) return std::unexpected(pipeline_result.error());
        vis_pipeline = *pipeline_result;

        auto interp_result = device->CreatePipeline(RM::ComputePipelineCreateInfo{
            .shader = *interp_shader,
        });
        if (!interp_result) return std::unexpected(interp_result.error());
        interp_pipeline = *interp_result;

        auto shading_result = device->CreatePipeline(RM::ComputePipelineCreateInfo{
            .shader = *shading_shader,
        });
        if (!shading_result) return std::unexpected(shading_result.error());
        shading_pipeline = *shading_result;

        auto debug_result = device->CreatePipeline(RM::ComputePipelineCreateInfo{
            .shader = *debug_shader,
        });
        if (!debug_result) return std::unexpected(debug_result.error());
        debug_pipeline = *debug_result;

        auto tonemap_result = device->CreatePipeline(RM::ComputePipelineCreateInfo{
            .shader = *tonemap_shader,
        });
        if (!tonemap_result) return std::unexpected(tonemap_result.error());
        tonemap_pipeline = *tonemap_result;
        ReleaseShaders();
        residency->Commit();
        return {};
    }

    RM::Result<const RM::Shader*> CompileShader(std::string_view file,
                                                std::string_view entry_point,
                                                RM::ShaderType type)
    {
        auto source = FileIO::Read(FileIO::ResourcePath() + "shaders/" + std::string(file));
        if (!source) return std::unexpected(source.error());
        auto shader = compiler->Compile({.code = *source, .entry_point = entry_point}, type);
        if (!shader) return std::unexpected(std::string(file) + ": " + shader.error());
        shaders.push_back(*shader);
        return *shader;
    }

    void ReleaseShaders()
    {
        for (const auto* shader : shaders) RM::DestroyShader(shader);
        shaders.clear();
        if (compiler) RM::DestroyCompiler(compiler);
        compiler = nullptr;
    }

    RM::Buffer* UpdateScene(const glm::mat4& view_projection)
    {
        scene_frame = (scene_frame + 1) % kFramesInFlight;
        scene_constants.view_projection = view_projection;
        std::memcpy(scene_data[scene_frame], &scene_constants, sizeof(SceneData));
        return scene_buffers[scene_frame];
    }

    void Cleanup()
    {
        ReleaseShaders();
        for (auto* buffer : {draw_buffer, transform_buffer, material_buffer, texture_buffer})
            device->DestroyImmediate(buffer);
        for (auto* buffer : scene_buffers) device->DestroyImmediate(buffer);
        if (vis_pipeline) device->DestroyImmediate(vis_pipeline);
        if (debug_pipeline) device->DestroyImmediate(debug_pipeline);
        if (tonemap_pipeline) device->DestroyImmediate(tonemap_pipeline);
        if (sampler) device->DestroyImmediate(sampler);
        delete model;
    }
};

int main()
{
    auto window_result = Window::Create();
    if (!window_result)
    {
        std::cerr << window_result.error() << '\n';
        return 1;
    }
    auto* window = *window_result;
    auto size = window->GetFramebufferSize();
    auto result_device = RM::CreateDevice({});
    if (!result_device)
    {
        std::cerr << result_device.error() << '\n';
        delete window;
        return 1;
    }
    auto* device = *result_device;
    auto result_queue = device->CreateQueue(RM::QueueType::Graphics);
    if (!result_queue)
    {
        std::cerr << result_queue.error() << '\n';
        RM::DestroyDevice(device);
        delete window;
        return 1;
    }
    auto* queue = *result_queue;
    auto result_swapchain = device->CreateSwapchain(queue,
                                                    {
                                                        .window = window->GetNativeHandle(),
                                                        .width = size.x,
                                                        .height = size.y,
                                                    });
    if (!result_swapchain)
    {
        std::cerr << result_swapchain.error() << '\n';
        device->DestroyImmediate(queue);
        RM::DestroyDevice(device);
        delete window;
        return 1;
    }
    auto* swapchain = *result_swapchain;
    auto result_command_ring = device->CreateCommandRing(RM::CommandType::Graphics);
    if (!result_command_ring)
    {
        std::cerr << result_command_ring.error() << '\n';
        device->DestroyImmediate(swapchain);
        device->DestroyImmediate(queue);
        RM::DestroyDevice(device);
        delete window;
        return 1;
    }
    auto* command_ring = *result_command_ring;

    auto result_graph = device->CreateRenderGraph();
    if (!result_graph)
    {
        std::cerr << result_graph.error() << '\n';
        device->DestroyImmediate(command_ring);
        device->DestroyImmediate(swapchain);
        device->DestroyImmediate(queue);
        RM::DestroyDevice(device);
        delete window;
        return 1;
    }
    auto* graph = *result_graph;

    auto result_model = Model::Create(device, "assets/chess.glb");
    if (!result_model)
    {
        std::cerr << result_model.error() << '\n';
        device->DestroyImmediate(graph);
        device->DestroyImmediate(command_ring);
        device->DestroyImmediate(swapchain);
        device->DestroyImmediate(queue);
        RM::DestroyDevice(device);
        delete window;
        return 1;
    }
    auto* model = *result_model;

    MeshShaderScene scene(device, model, size);
    if (auto result = scene.Setup(); !result)
    {
        std::cerr << result.error() << '\n';
        scene.Cleanup();
        device->DestroyImmediate(graph);
        device->DestroyImmediate(command_ring);
        device->DestroyImmediate(swapchain);
        device->DestroyImmediate(queue);
        RM::DestroyDevice(device);
        delete window;
        return 1;
    }

    int exit_code = 0;
    constexpr RM::ResourceState present{.layout = RM::BarrierLayout::Present};

    Camera camera;
    camera.LookAt({0.f, 0.f, 5.f}, {0.f, 0.f, 0.f});
    auto previous_time = std::chrono::steady_clock::now();

    ImGuiLayer* imgui;
    auto result_imgui = ImGuiLayer::Create(device, queue, window, MeshShaderScene::kColorFormat);
    if (!result_imgui)
    {
        std::cerr << result_imgui.error() << '\n';
        exit_code = 1;
        return exit_code;
    }
    imgui = result_imgui.value();

    bool bDebugVisibility = true;
    glm::uvec2 viewport_size = size;
    bool viewport_input = false;
    while (window->IsRunning())
    {
        window->Tick();
        if (!window->IsRunning()) break;

        const auto time = std::chrono::steady_clock::now();
        const float delta_time = std::chrono::duration<float>(time - previous_time).count();
        previous_time = time;

        if (window->IsResized())
        {
            size = window->GetFramebufferSize();
            if (size.x && size.y)
            {
                if (auto result = swapchain->Resize(size.x, size.y); !result)
                {
                    std::cerr << result.error() << '\n';
                    exit_code = 1;
                    break;
                }
            }
        }
        if (!size.x || !size.y) continue;

        imgui->NewFrame(delta_time);
        imgui->ShowStatistics(*graph);
        const bool camera_input =
            (viewport_input && !imgui->WantsTextInput()) ||
            (!imgui->WantsMouse() && !imgui->WantsKeyboard());
        if (camera_input) camera.Update(*window, delta_time);
        camera.SetAspect(static_cast<float>(viewport_size.x) / static_cast<float>(viewport_size.y));
        const glm::uvec2 render_size = viewport_size;

        if (camera_input && window->IsKeyPressed(Key::Tab))
            camera.SetMode(camera.GetMode() == CameraMode::Orbit
                               ? CameraMode::Fly
                               : CameraMode::Orbit);

        const glm::mat4 view_projection = camera.GetViewProjection();
        const RM::DisplayInfo display_info = swapchain->GetDisplayInfo();
        imgui->SetDisplayInfo(display_info);

        if (auto result = device->NewFrame(queue); !result)
        {
            std::cerr << result.error() << '\n';
            exit_code = 1;
            break;
        }

        if (auto result = swapchain->Acquire(); !result)
        {
            std::cerr << result.error() << '\n';
            exit_code = 1;
            break;
        }

        auto* command = command_ring->GetCurrent();

        command->Begin();

        auto output = graph->ImportTexture(swapchain->GetCurrentBackBuffer(), present, present);
        auto visibility = graph->CreateTexture(
            RM::TextureBuilder(render_size.x, render_size.y).Format(
                MeshShaderScene::kVisibilityFormat).Build(),
            "visibility");
        auto debug = graph->CreateTexture(
            RM::TextureBuilder(render_size.x, render_size.y)
            .Format(MeshShaderScene::kColorFormat).Build(),
            "debug");
        auto pp = graph->CreateTexture(
            RM::TextureBuilder(render_size.x, render_size.y)
            .Format(MeshShaderScene::kColorFormat).Build(),
            "pp");
        auto depth = graph->CreateTexture(
            RM::TextureBuilder(render_size.x, render_size.y)
            .Format(MeshShaderScene::kDepthFormat).Build(),
            "depth");
        auto uv_texture = graph->CreateTexture(
            RM::TextureBuilder(render_size.x, render_size.y)
            .Format(MeshShaderScene::kUVFormat).Build(),
            "uv");
        auto uv_gradient_texture = graph->CreateTexture(
            RM::TextureBuilder(render_size.x, render_size.y)
            .Format(MeshShaderScene::kDerivFormat).Build(),
            "uv gradients");
        auto tangent_frame_texture = graph->CreateTexture(
            RM::TextureBuilder(render_size.x, render_size.y).Format(
                MeshShaderScene::kTangentFrameFormat).Build(),
            "tangent frame");

        auto albedo_texture = graph->CreateTexture(
            RM::TextureBuilder(render_size.x, render_size.y).Format(
                MeshShaderScene::kAlbedoFormat).Build(),
            "albedo");

        graph->AddRenderPass("Scene")
             .Color(visibility,
                    RM::LoadOp::Clear,
                    RM::StoreOp::Store,
                    {.r = 0, .g = 0, .b = 0, .a = 0})
             .Depth(depth, RM::LoadOp::Clear, RM::StoreOp::DontCare)
             .Pipeline(scene.vis_pipeline)
             .ConstantBuffer(0, scene.UpdateScene(view_projection))
             .DispatchMesh({.x = scene.meshlet_count, .y = 1, .z = 1});

        graph->AddComputePass("Interp")
             .Read(visibility)
             .Write(uv_texture)
             .Write(uv_gradient_texture)
             .Write(tangent_frame_texture)
             .Pipeline(scene.interp_pipeline)
             .ConstantBuffer(0, scene.UpdateScene(view_projection))
             .RootConstants(render_size, 0)
             .ResourceIndex(8, visibility)
             .ResourceIndex(12, uv_texture)
             .ResourceIndex(16, uv_gradient_texture)
             .ResourceIndex(20, tangent_frame_texture)
             .Dispatch({.x = (render_size.x + 7) / 8, .y = (render_size.y + 7) / 8, .z = 1});

        graph->AddComputePass("Shading")
             .Read(visibility)
             .Read(uv_texture)
             .Read(uv_gradient_texture)
             .Read(tangent_frame_texture)
             .Write(albedo_texture)
             .Pipeline(scene.shading_pipeline)
             .ConstantBuffer(0, scene.UpdateScene(view_projection))
             .RootConstants(render_size, 0)
             .ResourceIndex(8, uv_texture)
             .ResourceIndex(12, uv_gradient_texture)
             .ResourceIndex(16, tangent_frame_texture)
             .ResourceIndex(20, visibility)
             .ResourceIndex(24, albedo_texture)
             .Dispatch({.x = (render_size.x + 7) / 8, .y = (render_size.y + 7) / 8, .z = 1});

        graph->AddComputePass("Visualize")
             .Read(visibility)
             .Write(debug)
             .Pipeline(scene.debug_pipeline)
             .ResourceIndex(0, visibility)
             .ResourceIndex(4, debug)
             .RootConstants(render_size, 8)
             .Dispatch({.x = (render_size.x + 7) / 8, .y = (render_size.y + 7) / 8, .z = 1});

        graph->AddComputePass("Tonemap")
             .Read(bDebugVisibility ? debug : albedo_texture)
             .Write(pp)
             .Pipeline(scene.tonemap_pipeline)
             .ResourceIndex(0, bDebugVisibility ? debug : albedo_texture)
             .ResourceIndex(4, pp)
             .RootConstants(MeshShaderScene::TonemapData{
                                .size = render_size,
                                .headroom = display_info.headroom,
                                .sdr_white_scale = display_info.sdr_white_scale,
                                .hdr = display_info.hdr ? 1u : 0u,
                            },
                            8)
             .Dispatch({.x = (render_size.x + 7) / 8, .y = (render_size.y + 7) / 8, .z = 1});

        viewport_input = false;
        if (ImGui::Begin("Viewport"))
        {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const ImVec2 pixel_scale = ImGui::GetIO().DisplayFramebufferScale;
            const glm::uvec2 requested(
                static_cast<uint32_t>(std::max(available.x, 0.f) * pixel_scale.x),
                static_cast<uint32_t>(std::max(available.y, 0.f) * pixel_scale.y));
            if (requested.x && requested.y)
            {
                viewport_size = requested;
                const ImVec2 origin = ImGui::GetCursorPos();
                ImGui::Image(imgui->GetTextureID(pp), available);
                ImGui::SetCursorPos(origin);
                ImGui::InvisibleButton("##viewport_input",
                                       available,
                                       ImGuiButtonFlags_MouseButtonLeft |
                                       ImGuiButtonFlags_MouseButtonRight |
                                       ImGuiButtonFlags_MouseButtonMiddle);
                viewport_input = ImGui::IsItemHovered() || ImGui::IsItemActive();
            }
        }
        ImGui::End();

        if (ImGui::Begin("Visibility Buffer"))
        {
            ImGui::Checkbox("Debug Visibility", &bDebugVisibility);
        }
        ImGui::End();

        imgui->AddPass(graph, output);

        //graph->AddCopyPass("Backbuffer Blit").Blit(pp, output);

        if (auto result = graph->Execute(command); !result)
        {
            std::cerr << result.error() << '\n';
            exit_code = 1;
            command->End();
            break;
        }

        command->End();

        queue->Submit(command);

        if (auto result = swapchain->Present(); !result)
        {
            std::cerr << result.error() << '\n';
            exit_code = 1;
            break;
        }
        imgui->Present();

        command_ring->Next();
    }
    if (auto result = queue->WaitIdle(); !result)
    {
        std::cerr << result.error() << '\n';
        exit_code = 1;
    }
    device->DestroyImmediate(graph);
    ImGuiLayer::Destroy(imgui);
    scene.Cleanup();
    device->DestroyImmediate(command_ring);
    device->DestroyImmediate(swapchain);
    device->DestroyImmediate(queue);
    RM::DestroyDevice(device);
    delete window;
    return exit_code;
}