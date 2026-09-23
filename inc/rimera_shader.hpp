#pragma once
#include "Metal/Metal.hpp"
#include "rimera_structs.hpp"

namespace RM
{
    class Device;
    class Shader
    {
    public:
        [[nodiscard]] void* GetNativeHandle() const;
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
        
        [[nodiscard]] ShaderType GetShaderType() const { return m_type; }

    private:
        friend class Device;
        Shader(Device* device, const ComputeShaderCreateInfo& info);
        Device* device;
        ShaderType m_type;
        NS::SharedPtr<MTL::RenderPipelineState> m_render_pso;
        NS::SharedPtr<MTL::ComputePipelineState> m_compute_pso;
        DispatchGroup m_compute_dispatch_group{};
        DispatchGroup m_mesh_dispatch_group{};
        DispatchGroup m_ampl_dispatch_group{};
    };
}  // namespace RM
