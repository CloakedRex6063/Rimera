#pragma once
#include "rimera_shader.hpp"
#include "rimera_structs.hpp"

namespace RM
{
    class Buffer;
    class Device;
    class Command
    {
    public:
        ~Command();

        void Begin() const;
        void End() const;

        void BeginRenderPass(const RenderPassCreateInfo& info);
        void EndRenderPass();
        
        void BeginComputePass();
        void EndComputePass();

        void BlitToSwapchain(const Device* device, const Texture* texture) const;

        void DispatchMesh(DispatchGroup dispatch_group) const;
        void Dispatch(DispatchGroup dispatch_group) const;

        void BindShader(const Shader* shader) const;
        void BindConstantBuffer(uint32_t slot, const Buffer* buffer) const;

        [[nodiscard]] void* GetNativeCommand() const;
        [[nodiscard]] void* GetNativeAllocator() const;

    private:
        struct BlitPassData
        {
            uint32_t source_index;
            uint32_t dest_index;
        };
#ifdef RIMERA_MAC
        Command(Device* device);
        NS::SharedPtr<MTL4::CommandBuffer> m_command_buffer;
        NS::SharedPtr<MTL4::CommandAllocator> m_command_allocator;

        std::shared_ptr<Buffer> m_blit_buffer;  // Reuse later if more custom functionality is added

        Device* m_device;
        std::shared_ptr<Shader> m_blit_shader;
        MTL4::RenderCommandEncoder* m_render_encoder = nullptr;
        MTL4::ComputeCommandEncoder* m_compute_encoder = nullptr;
        DispatchGroup m_threads_per_mesh{};
        DispatchGroup m_threads_per_ampl{};
        DispatchGroup m_threads_per_compute{};
        friend class CommandRing;
        friend class Device;
#endif
    };

    class CommandRing
    {
    public:
        CommandRing(Device* device);
        ~CommandRing();

        void Next() { m_current_buffer = (m_current_buffer + 1) % m_commands.size(); }
        [[nodiscard]] Command* GetCurrent() const { return m_commands[m_current_buffer]; }

    private:
        std::array<Command*, 3> m_commands{};
        uint32_t m_current_buffer = 0;
    };
}  // namespace RM
