#include "rimera_device.hpp"
#include "window.hpp"
#include "model.hpp"
#include "rimera_builders.hpp"

int main()
{
    Window window;
    auto size = window.GetSize();
    RM::DeviceCreateInfo create_info{
        .window = window.GetNativeHandle(),
        .width = size.x,
        .height = size.y,
    };
    auto* device = new RM::Device(create_info);
    auto* command_ring = device->CreateCommandRing(RM::CommandType::Graphics);
    auto* texture = device->CreateTexture({
        .width = size.x,
        .height = size.y,
        .mip_levels = 1,
        .array_layers = 1,
        .format = RM::Format::RGBA8_UNorm,
        .usage = {RM::TextureUsage::RenderTarget, RM::TextureUsage::ShaderRead},
    });
    auto* rt_tex_view = device->CreateTextureView({
        .texture = texture,
        .view_type = RM::TextureViewType::RenderTarget,
    });
    device->GetCoreResidencySet()->AddAllocation(texture);
    auto* srv = device->CreateTextureView({
        .texture = texture,
        .view_type = RM::TextureViewType::ShaderRead,
    });

    auto* model = new Model(device, "assets/damaged_helmet.glb");

    auto texture_layout = RM::BarrierLayout::Common;

    while (window.IsRunning())
    {
        window.Tick();

        auto* queue = device->GetGraphicsQueue();
        device->NewFrame(queue);

        auto* command = command_ring->GetCurrent();
        auto* backbuffer = device->GetCurrentBackBuffer();

        command->Begin();

        auto rt_barrier = RM::TextureBarrierBuilder(
                              texture,
                              texture_layout,
                              RM::BarrierLayout::RenderTarget)
                          .Before(RM::BarrierStage::None, RM::BarrierAccess::NoAccess)
                          .After(RM::BarrierStage::RenderTarget, RM::BarrierAccess::RenderTarget)
                          .Build();

        command->Barrier(rt_barrier);

        auto rpci = RM::RenderPassBuilder({size.x, size.y})
                    .Color(
                        rt_tex_view,
                        RM::LoadOp::Clear,
                        RM::StoreOp::Store,
                        {.r = 0.392157, .g = 0.584314, .b = 0.929412, .a = 1.0})
                    .Build();

        command->BeginRenderPass(rpci);
        command->EndRenderPass();

        auto copy_src = RM::TextureBarrierBuilder(
                            texture,
                            RM::BarrierLayout::RenderTarget,
                            RM::BarrierLayout::ShaderResource)
                        .Before(RM::BarrierStage::RenderTarget, RM::BarrierAccess::RenderTarget)
                        .After(RM::BarrierStage::PixelShading, RM::BarrierAccess::ShaderResource)
                        .Build();

        auto copy_dst = RM::TextureBarrierBuilder(
                            backbuffer->GetTexture(),
                            RM::BarrierLayout::Present,
                            RM::BarrierLayout::RenderTarget)
                        .Before(RM::BarrierStage::None, RM::BarrierAccess::NoAccess)
                        .After(RM::BarrierStage::RenderTarget, RM::BarrierAccess::RenderTarget)
                        .Build();

        command->Barrier(std::array{copy_src, copy_dst});
        command->Blit(srv, backbuffer, RM::BlitRegion());

        auto present_barrier = RM::TextureBarrierBuilder(
                                   backbuffer->GetTexture(),
                                   RM::BarrierLayout::RenderTarget,
                                   RM::BarrierLayout::Present)
                               .Before(RM::BarrierStage::RenderTarget,
                                       RM::BarrierAccess::RenderTarget)
                               .After(RM::BarrierStage::None, RM::BarrierAccess::NoAccess)
                               .Build();

        command->Barrier(present_barrier);

        command->End();

        queue->Submit(command);
        queue->Present();

        texture_layout = RM::BarrierLayout::ShaderResource;
        command_ring->Next();
    }
    device->GetGraphicsQueue()->WaitIdle();
    delete model;
    device->DestroyTextureView(rt_tex_view);
    device->DestroyTexture(texture);
    device->DestroyCommandRing(command_ring);
    RM::DestroyDevice(device);

    return 0;
}