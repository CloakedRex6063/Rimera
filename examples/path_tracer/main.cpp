#include "rimera_device.hpp"
#include "window.hpp"
#include "model.hpp"

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
    auto* queue = device->CreateQueue(RM::QueueType::Graphics);
    auto* command_ring = device->CreateCommandRing(RM::CommandType::Graphics);
    auto* residency_set = device->CreateResidencySet();
    auto* texture = device->CreateTexture(RM::TextureCreateInfo{
        .width = size.x,
        .height = size.y,
        .mip_levels = 1,
        .array_layers = 1,
        .format = RM::Format::RGBA16_Float,
        .usage = {RM::TextureUsage::RenderTarget, RM::TextureUsage::ShaderRead},
    });
    residency_set->AddAllocation(texture);
    residency_set->Commit();
    residency_set->RequestResidency();
    queue->AddResidencySet(residency_set);

    auto model = Model(device, "assets/damaged_helmet.glb");
    
    // auto* shader = device->CreateShader(RM::ComputeShaderCreateInfo{
    //     .code = ,
    //     .entry_point = "main"
    // });

    while (window.IsRunning())
    {
        window.Tick();

        device->NewFrame(queue);

        const auto command = command_ring->GetCurrent();
        command->Begin();

        auto* presentable = device->GetCurrentPresentable();

        RM::RenderPassCreateInfo rpci{
            .size =
                RM::Size{
                    .width = size.x,
                    .height = size.y,
                },
            .color_attachments = {RM::ColorAttachment{
                .texture = texture,
                .clear_color = RM::ClearColor{0.3921568627f, 0.5843137255f, 0.9294117647f, 1.f},
                .store_op = RM::StoreOp::Store,
                .load_op = RM::LoadOp::Clear,
            }},
            .depth_attachment = std::nullopt,
        };
        command->BeginRenderPass(rpci);
        command->EndRenderPass();

        command->BeginComputePass();
        // command->BindShader();
        // command->Dispatch({.x = size.x / 8, .y = size.y / 8, .z = 1});
        command->EndComputePass();

        command->BlitToSwapchain(device, texture);

        command->End();

        queue->Submit(command);

        queue->Present(presentable);

        command_ring->Next();
    }
    return 0;
}
