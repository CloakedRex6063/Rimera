#pragma once

#include "rimera_structs.hpp"

namespace RM
{
    class Device;

    class Pipeline
    {
    public:
        [[nodiscard]] void* GetNativeHandle() const;

#ifdef RIMERA_MAC
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
        friend class Device;
        Pipeline(Device* device, const ComputePipelineCreateInfo& info);
        Pipeline(Device* device, const GraphicsPipelineCreateInfo& info);
        Pipeline(Device* device, const MeshGraphicsPipelineCreateInfo& info);

        Device* m_device;
        PipelineType m_type;
#ifdef RIMERA_MAC
        NS::SharedPtr<MTL::RenderPipelineState> m_render_pso;
        NS::SharedPtr<MTL::ComputePipelineState> m_compute_pso;
        DispatchGroup m_compute_dispatch_group{};
        DispatchGroup m_mesh_dispatch_group{};
        DispatchGroup m_ampl_dispatch_group{};
#elifdef RIMERA_WINDOWS
        ComPtr<ID3D12PipelineState> m_pso;
        static std::vector<D3D12_INPUT_ELEMENT_DESC> CreateInputLayout(
            std::span<const VertexBinding> bindings,
            std::span<const VertexAttribute> attributes);
#endif
    };
} // namespace RM