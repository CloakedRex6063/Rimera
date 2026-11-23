#include "camera.hpp"
#include "fileio.hpp"
#include "imgui.h"
#include "imgui_layer.hpp"
#include "rimera_buffer.hpp"
#include "rimera_builders.hpp"
#include "rimera_compiler.hpp"
#include "rimera_device.hpp"
#include "rimera_render_graph.hpp"
#include "rimera_structs.hpp"
#include "window.hpp"
#include "algorithm"
#include "array"
#include "chrono"
#include "cmath"
#include "cstring"
#include "expected"
#include "string_view"
#include "vector"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

constexpr uint32_t kGroupSize = 128;
constexpr uint32_t kMaxGroupsPerDimension = 65535;
constexpr uint32_t kMaxGrassPatches = kMaxGroupsPerDimension * kGroupSize;

constexpr uint32_t kDefaultGrassPatches = 1500;
static_assert(uint64_t{kDefaultGrassPatches} * kDefaultGrassPatches <= kMaxGrassPatches);

struct GrassSettings
{
    uint32_t patch_x = kDefaultGrassPatches;
    uint32_t patch_y = kDefaultGrassPatches;
    float width = 0.279f;
    float height = 1.5f;
    float grid_spacing = 1.f;
    float wind_speed = 0.5f;
    float wind_strength = 1.f;
    float lod_start = 50.f;
    float lod_distance = 1000.f;
    float radius = 2.15f;
    float terrain_height = 60.f;
    float terrain_frequency = 0.022f;
    float terrain_exponent = 2.05f;
    bool apply_view_space_thicken = true;
    bool cull = true;
    bool cull_z = false;
    bool debug_patch_colors = false;
};

struct GrassConstants
{
    float wind_speed;
    float wind_strength;
    uint32_t apply_view_space_thicken;
    float lod_distance;
    uint32_t grass_count;
    float radius;
    uint32_t cull;
    uint32_t cull_z;
    float width;
    float time;
    float terrain_height;
    float terrain_frequency;
    float terrain_exponent;
    float lod_start;
    uint32_t debug_patch_colors;
};

static_assert(sizeof(GrassConstants) == 60);

struct GroundConstants
{
    glm::vec2 origin;
    glm::vec2 extent;
    float cell_size;
    float terrain_height;
    float terrain_frequency;
    float terrain_exponent;
    uint32_t tile_count_x;
    uint32_t tile_count;
    uint32_t cull;
    uint32_t cull_z;
};

static_assert(sizeof(GroundConstants) == 48);

struct TonemapData
{
    float headroom;
    float sdr_white_scale;
    uint32_t hdr;
    uint32_t apply_curve;
};

static_assert(sizeof(TonemapData) == 16);

struct GrassPatch
{
    glm::vec3 position;
    float height;
};

static_assert(sizeof(GrassPatch) == 16);

struct SceneData
{
    glm::mat4 view_projection{1};
    glm::vec3 camera_position{};
    uint32_t grass_buffer_index;
    glm::mat4 cull_view_projection{1};
};

static_assert(sizeof(SceneData) == 144);

constexpr auto kColorFormat = RM::Format::RGBA16_Float;
constexpr auto kDepthFormat = RM::Format::Depth32_Float;
constexpr uint32_t kFramesInFlight = 3;
constexpr uint32_t kGroundTileQuads = 8;
constexpr float kGroundCellScale = 2.f;

RM::Result<RM::Buffer*> CreateGrassBuffer(RM::Device* device, const GrassSettings& settings)
{
    const uint64_t requested = static_cast<uint64_t>(settings.patch_x) * settings.patch_y;
    const auto count = static_cast<uint32_t>(std::clamp<uint64_t>(requested, 1, kMaxGrassPatches));

    std::vector<GrassPatch> patches;
    patches.reserve(count);
    for (uint32_t i = 0; i < settings.patch_x && patches.size() < count; ++i)
    {
        for (uint32_t j = 0; j < settings.patch_y && patches.size() < count; ++j)
        {
            patches.push_back({
                .position = glm::vec3(static_cast<float>(i) * settings.grid_spacing,
                                      0.f,
                                      static_cast<float>(j) * settings.grid_spacing),
                .height = settings.height,
            });
        }
    }
    if (patches.empty()) patches.push_back({.position = glm::vec3(0.f), .height = settings.height});

    auto result = device->CreateBuffer({
        .size = static_cast<uint32_t>(patches.size() * sizeof(GrassPatch)),
        .data = patches.data(),
    });
    if (!result) return std::unexpected(result.error());
    device->GetCoreResidencySet()->AddAllocation(*result);
    device->GetCoreResidencySet()->Commit();
    return *result;
}

uint32_t GetGrassCount(const RM::Buffer* grass_buffer)
{
    return static_cast<uint32_t>(grass_buffer->GetLength() / sizeof(GrassPatch));
}

RM::Result<const RM::Shader*> CompileShader(RM::Compiler* compiler,
                                            std::vector<const RM::Shader*>& shaders,
                                            std::string_view file,
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

RM::Result<void> CompilePipelines(RM::Device* device,
                                  RM::Compiler* compiler,
                                  std::vector<const RM::Shader*>& shaders,
                                  RM::Pipeline*& grass_pipeline,
                                  RM::Pipeline*& ground_pipeline,
                                  RM::Pipeline*& tonemap_pipeline)
{
    auto amp_shader =
        CompileShader(compiler, shaders, "grass.hlsl", "AMMain", RM::ShaderType::Amplification);
    if (!amp_shader) return std::unexpected(amp_shader.error());
    auto mesh_shader = CompileShader(compiler, shaders, "grass.hlsl", "MSMain", RM::ShaderType::Mesh);
    if (!mesh_shader) return std::unexpected(mesh_shader.error());
    auto fragment_shader =
        CompileShader(compiler, shaders, "grass.hlsl", "PSMain", RM::ShaderType::Fragment);
    if (!fragment_shader) return std::unexpected(fragment_shader.error());
    auto ground_amp_shader =
        CompileShader(compiler, shaders, "ground.hlsl", "AMMain", RM::ShaderType::Amplification);
    if (!ground_amp_shader) return std::unexpected(ground_amp_shader.error());
    auto ground_mesh_shader =
        CompileShader(compiler, shaders, "ground.hlsl", "MSMain", RM::ShaderType::Mesh);
    if (!ground_mesh_shader) return std::unexpected(ground_mesh_shader.error());
    auto ground_fragment_shader =
        CompileShader(compiler, shaders, "ground.hlsl", "PSMain", RM::ShaderType::Fragment);
    if (!ground_fragment_shader) return std::unexpected(ground_fragment_shader.error());
    auto tonemap_shader =
        CompileShader(compiler, shaders, "tonemap.hlsl", "CSMain", RM::ShaderType::Compute);
    if (!tonemap_shader) return std::unexpected(tonemap_shader.error());

    auto grass_result = device->CreatePipeline(RM::MeshGraphicsPipelineCreateInfo{
        .ampl_shader = *amp_shader,
        .mesh_shader = *mesh_shader,
        .fragment_shader = *fragment_shader,
        .rtv_formats = {kColorFormat},
        .dsv_format = kDepthFormat,
        .depth_stencil_state =
            {
                .depth_enable = true,
                .depth_write_enable = true,
                .depth_test = RM::ComparisonFunc::Greater,
            },
        .rasterizer_state = {.cull_mode = RM::CullMode::None},
        .polygon_mode = RM::PolygonMode::Triangle,
    });
    if (!grass_result) return std::unexpected(grass_result.error());
    grass_pipeline = *grass_result;

    auto ground_result = device->CreatePipeline(RM::MeshGraphicsPipelineCreateInfo{
        .ampl_shader = *ground_amp_shader,
        .mesh_shader = *ground_mesh_shader,
        .fragment_shader = *ground_fragment_shader,
        .rtv_formats = {kColorFormat},
        .dsv_format = kDepthFormat,
        .depth_stencil_state =
            {
                .depth_enable = true,
                .depth_write_enable = true,
                .depth_test = RM::ComparisonFunc::Greater,
            },
        .rasterizer_state = {.cull_mode = RM::CullMode::None},
        .polygon_mode = RM::PolygonMode::Triangle,
    });
    if (!ground_result) return std::unexpected(ground_result.error());
    ground_pipeline = *ground_result;

    auto tonemap_result = device->CreatePipeline(RM::ComputePipelineCreateInfo{
        .shader = *tonemap_shader,
    });
    if (!tonemap_result) return std::unexpected(tonemap_result.error());
    tonemap_pipeline = *tonemap_result;
    return {};
}

RM::Result<void> CreatePipelines(RM::Device* device,
                                 RM::Pipeline*& grass_pipeline,
                                 RM::Pipeline*& ground_pipeline,
                                 RM::Pipeline*& tonemap_pipeline)
{
    const std::string shaders_dir = FileIO::ResourcePath() + "shaders";
    const std::string_view include_paths[] = {shaders_dir};
    auto compiler_result = RM::CreateCompiler({.include_paths = include_paths});
    if (!compiler_result) return std::unexpected(compiler_result.error());
    std::vector<const RM::Shader*> shaders;
    auto result =
        CompilePipelines(device, *compiler_result, shaders, grass_pipeline, ground_pipeline, tonemap_pipeline);
    for (const auto* shader : shaders) RM::DestroyShader(shader);
    RM::DestroyCompiler(*compiler_result);
    return result;
}

int main()
{
    auto* window = RM::Unwrap(Window::Create());
    auto size = window->GetFramebufferSize();
    auto* device = RM::Unwrap(RM::CreateDevice({}));
    auto* queue = RM::Unwrap(device->CreateQueue(RM::QueueType::Graphics));
    const NativeWindow native_window = window->GetNativeWindow();
    auto* swapchain = RM::Unwrap(device->CreateSwapchain(queue,
                                                         {
                                                             .window = native_window.window,
                                                             .width = size.x,
                                                             .height = size.y,
                                                             .display = native_window.display,
                                                         }));
    auto* command_ring = RM::Unwrap(device->CreateCommandRing(RM::CommandType::Graphics));

    auto* graph = RM::Unwrap(device->CreateRenderGraph());

    GrassSettings grass_settings{};
    auto* grass_buffer = RM::Unwrap(CreateGrassBuffer(device, grass_settings));

    std::array<RM::Buffer*, kFramesInFlight> scene_buffers{};
    std::array<SceneData*, kFramesInFlight> scene_data{};
    for (uint32_t i = 0; i < kFramesInFlight; ++i)
    {
        const SceneData initial{};
        scene_buffers[i] = RM::Unwrap(device->CreateBuffer({.size = sizeof(SceneData),
                                                            .data = &initial,
                                                            .memory_type = RM::MemoryType::HostVisible}));
        scene_data[i] = static_cast<SceneData*>(RM::Unwrap(scene_buffers[i]->Map()));
        device->GetCoreResidencySet()->AddAllocation(scene_buffers[i]);
    }
    device->GetCoreResidencySet()->Commit();
    uint32_t scene_frame = 0;

    RM::Pipeline* grass_pipeline = nullptr;
    RM::Pipeline* ground_pipeline = nullptr;
    RM::Pipeline* tonemap_pipeline = nullptr;
    RM::Unwrap(CreatePipelines(device, grass_pipeline, ground_pipeline, tonemap_pipeline));

    constexpr RM::ResourceState present{.layout = RM::BarrierLayout::Present};

    Camera camera;
    camera.SetReversedZ(true);
    camera.LookAt({750.f, 90.f, 750.f}, {1000.f, 40.f, 750.f});
    auto previous_time = std::chrono::steady_clock::now();

    auto* imgui = RM::Unwrap(ImGuiLayer::Create(device, queue, window, kColorFormat));

    constexpr float near_plane = 0.01f;
    constexpr float far_plane = 10000.f;
    float cull_near_plane = 0.01f;
    float cull_far_plane = 2000.f;
    float time = 0.f;
    bool rebuild_grass = false;
    glm::uvec2 viewport_size = size;
    bool viewport_input = false;
    while (window->IsRunning())
    {
        window->Tick();

        const auto now = std::chrono::steady_clock::now();
        const float delta_time = std::chrono::duration<float>(now - previous_time).count();
        previous_time = now;
        time += delta_time;

        if (window->IsResized())
        {
            size = window->GetFramebufferSize();
            RM::Unwrap(swapchain->Resize(size.x, size.y));
        }
        if (!size.x || !size.y) continue;

        if (rebuild_grass)
        {
            RM::Unwrap(queue->WaitIdle());
            auto* rebuilt = RM::Unwrap(CreateGrassBuffer(device, grass_settings));
            device->DestroyImmediate(grass_buffer);
            grass_buffer = rebuilt;
            rebuild_grass = false;
        }
        const uint32_t grass_count = GetGrassCount(grass_buffer);

        imgui->NewFrame(delta_time);
        imgui->ShowStatistics(*graph);
        const bool camera_input = (viewport_input && !imgui->WantsTextInput()) ||
                                  (!imgui->WantsMouse() && !imgui->WantsKeyboard());
        if (camera_input) camera.Update(*window, delta_time);
        camera.SetAspect(static_cast<float>(viewport_size.x) / static_cast<float>(viewport_size.y));
        camera.SetPerspective(camera.GetFovY(), near_plane, far_plane);
        const glm::uvec2 render_size = viewport_size;

        const RM::DisplayInfo display_info = swapchain->GetDisplayInfo();
        imgui->SetDisplayInfo(display_info);

        RM::Unwrap(device->NewFrame());

        RM::Unwrap(swapchain->Acquire());

        scene_frame = (scene_frame + 1) % kFramesInFlight;
        const SceneData frame_data{
            .view_projection = camera.GetViewProjection(),
            .camera_position = camera.GetPosition(),
            .grass_buffer_index = grass_buffer->GetResourceIndex(),
            .cull_view_projection = glm::perspectiveRH_ZO(camera.GetFovY(),
                                                          camera.GetAspect(),
                                                          cull_near_plane,
                                                          cull_far_plane) *
                                    camera.GetView(),
        };
        std::memcpy(scene_data[scene_frame], &frame_data, sizeof(SceneData));
        auto* scene_buffer = scene_buffers[scene_frame];
        auto* command = command_ring->GetCurrent();

        command->Begin();

        auto output = graph->ImportTexture(swapchain->GetCurrentBackBuffer(), present, present);
        auto color = graph->CreateTexture(RM::TextureBuilder(render_size.x, render_size.y)
                                              .Format(kColorFormat)
                                              .Build(),
                                          "color");
        auto depth = graph->CreateTexture(RM::TextureBuilder(render_size.x, render_size.y)
                                              .Format(kDepthFormat)
                                              .Build(),
                                          "depth");
        auto pp = graph->CreateTexture(RM::TextureBuilder(render_size.x, render_size.y)
                                           .Format(kColorFormat)
                                           .Build(),
                                       "pp");

        const float grid_spacing = std::max(grass_settings.grid_spacing, 0.001f);
        const float terrain_frequency = grass_settings.terrain_frequency / grid_spacing;
        const float ground_cell_size = kGroundCellScale * grid_spacing;
        const glm::vec2 ground_extent =
            glm::vec2(static_cast<float>(grass_settings.patch_x - 1),
                      static_cast<float>(grass_settings.patch_y - 1)) *
                grass_settings.grid_spacing +
            2.f * grass_settings.radius;
        const glm::uvec2 ground_tiles =
            glm::uvec2(glm::ceil(ground_extent / (ground_cell_size * kGroundTileQuads)));
        const uint32_t ground_tile_count = ground_tiles.x * ground_tiles.y;

        graph->AddRenderPass("Ground")
            .Color(color,
                   RM::LoadOp::Clear,
                   RM::StoreOp::Store,
                   {.r = 0.392f, .g = 0.584f, .b = 0.929f, .a = 1.f})
            .Depth(depth, RM::LoadOp::Clear, RM::StoreOp::Store, 0.f)
            .Pipeline(ground_pipeline)
            .ConstantBuffer(0, scene_buffer)
            .RootConstants(GroundConstants{
                .origin = glm::vec2(-grass_settings.radius),
                .extent = ground_extent,
                .cell_size = ground_cell_size,
                .terrain_height = grass_settings.terrain_height,
                .terrain_frequency = terrain_frequency,
                .terrain_exponent = grass_settings.terrain_exponent,
                .tile_count_x = ground_tiles.x,
                .tile_count = ground_tile_count,
                .cull = grass_settings.cull ? 1u : 0u,
                .cull_z = grass_settings.cull_z ? 1u : 0u,
            })
            .DispatchMesh({.x = (ground_tile_count + kGroupSize - 1) / kGroupSize, .y = 1, .z = 1});

        graph->AddRenderPass("Grass")
            .Color(color, RM::LoadOp::Load, RM::StoreOp::Store)
            .Depth(depth, RM::LoadOp::Load, RM::StoreOp::Store)
            .Pipeline(grass_pipeline)
            .ConstantBuffer(0, scene_buffer)
            .RootConstants(GrassConstants{
                .wind_speed = grass_settings.wind_speed,
                .wind_strength = grass_settings.wind_strength,
                .apply_view_space_thicken = grass_settings.apply_view_space_thicken ? 1u : 0u,
                .lod_distance = grass_settings.lod_distance,
                .grass_count = grass_count,
                .radius = grass_settings.radius,
                .cull = grass_settings.cull ? 1u : 0u,
                .cull_z = grass_settings.cull_z ? 1u : 0u,
                .width = grass_settings.width,
                .time = time,
                .terrain_height = grass_settings.terrain_height,
                .terrain_frequency = terrain_frequency,
                .terrain_exponent = grass_settings.terrain_exponent,
                .lod_start = grass_settings.lod_start,
                .debug_patch_colors = grass_settings.debug_patch_colors ? 1u : 0u,
            })
            .DispatchMesh({.x = (grass_count + kGroupSize - 1) / kGroupSize, .y = 1, .z = 1});

        graph->AddComputePass("Tonemap")
            .Read(color)
            .Write(pp)
            .Pipeline(tonemap_pipeline)
            .ResourceIndex(0, color)
            .ResourceIndex(4, pp)
            .RootConstants(
                TonemapData{
                    .headroom = display_info.headroom,
                    .sdr_white_scale = display_info.sdr_white_scale,
                    .hdr = display_info.hdr ? 1u : 0u,
                    .apply_curve = 0u,
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

        if (ImGui::Begin("Grass Patches"))
        {
            constexpr uint32_t min_patches = 1;
            ImGui::DragScalar("Patch X", ImGuiDataType_U32, &grass_settings.patch_x, 1, &min_patches);
            rebuild_grass |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragScalar("Patch Y", ImGuiDataType_U32, &grass_settings.patch_y, 1, &min_patches);
            rebuild_grass |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Grid Spacing", &grass_settings.grid_spacing, 0.01f, 0.f, 1000.f);
            rebuild_grass |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Grass Height", &grass_settings.height, 0.01f, 0.f, 100.f);
            rebuild_grass |= ImGui::IsItemDeactivatedAfterEdit();

            ImGui::Checkbox("Viewspace Thicken", &grass_settings.apply_view_space_thicken);
            ImGui::Checkbox("Colour by Mesh Group", &grass_settings.debug_patch_colors);

            ImGui::DragFloat("Wind Speed", &grass_settings.wind_speed, 0.01f);
            ImGui::DragFloat("Wind Strength", &grass_settings.wind_strength, 0.01f);
            ImGui::DragFloat("Grass Lod Start", &grass_settings.lod_start, 1.f, 0.f, grass_settings.lod_distance - 0.01f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Grass Lod Distance", &grass_settings.lod_distance, 1.f, grass_settings.lod_start + 0.01f, 100000.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Grass Patch Radius", &grass_settings.radius, 0.01f);
            ImGui::DragFloat("Grass Width", &grass_settings.width, 0.001f);

            ImGui::DragFloat("Terrain Height", &grass_settings.terrain_height, 0.05f, 0.f, 100.f);
            ImGui::DragFloat("Terrain Frequency", &grass_settings.terrain_frequency, 0.001f, 0.f, 10.f);
            ImGui::DragFloat("Terrain Exponent", &grass_settings.terrain_exponent, 0.01f, 0.01f, 10.f);

            ImGui::Checkbox("Apply Culling", &grass_settings.cull);
            ImGui::BeginDisabled(!grass_settings.cull);
            ImGui::Checkbox("Apply Culling Z", &grass_settings.cull_z);
            if (grass_settings.cull_z)
            {
                ImGui::DragFloat("Cull Near Plane", &cull_near_plane, 0.001f, 0.001f, cull_far_plane - 0.001f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::DragFloat("Cull Far Plane", &cull_far_plane, 1.f, cull_near_plane + 0.001f, 100000.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            }
            ImGui::EndDisabled();
            ImGui::Text("Patches: %u", grass_count);
        }
        ImGui::End();

        imgui->AddPass(graph, output);

        RM::Unwrap(graph->Execute(command));

        command->End();

        std::vector swapchains{swapchain};
        imgui->GetAcquiredSwapchains(swapchains);
        RM::Unwrap(queue->Submit(RM::SubmitBuilder()
                                     .Execute(command)
                                     .Signal(device->GetFrameSignal())
                                     .Present(swapchains)
                                     .Build()));

        RM::Unwrap(swapchain->Present());
        imgui->Present();

        command_ring->Next();
    }
    RM::Unwrap(queue->WaitIdle());
    device->DestroyImmediate(graph);
    ImGuiLayer::Destroy(imgui);
    device->DestroyImmediate(grass_pipeline);
    device->DestroyImmediate(ground_pipeline);
    device->DestroyImmediate(tonemap_pipeline);
    for (auto* buffer : scene_buffers) device->DestroyImmediate(buffer);
    device->DestroyImmediate(grass_buffer);
    device->DestroyImmediate(command_ring);
    device->DestroyImmediate(swapchain);
    device->DestroyImmediate(queue);
    RM::DestroyDevice(device);
    delete window;
    return 0;
}
