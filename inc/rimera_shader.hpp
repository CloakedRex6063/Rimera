#pragma once

#include "rimera_structs.hpp"

namespace RM
{
    class Device;

    class Pipeline
    {
    public:
        Pipeline(const Pipeline&) = delete;
        Pipeline& operator=(const Pipeline&) = delete;
#ifdef RIMERA_VULKAN
        ~Pipeline();
#endif
        [[nodiscard]] void* GetNativeHandle() const;

#ifdef RIMERA_METAL
        [[nodiscard]] DispatchGroup GetComputeThreadsPerThreadgroup() const
        {
            return m_compute_dispatch_group;
        }
        [[nodiscard]] DispatchGroup GetMeshThreadsPerThreadgroup() const
        {
            return m_mesh_dispatch_group;
        }
        [[nodiscard]] DispatchGroup GetAmplThreadsPerThreadgroup() const
        {
            return m_ampl_dispatch_group;
        }
#endif

        [[nodiscard]] PipelineType GetShaderType() const { return m_type; }

    private:
        Pipeline() = default;
        friend class Device;
        [[nodiscard]] Result<void> Initialize(Device* device,
                                              const ComputePipelineCreateInfo& info);
        [[nodiscard]] Result<void> Initialize(Device* device,
                                              const GraphicsPipelineCreateInfo& info);
        [[nodiscard]] Result<void> Initialize(Device* device,
                                              const MeshGraphicsPipelineCreateInfo& info);

        Device* m_device = nullptr;
        PipelineType m_type;
#ifdef RIMERA_METAL
        friend class Command;
        NS::SharedPtr<MTL::DepthStencilState> m_depth_stencil_state;
        RasterizerState m_rasterizer_state{};
        MTL::CullMode m_cull_mode = MTL::CullModeNone;
        MTL::Winding m_winding = MTL::WindingClockwise;
        MTL::TriangleFillMode m_fill_mode = MTL::TriangleFillModeFill;
        NS::SharedPtr<MTL::RenderPipelineState> m_render_pso;
        NS::SharedPtr<MTL::ComputePipelineState> m_compute_pso;
        DispatchGroup m_compute_dispatch_group{};
        DispatchGroup m_mesh_dispatch_group{};
        DispatchGroup m_ampl_dispatch_group{};
#elifdef RIMERA_D3D12
        ComPtr<ID3D12PipelineState> m_pso;
        [[nodiscard]] static Result<std::vector<D3D12_INPUT_ELEMENT_DESC>> CreateInputLayout(
            std::span<const VertexBinding> bindings,
            std::span<const VertexAttribute> attributes);
#elifdef RIMERA_VULKAN
        VkPipeline m_pipeline = VK_NULL_HANDLE;
#endif
    };
} // namespace RM