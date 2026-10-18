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

static std::string HResultMessage(const HRESULT hr)
{
    char* buffer = nullptr;
    DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(hr),
        0,
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);

    if (length == 0)
        return "Unknown HRESULT";

    std::string message(buffer, length);
    LocalFree(buffer);

    while (!message.empty() &&
           (message.back() == '\r' || message.back() == '\n'))
        message.pop_back();

    return message;
}

RM::Result<RM::Device*> RM::CreateDevice(const DeviceCreateInfo& info)
{
    return Device::CreateObject<Device>(info);
}

void RM::DestroyDevice(const Device* device)
{
    delete device;
}

RM::Result<void> RM::Device::Initialize(const DeviceCreateInfo& info)
{
    if (!info.window || !info.width || !info.height || !info.resource_descriptor_count ||
        !info.sampler_descriptor_count)
        return std::unexpected("Invalid device creation parameters");
#ifdef _DEBUG
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    {
        debug->EnableDebugLayer();
    }
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
        std::cout << std::string(HResultMessage(result)) << std::endl;
        return std::unexpected("Failed to create D3D12 device, " +
                               std::string(HResultMessage(result)));
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

    auto graphics_queue_result = CreateQueue(QueueType::Graphics);
    if (!graphics_queue_result) return std::unexpected(graphics_queue_result.error());
    m_graphics_queue = std::move(*graphics_queue_result);

    DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {
        .Width = info.width,
        .Height = info.height,
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .Stereo = false,
        .SampleDesc = DXGI_SAMPLE_DESC{
            .Count = 1,
            .Quality = 0,
        },
        .BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 3,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
        .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING,
    };
    ComPtr<IDXGISwapChain1> swapchain;
    result = m_factory->CreateSwapChainForHwnd(m_graphics_queue->m_queue.Get(),
                                               static_cast<HWND>(info.window),
                                               &swapchain_desc,
                                               nullptr,
                                               nullptr,
                                               swapchain.GetAddressOf());

    if (FAILED(result))
    {
        std::cout << "Failed to create swapchain, " + std::string(HResultMessage(result)) <<
            '\n';
        return std::unexpected("Failed to create swapchain, " +
                               std::string(HResultMessage(result)));
    }
    if (FAILED(swapchain.As(&m_swapchain)))
        return std::unexpected("Failed to obtain swapchain interface");
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
    for (int i = 0; i < 3; i++)
    {
        ID3D12Resource* swapchain_texture = nullptr;
        if (FAILED(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&swapchain_texture))))
            return std::unexpected("Failed to get swapchain buffer");
        auto texture = CreateObject<Texture>(this, swapchain_texture, TextureUsage::RenderTarget);
        swapchain_texture->Release();
        if (!texture) return std::unexpected(texture.error());
        m_frame_datas[i].swapchain_texture = *texture;
        m_frame_datas[i].fence_value = 0;
        auto fence_result = CreateFence();
        if (!fence_result) return std::unexpected(fence_result.error());
        m_frame_datas[i].swapchain_fence = std::move(*fence_result);
        m_core_residency_set->AddAllocation(m_frame_datas[i].swapchain_texture);
    }

    if (auto result = CreateRootSignature(); !result) return std::unexpected(result.error());
    return {};
}

RM::Device::~Device()
{
    if (m_graphics_queue) static_cast<void>(m_graphics_queue->WaitIdle());
    if (m_core_residency_set) m_core_residency_set->RemoveAllAllocations();
    for (int i = 0; i < 3; i++)
    {
        DestroyTexture(m_frame_datas[i].swapchain_texture);
        DestroyFence(m_frame_datas[i].swapchain_fence);
    }
    DestroyResidencySet(m_core_residency_set);
    DestroyQueue(m_graphics_queue);

    if (m_callback_registered)
    {
        m_info_queue->UnregisterMessageCallback(m_callback_cookie);
    }
}

RM::Result<RM::Fence*> RM::Device::CreateFence() { return CreateObject<Fence>(this); }

void RM::Device::DestroyFence(const Fence* fence)
{
    delete fence;
}

RM::Result<RM::Queue*> RM::Device::CreateQueue(QueueType type)
{
    return CreateObject<Queue>(this, type);
}

void RM::Device::DestroyQueue(const Queue* queue)
{
    delete queue;
}

RM::Result<RM::Command*> RM::Device::CreateCommand(CommandType type)
{
    return CreateObject<Command>(this, type);
}

void RM::Device::DestroyCommand(const Command* command) const
{
    delete command;
}

RM::Result<RM::CommandRing*> RM::Device::CreateCommandRing(QueueType type)
{
    return CreateObject<CommandRing>(this, type);
}

void RM::Device::DestroyCommandRing(const CommandRing* command_ring) const
{
    delete command_ring;
}

RM::Result<RM::ResidencySet*> RM::Device::CreateResidencySet()
{
    return CreateObject<ResidencySet>(this);
}

void RM::Device::DestroyResidencySet(const ResidencySet* residency_set) const
{
    delete residency_set;
}

RM::Result<RM::Pipeline*> RM::Device::CreatePipeline(const ComputePipelineCreateInfo& info)
{
    return CreateObject<Pipeline>(this, info);
}

RM::Result<RM::Pipeline*> RM::Device::CreatePipeline(const GraphicsPipelineCreateInfo& info)
{
    return CreateObject<Pipeline>(this, info);
}

void RM::Device::DestroyPipeline(const Pipeline* pipeline)
{
    delete pipeline;
}

RM::Result<RM::Texture*> RM::Device::CreateTexture(const TextureCreateInfo& info)
{
    return CreateObject<Texture>(this, info);
}

void RM::Device::DestroyTexture(const Texture* texture) const
{
    delete texture;
}

RM::Result<RM::Texture*> RM::Device::CreateTextureView(const TextureViewCreateInfo& info)
{
    return CreateObject<Texture>(this, info);
}

RM::Result<RM::Buffer*> RM::Device::CreateBuffer(const BufferCreateInfo& info)
{
    return CreateObject<Buffer>(this, info);
}

void RM::Device::DestroyBuffer(const Buffer* buffer)
{
    delete buffer;
}

RM::Result<RM::Sampler*> RM::Device::CreateSampler(const SamplerCreateInfo& info)
{
    return CreateObject<Sampler>(this, info);
}

void RM::Device::DestroySampler(const Sampler* sampler)
{
    delete sampler;
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
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc
    {
        .Format = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
        .Buffer = {
            .FirstElement = 0,
            .NumElements = 1,
            .StructureByteStride = sizeof(buffer->GetLength()),
            .Flags = D3D12_BUFFER_UAV_FLAG_NONE
        },
    };
    auto* resource = static_cast<ID3D12Resource*>(buffer->GetNativeHandle());
    m_device->CreateUnorderedAccessView(resource, nullptr, &uav_desc, cpu_handle);
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

RM::Texture* RM::Device::GetCurrentBackBuffer()
{
    return m_frame_datas[m_swapchain->GetCurrentBackBufferIndex()].swapchain_texture;
}

RM::Result<void> RM::Device::NewFrame(Queue*)
{
    return m_frame_datas[m_current_frame].swapchain_fence->Wait(
        m_frame_datas[m_current_frame].fence_value);
}

RM::Result<void> RM::Device::Resize(uint32_t width, uint32_t height)
{
    if (!width || !height) return {};
    if (auto result = m_graphics_queue->WaitIdle(); !result) return std::unexpected(result.error());
    for (auto& frame_data : m_frame_datas)
    {
        m_core_residency_set->RemoveAllocation(frame_data.swapchain_texture);
        delete frame_data.swapchain_texture;
        frame_data.swapchain_texture = nullptr;
    }
    if (FAILED(m_swapchain->ResizeBuffers(3,
                                          width,
                                          height,
                                          DXGI_FORMAT_R8G8B8A8_UNORM,
                                          DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)))
        return std::unexpected("Failed to resize swapchain");

    for (int i = 0; i < 3; i++)
    {
        ID3D12Resource* swapchain_texture = nullptr;
        if (FAILED(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&swapchain_texture))))
            return std::unexpected("Failed to get swapchain buffer");
        auto texture = CreateObject<Texture>(this, swapchain_texture, TextureUsage::RenderTarget);
        swapchain_texture->Release();
        if (!texture) return std::unexpected(texture.error());
        m_frame_datas[i].swapchain_texture = *texture;
        m_core_residency_set->AddAllocation(m_frame_datas[i].swapchain_texture);
    }
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
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
            .Constants =
            {
                .ShaderRegister = 0,
                .RegisterSpace = 0,
                .Num32BitValues = 32,
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