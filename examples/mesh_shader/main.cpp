#include "rimera_device.hpp"
#include "window.hpp"
#include "model.hpp"
#include "fileio.hpp"
#include "rimera_builders.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

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
        uint32_t vertex_index;
        uint32_t vertex_indices_index;
        uint32_t triangle_index;
        uint32_t meshlet_buffer_index;
        uint32_t transform_index;
        uint32_t material_index;
        uint32_t padding{};
    };

    static_assert(sizeof(DrawData) == 32);

    struct SceneData
    {
        glm::mat4 view_projection;
        uint32_t draw_buffer_index;
        uint32_t transform_buffer_index;
        uint32_t material_buffer_index;
        uint32_t texture_buffer_index;
        uint32_t sampler_index;
        uint32_t padding[3]{};
    };

    static_assert(sizeof(SceneData) == 96);

    MeshShaderScene(RM::Device* device, RM::Texture* texture, Model* model, glm::uvec2 size)
        : device(device), texture(texture), model(model), size(size),
          residency(device->GetCoreResidencySet())
    {
    }

    RM::Device* device;
    RM::Texture* texture;
    Model* model;
    glm::uvec2 size;
    RM::ResidencySet* residency;
    uint32_t meshlet_count = 0;
    RM::Buffer* draw_buffer = nullptr;
    RM::Buffer* scene_buffer = nullptr;
    RM::Buffer* transform_buffer = nullptr;
    RM::Buffer* material_buffer = nullptr;
    RM::Buffer* texture_buffer = nullptr;
    RM::Texture* depth_texture = nullptr;
    RM::Pipeline* pipeline = nullptr;
    RM::Sampler* sampler = nullptr;

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
            residency->AddAllocation(mesh->GetVertexBuffer());
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
            transforms.push_back({node.world_transform,
                                  glm::transpose(glm::inverse(node.world_transform))});
        std::vector<DrawData> draw_data;
        auto visit_result = model->ForEachMeshlet(
            [&](const Mesh& mesh, uint32_t meshlet, uint32_t node_index) -> RM::Result<void>
            {
                draw_data.push_back({
                    .meshlet_index = meshlet,
                    .vertex_index = mesh.GetVertexBuffer()->GetResourceIndex(),
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

        constexpr auto center = glm::vec3();
        const float aspect = static_cast<float>(size.x) / size.y;
        constexpr float fov = glm::radians(45.f);
        const auto view = glm::lookAtRH(center + glm::vec3(0, 0, 5), center, glm::vec3(0, 1, 0));
        const auto projection = glm::perspectiveRH_ZO(fov, aspect, .01f, 1000.f);
        SceneData scene{.view_projection = projection * view,
                        .draw_buffer_index = draw_buffer->GetResourceIndex(),
                        .transform_buffer_index = transform_buffer->GetResourceIndex(),
                        .material_buffer_index = material_buffer->GetResourceIndex(),
                        .texture_buffer_index = texture_buffer->GetResourceIndex(),
                        .sampler_index = sampler->GetSamplerIndex()};
        auto scene_buffer_result = device->CreateBuffer({.size = sizeof(scene), .data = &scene});
        if (!scene_buffer_result) return std::unexpected(scene_buffer_result.error());
        scene_buffer = *scene_buffer_result;
        residency->AddAllocation(scene_buffer);

        auto depth_result = device->CreateTexture({
            .width = size.x,
            .height = size.y,
            .format = RM::Format::Depth32_Float,
            .usage = RM::TextureUsage::RenderTarget,
        });
        if (!depth_result) return std::unexpected(depth_result.error());
        depth_texture = *depth_result;
        residency->AddAllocation(depth_texture);

        std::filesystem::path shader_path = FileIO::ResourcePath() + "shaders/mesh_model.hlsl";
        auto model_shader = FileIO::Read(shader_path);
        if (!model_shader) return std::unexpected(model_shader.error());
        RM::MeshGraphicsPipelineCreateInfo pipeline_info{
            .mesh_shader = {.code = *model_shader, .entry_point = "MSMain"},
            .fragment_shader = {.code = *model_shader, .entry_point = "PSMain"},
            .rtv_formats = {texture->GetFormat()},
            .dsv_format = depth_texture->GetFormat(),
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
        pipeline = *pipeline_result;
        residency->Commit();
        return {};
    }

    RM::Result<void> DrawModel(RM::Command* command)
    {
        if (auto result = command->BindShader(pipeline); !result) return result;
        if (auto result = command->BindConstantBuffer(0, scene_buffer); !result) return result;
        command->SetViewport({.size = {size.x, size.y}, .depth_range = {0, 1}});
        command->SetScissor({.size = {size.x, size.y}});
        return command->DispatchMesh({.x = meshlet_count, .y = 1, .z = 1});
    }

    void Cleanup()
    {
        for (auto* mesh : model->GetMeshes())
        {
            residency->RemoveAllocation(mesh->GetVertexBuffer());
            residency->RemoveAllocation(mesh->GetVertexIndexBuffer());
            residency->RemoveAllocation(mesh->GetTriangleBuffer());
            residency->RemoveAllocation(mesh->GetMeshletBuffer());
        }
        for (auto* image : model->GetTextures())
            residency->RemoveAllocation(image->GetTexture());
        for (auto* buffer : {draw_buffer, scene_buffer, transform_buffer, material_buffer,
                             texture_buffer})
            if (buffer)
            {
                residency->RemoveAllocation(buffer);
                device->DestroyBuffer(buffer);
            }
        if (depth_texture)
        {
            residency->RemoveAllocation(depth_texture);
            device->DestroyTexture(depth_texture);
        }
        if (pipeline) device->DestroyPipeline(pipeline);
        if (sampler) device->DestroySampler(sampler);
        delete model;
        residency->RemoveAllocation(texture);
        device->DestroyTexture(texture);
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
    auto size = window->GetSize();
    RM::DeviceCreateInfo create_info{
        .window = window->GetNativeHandle(),
        .width = size.x,
        .height = size.y,
    };
    auto result_device = RM::CreateDevice(create_info);
    if (!result_device)
    {
        std::cerr << result_device.error() << '\n';
        delete window;
        return 1;
    }
    auto* device = *result_device;
    auto result_command_ring = device->CreateCommandRing(RM::CommandType::Graphics);
    if (!result_command_ring)
    {
        std::cerr << result_command_ring.error() << '\n';
        RM::DestroyDevice(device);
        delete window;
        return 1;
    }
    auto* command_ring = *result_command_ring;
    auto result_texture = device->CreateTexture({
        .width = size.x,
        .height = size.y,
        .mip_levels = 1,
        .array_layers = 1,
        .format = RM::Format::RGBA8_UNorm,
        .usage = {RM::TextureUsage::RenderTarget, RM::TextureUsage::ShaderRead},
    });
    if (!result_texture)
    {
        std::cerr << result_texture.error() << '\n';
        device->DestroyCommandRing(command_ring);
        RM::DestroyDevice(device);
        delete window;
        return 1;
    }
    auto* texture = *result_texture;
    device->GetCoreResidencySet()->AddAllocation(texture);

    auto result_model = Model::Create(device, "assets/chess.glb");
    if (!result_model)
    {
        std::cerr << result_model.error() << '\n';
        device->GetCoreResidencySet()->RemoveAllocation(texture);
        device->DestroyTexture(texture);
        device->DestroyCommandRing(command_ring);
        RM::DestroyDevice(device);
        delete window;
        return 1;
    }
    auto* model = *result_model;

    MeshShaderScene scene(device, texture, model, size);
    if (auto result = scene.Setup(); !result)
    {
        std::cerr << result.error() << '\n';
        scene.Cleanup();
        device->DestroyCommandRing(command_ring);
        RM::DestroyDevice(device);
        delete window;
        return 1;
    }

    int exit_code = 0;
    auto texture_layout = RM::BarrierLayout::Common;
    auto depth_layout = RM::BarrierLayout::Common;

    while (window->IsRunning())
    {
        window->Tick();
        if (!window->IsRunning()) break;

        auto* queue = device->GetGraphicsQueue();
        if (auto result = device->NewFrame(queue); !result)
        {
            std::cerr << result.error() << '\n';
            exit_code = 1;
            break;
        }

        auto* command = command_ring->GetCurrent();
        auto* backbuffer = device->GetCurrentBackBuffer();

        command->Begin();

        auto rt_barrier = RM::TextureBarrierBuilder(texture)
                          .BeforeLayout(texture_layout)
                          .AfterLayout(RM::BarrierLayout::RenderTarget)
                          .BeforeStage(RM::BarrierStage::None)
                          .BeforeAccess(RM::BarrierAccess::NoAccess)
                          .AfterStage(RM::BarrierStage::RenderTarget)
                          .AfterAccess(RM::BarrierAccess::RenderTarget)
                          .Build();

        command->Barrier(rt_barrier);

        auto ds_barrier = RM::TextureBarrierBuilder(scene.depth_texture)
                          .BeforeLayout(depth_layout)
                          .AfterLayout(RM::BarrierLayout::DepthStencilWrite)
                          .BeforeStage(RM::BarrierStage::None)
                          .BeforeAccess(RM::BarrierAccess::NoAccess)
                          .AfterStage(RM::BarrierStage::DepthStencil)
                          .AfterAccess(RM::BarrierAccess::DepthStencilWrite)
                          .Build();

        command->Barrier(ds_barrier);

        auto rpci = RM::RenderPassBuilder({size.x, size.y})
                    .Color(texture,
                           RM::LoadOp::Clear,
                           RM::StoreOp::Store,
                           {.r = 0.392157, .g = 0.584314, .b = 0.929412, .a = 1.0})
                    .Depth(scene.depth_texture, RM::LoadOp::Clear, RM::StoreOp::Store)
                    .Build();

        command->BeginRenderPass(rpci);
        if (auto result = scene.DrawModel(command); !result)
        {
            std::cerr << result.error() << '\n';
            exit_code = 1;
            command->EndRenderPass();
            command->End();
            break;
        }
        command->EndRenderPass();

        auto copy_src = RM::TextureBarrierBuilder(texture)
                        .BeforeLayout(RM::BarrierLayout::RenderTarget)
                        .AfterLayout(RM::BarrierLayout::BlitSource)
                        .BeforeStage(RM::BarrierStage::RenderTarget)
                        .AfterStage(RM::BarrierStage::BlitSource)
                        .BeforeAccess(RM::BarrierAccess::RenderTarget)
                        .AfterAccess(RM::BarrierAccess::ShaderResource)
                        .Build();

        auto copy_dst = RM::TextureBarrierBuilder(backbuffer)
                        .BeforeLayout(RM::BarrierLayout::Present)
                        .AfterLayout(RM::BarrierLayout::BlitDestination)
                        .BeforeStage(RM::BarrierStage::None)
                        .AfterStage(RM::BarrierStage::BlitDestination)
                        .BeforeAccess(RM::BarrierAccess::NoAccess)
                        .AfterAccess(RM::BarrierAccess::RenderTarget)
                        .Build();

        command->Barrier(std::array{copy_src, copy_dst});
        command->Blit(texture, backbuffer);

        auto present_barrier = RM::TextureBarrierBuilder(backbuffer)
                               .BeforeLayout(RM::BarrierLayout::BlitDestination)
                               .AfterLayout(RM::BarrierLayout::Present)
                               .BeforeStage(RM::BarrierStage::BlitDestination)
                               .BeforeAccess(RM::BarrierAccess::RenderTarget)
                               .AfterStage(RM::BarrierStage::None)
                               .AfterAccess(RM::BarrierAccess::NoAccess)
                               .Build();

        command->Barrier(present_barrier);

        command->End();

        queue->Submit(command);

        if (auto result = queue->Present(); !result)
        {
            std::cerr << result.error() << '\n';
            exit_code = 1;
            break;
        }

        texture_layout = RM::BarrierLayout::BlitSource;
        depth_layout = RM::BarrierLayout::DepthStencilWrite;
        command_ring->Next();
    }
    if (auto result = device->GetGraphicsQueue()->WaitIdle(); !result)
    {
        std::cerr << result.error() << '\n';
        exit_code = 1;
    }
    scene.Cleanup();
    device->DestroyCommandRing(command_ring);
    RM::DestroyDevice(device);
    delete window;
    return exit_code;
}