#include "rimera_device.hpp"

#include "rimera_d3d12.hpp"
#include "rimera_helpers.hpp"
#include "directx/d3d12.h"

extern "C"
{
__declspec(dllexport) extern const UINT D3D12SDKVersion = 619;
__declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}

static void CALLBACK D3D12LogCallback(
    D3D12_MESSAGE_CATEGORY,
    D3D12_MESSAGE_SEVERITY,
    D3D12_MESSAGE_ID id,
    LPCSTR message,
    void*)
{
    std::fprintf(stderr,
                 "[D3D12:%u] %s\n",
                 static_cast<unsigned>(id),
                 message);
}

RM::Result<void> RM::Device::Initialize(const DeviceCreateInfo& info)
{
    if (!info.resource_descriptor_count || !info.sampler_descriptor_count)
        return std::unexpected("Invalid device creation parameters");
#ifdef RIMERA_DEBUG
    D3D12::EnableDebugLayer();
#endif
    if (FAILED(CreateDXGIFactory(IID_PPV_ARGS(&m_factory))))
        return std::unexpected("Failed to create DXGI factory");
    if (FAILED(m_factory->EnumAdapterByGpuPreference(0,
                                                     DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                     IID_PPV_ARGS(m_adapter.GetAddressOf()))))
        return std::unexpected("Failed to select adapter");
    ComPtr<ID3D12Device> device;
    auto result = D3D12CreateDevice(m_adapter.Get(),
                                    D3D_FEATURE_LEVEL_12_2,
                                    IID_PPV_ARGS(device.GetAddressOf()));

    if (FAILED(result))
    {
        std::cout << D3D12::HResultMessage(result) << std::endl;
        return std::unexpected("Failed to create D3D12 device, " +
                               D3D12::HResultMessage(result));
    }
    if (FAILED(device.As(&m_device)))
        return std::unexpected("Required D3D12 device interface is unavailable");

#ifdef _DEBUG
    if (SUCCEEDED(m_device.As(&m_info_queue)))
    {
        m_callback_registered = SUCCEEDED(m_info_queue->RegisterMessageCallback(
            D3D12LogCallback,
            D3D12_MESSAGE_CALLBACK_FLAG_NONE,
            nullptr,
            &m_callback_cookie));
    }
#endif

    D3D12MA::ALLOCATOR_DESC alloc_desc{
        .Flags = D3D12MA::ALLOCATOR_FLAG_SINGLETHREADED,
        .pDevice = m_device.Get(),
        .pAdapter = m_adapter.Get(),
    };
    if (FAILED(D3D12MA::CreateAllocator(&alloc_desc, &m_allocator)))
        return std::unexpected("Failed to create memory allocator");

    auto rtv_heap_result = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 64);
    if (!rtv_heap_result) return std::unexpected(rtv_heap_result.error());
    m_rtv_heap = std::move(*rtv_heap_result);
    auto dsv_heap_result = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64);
    if (!dsv_heap_result) return std::unexpected(dsv_heap_result.error());
    m_dsv_heap = std::move(*dsv_heap_result);
    auto srv_heap_result = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                info.resource_descriptor_count);
    if (!srv_heap_result) return std::unexpected(srv_heap_result.error());
    m_srv_heap = std::move(*srv_heap_result);
    auto sampler_heap_result =
        CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, info.sampler_descriptor_count);
    if (!sampler_heap_result) return std::unexpected(sampler_heap_result.error());
    m_sampler_heap = std::move(*sampler_heap_result);

    auto core_residency_set_result = CreateResidencySet();
    if (!core_residency_set_result) return std::unexpected(core_residency_set_result.error());
    m_core_residency_set = std::move(*core_residency_set_result);
    auto frame_fence_result = CreateFence();
    if (!frame_fence_result) return std::unexpected(frame_fence_result.error());
    m_frame_fence = *frame_fence_result;

    if (auto result = CreateRootSignature(); !result) return std::unexpected(result.error());

    for (const auto format : {Format::RGBA16_Float, Format::BGRA8_UNorm})
    {
        auto blit_result = Helpers::CreateBlitShader(this, format);
        if (!blit_result) return std::unexpected(blit_result.error());
        m_blit_pipelines[format] = *blit_result;
    }
    return {};
}

RM::Device::~Device()
{
    if (m_frame_fence) static_cast<void>(m_frame_fence->Wait());
    FlushRetired(nullptr);
    if (m_upload_command) DestroyImmediate(m_upload_command);
    if (m_upload_queue) DestroyImmediate(m_upload_queue);
    if (m_upload_fence) DestroyImmediate(m_upload_fence);
    if (m_frame_fence) DestroyImmediate(m_frame_fence);
    for (auto [format, pipeline] : m_blit_pipelines) DestroyImmediate(pipeline);
    DestroyImmediate(m_core_residency_set);
#ifdef RIMERA_DEBUG
    ValidateLiveObjects();
#endif

    if (m_callback_registered)
    {
        m_info_queue->UnregisterMessageCallback(m_callback_cookie);
    }
}

RM::Result<RM::ResourceAllocationInfo> RM::Device::GetAllocationInfo(
    const TextureCreateInfo& info,
    HeapMemory) const
{
    if (auto result = Texture::ValidateCreateInfo(info); !result)
        return std::unexpected(result.error());
    const auto desc = D3D12::GetResourceDesc(info);
    const auto allocation = m_device->GetResourceAllocationInfo(0, 1, &desc);
    if (allocation.SizeInBytes == UINT64_MAX)
        return std::unexpected("Failed to query texture allocation info");
    return ResourceAllocationInfo{.size = allocation.SizeInBytes,
                                  .alignment = allocation.Alignment};
}

RM::Result<RM::ResourceAllocationInfo> RM::Device::GetAllocationInfo(
    const BufferCreateInfo& info,
    HeapMemory) const
{
    if (!info.size) return std::unexpected("Invalid buffer creation parameters");
    auto desc = D3D12::GetResourceDesc(info);
    if (info.flags & BufferFlags::eUnorderedAccess)
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const auto allocation = m_device->GetResourceAllocationInfo(0, 1, &desc);
    if (allocation.SizeInBytes == UINT64_MAX)
        return std::unexpected("Failed to query buffer allocation info");
    return ResourceAllocationInfo{.size = allocation.SizeInBytes,
                                  .alignment = allocation.Alignment};
}

RM::TextureCopyFootprint RM::Device::GetCopyFootprint(const Texture* texture,
                                                      uint32_t mip_level) const
{
    const auto desc = static_cast<ID3D12Resource*>(texture->GetNativeHandle())->GetDesc();
    const uint32_t subresource =
        texture->GetBaseMip() + mip_level + texture->GetBaseLayer() * desc.MipLevels;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT row_count = 0;
    UINT64 row_size = 0;
    UINT64 total_size = 0;
    m_device->GetCopyableFootprints(&desc,
                                    subresource,
                                    1,
                                    0,
                                    &footprint,
                                    &row_count,
                                    &row_size,
                                    &total_size);
    if (total_size == UINT64_MAX) return {};
    return {.bytes_per_row = footprint.Footprint.RowPitch,
            .row_size = static_cast<uint32_t>(row_size),
            .row_count = row_count,
            .size = total_size};
}

RM::Result<uint32_t> RM::Device::AddDescriptor(const Buffer* buffer)
{
    uint32_t index;
    if (!m_srv_heap.free_descriptors.empty())
    {
        index = m_srv_heap.free_descriptors.back();
        m_srv_heap.free_descriptors.pop_back();
    }
    else
    {
        if (m_srv_heap.index >= m_srv_heap.heap->GetDesc().NumDescriptors)
            return std::unexpected("Descriptor heap is full");
        index = m_srv_heap.index++;
    }
    auto cpu_handle = m_srv_heap.cpu_base;
    cpu_handle.ptr += index * m_srv_heap.stride;
    auto* resource = static_cast<ID3D12Resource*>(buffer->GetNativeHandle());
    if (buffer->m_flags & BufferFlags::eUnorderedAccess)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc
        {
            .Format = DXGI_FORMAT_R32_TYPELESS,
            .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
            .Buffer = {
                .FirstElement = 0,
                .NumElements = buffer->GetLength() / 4,
                .StructureByteStride = 0,
                .Flags = D3D12_BUFFER_UAV_FLAG_RAW
            },
        };
        m_device->CreateUnorderedAccessView(resource, nullptr, &uav_desc, cpu_handle);
        return index;
    }
    const D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{
        .Format = DXGI_FORMAT_R32_TYPELESS,
        .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Buffer = {
            .FirstElement = 0,
            .NumElements = buffer->GetLength() / 4,
            .StructureByteStride = 0,
            .Flags = D3D12_BUFFER_SRV_FLAG_RAW,
        },
    };
    m_device->CreateShaderResourceView(resource, &srv_desc, cpu_handle);
    return index;
}

RM::Result<uint32_t> RM::Device::AddDescriptor(TextureViewType view_type)
{
    DescriptorHeap* heap = nullptr;
    switch (view_type)
    {
        case TextureViewType::RenderTarget:
            heap = &m_rtv_heap;
            break;
        case TextureViewType::DepthStencil:
            heap = &m_dsv_heap;
            break;
        case TextureViewType::ShaderRead:
        case TextureViewType::ShaderReadWrite:
            heap = &m_srv_heap;
            break;
    }
    if (!heap) return std::unexpected("Invalid descriptor type");
    uint32_t index;
    if (!heap->free_descriptors.empty())
    {
        index = heap->free_descriptors.back();
        heap->free_descriptors.pop_back();
    }
    else
    {
        if (heap->index >= heap->heap->GetDesc().NumDescriptors)
            return std::unexpected("Descriptor heap is full");
        index = heap->index++;
    }

    return index;
}

RM::Result<uint32_t> RM::Device::AddDescriptor(Sampler* sampler)
{
    uint32_t index;
    if (!m_sampler_heap.free_descriptors.empty())
    {
        index = m_sampler_heap.free_descriptors.back();
        m_sampler_heap.free_descriptors.pop_back();
    }
    else
    {
        if (m_sampler_heap.index >= m_sampler_heap.heap->GetDesc().NumDescriptors)
            return std::unexpected("Descriptor heap is full");
        index = m_sampler_heap.index++;
    }
    sampler->m_cpu_handle.ptr = m_sampler_heap.cpu_base.ptr + index * m_sampler_heap.stride;
    sampler->m_gpu_handle.ptr = m_sampler_heap.gpu_base.ptr + index * m_sampler_heap.stride;
    return index;
}

void RM::Device::RemoveDescriptor(const Buffer* buffer)
{
    m_srv_heap.free_descriptors.push_back(buffer->GetResourceIndex());
}

void RM::Device::RemoveDescriptor(const Texture* texture)
{
    for (uint32_t i = 0; i < texture->m_descriptors.size(); ++i)
    {
        if (texture->m_descriptors[i] == UINT32_MAX) continue;
        const auto type = static_cast<TextureViewType>(i);
        DescriptorHeap* heap = nullptr;
        switch (type)
        {
            case TextureViewType::RenderTarget:
                heap = &m_rtv_heap;
                break;
            case TextureViewType::DepthStencil:
                heap = &m_dsv_heap;
                break;
            case TextureViewType::ShaderRead:
            case TextureViewType::ShaderReadWrite:
                heap = &m_srv_heap;
                break;
        }
        heap->free_descriptors.push_back(texture->m_descriptors[i]);
    }
}

void RM::Device::RemoveDescriptor(const Sampler* sampler)
{
    m_sampler_heap.free_descriptors.push_back(sampler->GetSamplerIndex());
}

void* RM::Device::GetNativeRootSignature() const
{
    return m_root_sig.Get();
}

RM::Result<void> RM::Device::NewFrame(Queue* queue)
{
    auto value = queue->Signal(m_frame_fence);
    if (!value) return std::unexpected(value.error());
    m_frame_values[m_current_frame] = *value;
    m_current_frame = (m_current_frame + 1) % m_frame_values.size();
    if (auto result = m_frame_fence->Wait(m_frame_values[m_current_frame]); !result)
        return result;
    ReleaseRetired();
    return {};
}

void* RM::Device::GetNativeHandle() const
{
    return m_device.Get();
}

RM::Result<void> RM::Device::CreateRootSignature()
{
    D3D12_DESCRIPTOR_RANGE1 ranges[2];

    ranges[0] = {
        .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        .NumDescriptors = UINT_MAX,
        .BaseShaderRegister = 0,
        .RegisterSpace = 0,
        .Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,
        .OffsetInDescriptorsFromTableStart = 0,
    };

    ranges[1] = {
        .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
        .NumDescriptors = UINT_MAX,
        .BaseShaderRegister = 0,
        .RegisterSpace = 0,
        .Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,
        .OffsetInDescriptorsFromTableStart = 0,
    };
    std::array root_params{
        D3D12_ROOT_PARAMETER1{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
            .Descriptor =
            {
                .ShaderRegister = 0,
                .RegisterSpace = 0,
            },
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
        },
        D3D12_ROOT_PARAMETER1{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
            .Descriptor =
            {
                .ShaderRegister = 1,
                .RegisterSpace = 0,
            },
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
        },
        D3D12_ROOT_PARAMETER1{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
            .Descriptor =
            {
                .ShaderRegister = 2,
                .RegisterSpace = 0,
            },
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
        },
        D3D12_ROOT_PARAMETER1{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
            .Constants =
            {
                .ShaderRegister = Command::kRootConstantSlot,
                .RegisterSpace = 0,
                .Num32BitValues = Command::kRootConstantCount,
            },
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
        },
        D3D12_ROOT_PARAMETER1{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
            .DescriptorTable =
            {
                .NumDescriptorRanges = 1,
                .pDescriptorRanges = &ranges[0],
            },
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
        },
        D3D12_ROOT_PARAMETER1{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
            .DescriptorTable =
            {
                .NumDescriptorRanges = 1,
                .pDescriptorRanges = &ranges[1],
            },
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
        },
    };

    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc = {
        .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
        .Desc_1_1 = {
            .NumParameters = static_cast<uint32_t>(root_params.size()),
            .pParameters = root_params.data(),
            .Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                     D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED,
        }};

    ComPtr<ID3DBlob> error_blob = nullptr;
    ComPtr<ID3DBlob> root_signature_blob = nullptr;
    auto result = D3D12SerializeVersionedRootSignature(
        &root_signature_desc,
        &root_signature_blob,
        &error_blob);
    if (FAILED(result))
    {
        return std::unexpected("Failed to serialize root signature");
    }

    result = m_device->CreateRootSignature(0,
                                           root_signature_blob->GetBufferPointer(),
                                           root_signature_blob->GetBufferSize(),
                                           IID_PPV_ARGS(m_root_sig.GetAddressOf()));
    if (FAILED(result))
    {
        return std::unexpected("Failed to create root signature");
    }
    return {};
}

D3D12_CPU_DESCRIPTOR_HANDLE RM::Device::GetCPUDescriptor(TextureViewType type, uint32_t index)
{
    DescriptorHeap* heap = nullptr;
    switch (type)
    {
        case TextureViewType::RenderTarget:
            heap = &m_rtv_heap;
            break;
        case TextureViewType::DepthStencil:
            heap = &m_dsv_heap;
            break;
        case TextureViewType::ShaderRead:
        case TextureViewType::ShaderReadWrite:
            heap = &m_srv_heap;
            break;
    }
    return heap->GetCPUHandle(index);
}

D3D12_GPU_DESCRIPTOR_HANDLE RM::Device::GetGPUDescriptor(TextureViewType type, uint32_t index)
{
    DescriptorHeap* heap = nullptr;
    switch (type)
    {
        case TextureViewType::RenderTarget:
            heap = &m_rtv_heap;
            break;
        case TextureViewType::DepthStencil:
            heap = &m_dsv_heap;
            break;
        case TextureViewType::ShaderRead:
        case TextureViewType::ShaderReadWrite:
            heap = &m_srv_heap;
            break;
    }
    return heap->GetGPUHandle(index);
}

RM::Result<RM::Device::DescriptorHeap> RM::Device::CreateDescriptorHeap(
    const D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
    const uint32_t count) const
{
    DescriptorHeap heap{};
    auto flag = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || heap_type ==
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        flag = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    }
    const D3D12_DESCRIPTOR_HEAP_DESC desc = {
        .Type = heap_type,
        .NumDescriptors = count,
        .Flags = flag,
        .NodeMask = 0,
    };
    const auto result = m_device->CreateDescriptorHeap(
        &desc,
        IID_PPV_ARGS(heap.heap.GetAddressOf()));
    if (FAILED(result))
    {
        return std::unexpected("Failed to create descriptor heap");
    }
    heap.stride = m_device->GetDescriptorHandleIncrementSize(heap_type);
    heap.cpu_base = heap.heap->GetCPUDescriptorHandleForHeapStart();
    if (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
    {
        heap.gpu_base = heap.heap->GetGPUDescriptorHandleForHeapStart();
    }
    switch (heap_type)
    {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
            heap.heap->SetName(L"CBV_SRV_UAV Heap");
            break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
            heap.heap->SetName(L"RTV Heap");
            break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
            heap.heap->SetName(L"DSV Heap");
            break;
        default:
            break;
    }
    return heap;
}
