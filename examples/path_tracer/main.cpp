#include "rimera_device.hpp"
#include "window.hpp"
#include "model.hpp"
#include "rimera_builders.hpp"

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

    auto result_model = Model::Create(device, "assets/damaged_helmet.glb");
    if (!result_model)
    {
        std::cerr << result_model.error() << '\n';
        device->DestroyTexture(texture);
        device->DestroyCommandRing(command_ring);
        RM::DestroyDevice(device);
        delete window;
        return 1;
    }
    auto* model = *result_model;

    int exit_code = 0;
    auto texture_layout = RM::BarrierLayout::Common;

    while (window->IsRunning())
    {
        window->Tick();

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

        auto rpci = RM::RenderPassBuilder({size.x, size.y})
                    .Color(texture,
                           RM::LoadOp::Clear,
                           RM::StoreOp::Store,
                           {.r = 0.392157, .g = 0.584314, .b = 0.929412, .a = 1.0})
                    .Build();

        command->BeginRenderPass(rpci);
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
#ifdef RIMERA_DEBUG
        if (auto result = command->Blit(texture, backbuffer); !result)
        {
            std::cerr << result.error() << '\n';
            exit_code = 1;
            break;
        }
#else
        static_cast<void>(command->Blit(texture, backbuffer));
#endif

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
        command_ring->Next();
    }
    if (auto result = device->GetGraphicsQueue()->WaitIdle(); !result)
    {
        std::cerr << result.error() << '\n';
        exit_code = 1;
    }
    delete model;
    device->DestroyTexture(texture);
    device->DestroyCommandRing(command_ring);
    RM::DestroyDevice(device);
    delete window;
    return exit_code;
}
