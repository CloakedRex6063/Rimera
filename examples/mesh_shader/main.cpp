#include "imgui.h"
#include "mesh_shader_scene.hpp"
#include "rimera_device.hpp"
#include "window.hpp"
#include "camera.hpp"
#include "rimera_builders.hpp"
#include "rimera_render_graph.hpp"
#include "chrono"
#include "imgui_layer.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/euler_angles.hpp"

enum class ViewType : uint32_t
{
    VisibilityBuffer,
    Albedo,
    Metallic,
    Roughness,
    Occlusion,
    Emissive,
    Normal,
    Lit,
};

// TODO: Just use magic enum later
constexpr const char* kViewTypesNames[] = {
    "Visibility Buffer",
    "Albedo",
    "Metallic",
    "Roughness",
    "Occlusion",
    "Emissive",
    "Normal",
    "Lit",
};

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

    auto* scene = RM::Unwrap(MeshShaderScene::Create(device, "assets/damaged_helmet.glb"));

    constexpr RM::ResourceState present{.layout = RM::BarrierLayout::Present};

    Camera camera;
    camera.LookAt({0.f, 0.f, 5.f}, {0.f, 0.f, 0.f});
    auto previous_time = std::chrono::steady_clock::now();

    auto* imgui = RM::Unwrap(ImGuiLayer::Create(device, queue, window, MeshShaderScene::kColorFormat));

    auto view_type = ViewType::Lit;
    MeshShaderScene::DirLight dir_light{.direction = {},
                                        .intensity = 3.f,
                                        .color = {1.f, 1.f, 1.f}};
    glm::vec3 light_rotation{-50.f, 30.f, 0.f};
    glm::uvec2 viewport_size = size;
    bool viewport_input = false;
    while (window->IsRunning())
    {
        window->Tick();

        const auto time = std::chrono::steady_clock::now();
        const float delta_time = std::chrono::duration<float>(time - previous_time).count();
        previous_time = time;

        if (window->IsResized())
        {
            size = window->GetFramebufferSize();
            RM::Unwrap(swapchain->Resize(size.x, size.y));
        }
        if (!size.x || !size.y) continue;

        imgui->NewFrame(delta_time);
        imgui->ShowStatistics(*graph);
        const bool camera_input = (viewport_input && !imgui->WantsTextInput()) ||
                                  (!imgui->WantsMouse() && !imgui->WantsKeyboard());
        if (camera_input) camera.Update(*window, delta_time);
        camera.SetAspect(static_cast<float>(viewport_size.x) / static_cast<float>(viewport_size.y));
        const glm::uvec2 render_size = viewport_size;

        const glm::mat4 view_projection = camera.GetViewProjection();
        const RM::DisplayInfo display_info = swapchain->GetDisplayInfo();
        imgui->SetDisplayInfo(display_info);

        RM::Unwrap(device->NewFrame());

        RM::Unwrap(swapchain->Acquire());

        auto* scene_buffer = scene->UpdateScene(view_projection, camera.GetPosition());
        auto* command = command_ring->GetCurrent();

        command->Begin();

        auto output = graph->ImportTexture(swapchain->GetCurrentBackBuffer(), present, present);
        auto visibility = graph->CreateTexture(RM::TextureBuilder(render_size.x, render_size.y)
                                                   .Format(MeshShaderScene::kVisibilityFormat)
                                                   .Build(),
                                               "visibility");
        auto debug = graph->CreateTexture(RM::TextureBuilder(render_size.x, render_size.y)
                                              .Format(MeshShaderScene::kColorFormat)
                                              .Build(),
                                          "debug");
        auto lit = graph->CreateTexture(RM::TextureBuilder(render_size.x, render_size.y)
                                            .Format(MeshShaderScene::kColorFormat)
                                            .Build(),
                                        "lit");
        auto pp = graph->CreateTexture(RM::TextureBuilder(render_size.x, render_size.y)
                                           .Format(MeshShaderScene::kColorFormat)
                                           .Build(),
                                       "pp");
        auto depth = graph->CreateTexture(RM::TextureBuilder(render_size.x, render_size.y)
                                              .Format(MeshShaderScene::kDepthFormat)
                                              .Build(),
                                          "depth");
        auto uv_texture = graph->CreateTexture(RM::TextureBuilder(render_size.x, render_size.y)
                                                   .Format(MeshShaderScene::kUVFormat)
                                                   .Build(),
                                               "uv");
        auto uv_gradient_texture =
            graph->CreateTexture(RM::TextureBuilder(render_size.x, render_size.y)
                                     .Format(MeshShaderScene::kDerivFormat)
                                     .Build(),
                                 "uv gradients");
        auto tangent_frame_texture =
            graph->CreateTexture(RM::TextureBuilder(render_size.x, render_size.y)
                                     .Format(MeshShaderScene::kTangentFrameFormat)
                                     .Build(),
                                 "tangent frame");

        auto albedo_texture = graph->CreateTexture(RM::TextureBuilder(render_size.x, render_size.y)
                                                       .Format(MeshShaderScene::kAlbedoFormat)
                                                       .Build(),
                                                   "albedo");
        auto normal_texture = graph->CreateTexture(RM::TextureBuilder(render_size.x, render_size.y)
                                                       .Format(MeshShaderScene::kNormalFormat)
                                                       .Build(),
                                                   "normal");
        auto rough_occlusion_texture =
            graph->CreateTexture(RM::TextureBuilder(render_size.x, render_size.y)
                                     .Format(MeshShaderScene::kRoughOcclusionFormat)
                                     .Build(),
                                 "rough occlusion");
        auto emissive_texture =
            graph->CreateTexture(RM::TextureBuilder(render_size.x, render_size.y)
                                     .Format(MeshShaderScene::kEmissiveFormat)
                                     .Build(),
                                 "emissive");

        graph->AddRenderPass("Scene")
            .Color(visibility,
                   RM::LoadOp::Clear,
                   RM::StoreOp::Store,
                   {.r = 0, .g = 0, .b = 0, .a = 0})
            .Depth(depth, RM::LoadOp::Clear, RM::StoreOp::Store)
            .Pipeline(scene->GetVisPipeline())
            .ConstantBuffer(0, scene_buffer)
            .DispatchMesh(scene->GetMeshDispatch());

        graph->AddComputePass("Interp")
            .Read(visibility)
            .Write(uv_texture)
            .Write(uv_gradient_texture)
            .Write(tangent_frame_texture)
            .Pipeline(scene->GetInterpPipeline())
            .ConstantBuffer(0, scene_buffer)
            .ResourceIndex(0, visibility)
            .ResourceIndex(4, uv_texture)
            .ResourceIndex(8, uv_gradient_texture)
            .ResourceIndex(12, tangent_frame_texture)
            .Dispatch({.x = (render_size.x + 7) / 8, .y = (render_size.y + 7) / 8, .z = 1});

        graph->AddComputePass("Shading")
            .Read(visibility)
            .Read(uv_texture)
            .Read(uv_gradient_texture)
            .Read(tangent_frame_texture)
            .Write(albedo_texture)
            .Write(normal_texture)
            .Write(rough_occlusion_texture)
            .Write(emissive_texture)
            .Pipeline(scene->GetShadingPipeline())
            .ConstantBuffer(0, scene_buffer)
            .ResourceIndex(0, uv_texture)
            .ResourceIndex(4, uv_gradient_texture)
            .ResourceIndex(8, tangent_frame_texture)
            .ResourceIndex(12, visibility)
            .ResourceIndex(16, albedo_texture)
            .ResourceIndex(20, normal_texture)
            .ResourceIndex(24, rough_occlusion_texture)
            .ResourceIndex(28, emissive_texture)
            .Dispatch({.x = (render_size.x + 7) / 8, .y = (render_size.y + 7) / 8, .z = 1});

        const glm::vec3 light_radians = glm::radians(light_rotation);
        dir_light.direction =
            glm::vec3(glm::yawPitchRoll(light_radians.y, light_radians.x, light_radians.z)[2]);
        graph->AddComputePass("Lighting")
            .Read(visibility)
            .Read(albedo_texture)
            .Read(normal_texture)
            .Read(rough_occlusion_texture)
            .Read(emissive_texture)
            .Read(depth)
            .Write(lit)
            .Pipeline(scene->GetLightingPipeline())
            .ConstantBuffer(0, scene_buffer)
            .ResourceIndex(0, visibility)
            .ResourceIndex(4, albedo_texture)
            .ResourceIndex(8, normal_texture)
            .ResourceIndex(12, rough_occlusion_texture)
            .ResourceIndex(16, emissive_texture)
            .ResourceIndex(20, lit)
            .ResourceIndex(24, depth)
            .RootConstants(dir_light, 32)
            .Dispatch({.x = (render_size.x + 7) / 8, .y = (render_size.y + 7) / 8, .z = 1});

        auto debug_input = visibility;
        switch (view_type)
        {
            case ViewType::VisibilityBuffer:
                debug_input = visibility;
                break;
            case ViewType::Albedo:
            case ViewType::Metallic:
                debug_input = albedo_texture;
                break;
            case ViewType::Roughness:
            case ViewType::Occlusion:
                debug_input = rough_occlusion_texture;
                break;
            case ViewType::Emissive:
                debug_input = emissive_texture;
                break;
            case ViewType::Normal:
                debug_input = normal_texture;
                break;
            case ViewType::Lit:
                break;
        }

        graph->AddComputePass("Visualize")
            .Read(visibility)
            .Read(debug_input)
            .Write(debug)
            .Pipeline(scene->GetDebugPipeline())
            .ResourceIndex(0, debug)
            .ResourceIndex(4, visibility)
            .ResourceIndex(8, debug_input)
            .RootConstants(static_cast<uint32_t>(view_type), 12)
            .Dispatch({.x = (render_size.x + 7) / 8, .y = (render_size.y + 7) / 8, .z = 1});

        const bool is_lit = view_type == ViewType::Lit;
        const auto tonemap_input = is_lit ? lit : debug;

        graph->AddComputePass("Tonemap")
            .Read(tonemap_input)
            .Write(pp)
            .Pipeline(scene->GetTonemapPipeline())
            .ResourceIndex(0, tonemap_input)
            .ResourceIndex(4, pp)
            .RootConstants(
                MeshShaderScene::TonemapData{
                    .headroom = display_info.headroom,
                    .sdr_white_scale = display_info.sdr_white_scale,
                    .hdr = display_info.hdr ? 1u : 0u,
                    .apply_curve = is_lit ? 1u : 0u,
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
            ImGui::Combo("View Type",
                         reinterpret_cast<int*>(&view_type),
                         kViewTypesNames,
                         std::size(kViewTypesNames));
        }
        ImGui::End();

        if (ImGui::Begin("Lighting"))
        {
            ImGui::SliderFloat3("Rotation", &light_rotation.x, -180.f, 180.f);
            ImGui::SliderFloat("Intensity", &dir_light.intensity, 0.f, 20.f);
            ImGui::ColorEdit3("Color", &dir_light.color.x);
        }
        ImGui::End();

        imgui->AddPass(graph, output);

        // graph->AddCopyPass("Backbuffer Blit").Blit(pp, output);

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
    delete scene;
    device->DestroyImmediate(command_ring);
    device->DestroyImmediate(swapchain);
    device->DestroyImmediate(queue);
    RM::DestroyDevice(device);
    delete window;
    return 0;
}
