#pragma once
#include "rimera_structs.hpp"
#include "rimera_compiler.hpp"

namespace RM::D3D12
{
    [[nodiscard]] Result<D3D12_SHADER_BYTECODE> GetShaderBytecode(const Shader* shader,
                                                                  ShaderType type);

#ifdef RIMERA_DEBUG
    void EnableDebugLayer();
#endif

    float GetSdrWhiteLevel(const wchar_t* device_name);
    std::string HResultMessage(HRESULT hr);

    D3D12_COMMAND_LIST_TYPE GetQueueType(QueueType type);
    D3D12_RESOURCE_DESC GetResourceDesc(const BufferCreateInfo& info);
    D3D12_RESOURCE_DESC GetResourceDesc(const TextureCreateInfo& info);
    D3D12_HEAP_TYPE GetHeapType(MemoryType memory);
    DXGI_FORMAT GetFormat(const Format& format);
    Format GetFormat(const DXGI_FORMAT& format);
    D3D12_BARRIER_ACCESS GetBarrierAccess(BarrierAccess access);
    D3D12_BARRIER_SYNC GetBarrierSync(BarrierStage stage);
    D3D12_BARRIER_TYPE GetBarrierType(BarrierType type);
    D3D12_BARRIER_LAYOUT GetBarrierLayout(BarrierLayout layout);

    D3D12_RENDER_PASS_BEGINNING_ACCESS ToBeginAccess(Format format,
                                                     LoadOp load_op,
                                                     ClearColor clear_color);

    D3D12_RENDER_PASS_BEGINNING_ACCESS ToBeginAccess(Format format,
                                                     LoadOp load_op,
                                                     float clear_depth,
                                                     uint8_t depth);

    D3D12_RENDER_PASS_ENDING_ACCESS ToEndAccess(StoreOp store_op);

    template <typename NativeBarrier, typename Barrier>
    void FillScope(NativeBarrier& dst, const Barrier& src)
    {
        dst.SyncBefore = GetBarrierSync(src.scope.sync_before);
        dst.SyncAfter = GetBarrierSync(src.scope.sync_after);

        dst.AccessBefore = GetBarrierAccess(src.scope.access_before);
        dst.AccessAfter = GetBarrierAccess(src.scope.access_after);
    }

    D3D12_GLOBAL_BARRIER ConvertBarrier(const GlobalBarrier& src);
    D3D12_TEXTURE_BARRIER ConvertBarrier(const TextureBarrier& src);
    D3D12_BUFFER_BARRIER ConvertBarrier(const BufferBarrier& src);

    D3D12_FILL_MODE GetFillMode(FillMode mode) noexcept;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE GetPolygonMode(PolygonMode mode) noexcept;
    D3D12_PRIMITIVE_TOPOLOGY GetPrimitiveTopology(PrimitiveTopology topology) noexcept;
    D3D12_COMPARISON_FUNC GetComparisonFunc(ComparisonFunc test) noexcept;
    D3D12_CULL_MODE GetCullMode(CullMode mode) noexcept;
    bool GetFrontFace(FrontFace front_face) noexcept;
    D3D12_CONSERVATIVE_RASTERIZATION_MODE GetConservativeRaster(bool conservative_raster) noexcept;
    D3D12_DEPTH_WRITE_MASK GetDepthWriteMask(bool depth_write_enable) noexcept;
    D3D12_STENCIL_OP GetStencilOp(StencilOp op) noexcept;
    D3D12_DEPTH_STENCILOP_DESC GetStencilOpDesc(StencilOpState op_desc) noexcept;
    D3D12_INPUT_CLASSIFICATION GetInputClassification(VertexInputRate rate) noexcept;
    D3D12_BLEND GetBlendFactor(BlendFactor factor) noexcept;
    D3D12_BLEND_OP GetBlendOp(BlendOp op) noexcept;
    D3D12_LOGIC_OP GetLogicOp(LogicOp op) noexcept;
    D3D12_FILTER ToFilter(Filter min_filter,
                          Filter mag_filter,
                          ReductionType type = ReductionType::Standard) noexcept;
    D3D12_TEXTURE_ADDRESS_MODE ToWrap(Wrap wrap) noexcept;
}
