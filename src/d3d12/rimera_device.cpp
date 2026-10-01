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

void RM::DestroyDevice(const Device* device)
{
    delete device;
}

RM::Device::Device(const DeviceCreateInfo& info)
{
#ifdef _DEBUG
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    {
        debug->EnableDebugLayer();
    }
#endif
    CreateDXGIFactory(IID_PPV_ARGS(&m_factory));
    m_factory->EnumAdapterByGpuPreference(0,
                                          DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                          IID_PPV_ARGS(m_adapter.GetAddressOf()));
    ComPtr<ID3D12Device> device;
    auto result = D3D12CreateDevice(m_adapter.Get(),
                                    D3D_FEATURE_LEVEL_12_2,
                                    IID_PPV_ARGS(device.GetAddressOf()));

    if (FAILED(result))
    {
        std::cout << std::string(HResultMessage(result)) << std::endl;
        throw std::runtime_error(
            "Failed to create D3D12 device, " + std::string(HResultMessage(result)));
    }
    device.As(&m_device);

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

    m_graphics_queue = CreateQueue(QueueType::Graphics);

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
        throw std::runtime_error(
            "Failed to create swapchain, " + std::string(HResultMessage(result)));
    }
    swapchain.As(&m_swapchain);
    D3D12MA::ALLOCATOR_DESC alloc_desc{
        .Flags = D3D12MA::ALLOCATOR_FLAG_SINGLETHREADED,
        .pDevice = m_device.Get(),
        .pAdapter = m_adapter.Get(),
    };
    D3D12MA::CreateAllocator(&alloc_desc, &m_allocator);

    m_rtv_heap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 64);
    m_dsv_heap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64);
    m_srv_heap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8192);
    m_sampler_heap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 64);

    m_core_residency_set = CreateResidencySet();
    for (int i = 0; i < 3; i++)
    {
        ID3D12Resource* swapchain_texture = nullptr;
        m_swapchain->GetBuffer(i, IID_PPV_ARGS(&swapchain_texture));
        m_frame_datas[i].swapchain_texture = new Texture(this,
                                                         swapchain_texture,
                                                         TextureUsage::RenderTarget);
        m_frame_datas[i].swapchain_rtv = CreateTextureView({
            .texture = m_frame_datas[i].swapchain_texture,
            .view_type = TextureViewType::RenderTarget,
        });
        m_frame_datas[i].fence_value = 0;
        m_frame_datas[i].swapchain_fence = CreateFence();
        m_core_residency_set->AddAllocation(m_frame_datas[i].swapchain_texture);
    }

    CreateRootSignature();
}

RM::Device::~Device()
{
    m_core_residency_set->RemoveAllAllocations();
    for (int i = 0; i < 3; i++)
    {
        DestroyTexture(m_frame_datas[i].swapchain_texture);
        DestroyTextureView(m_frame_datas[i].swapchain_rtv);
        DestroyFence(m_frame_datas[i].swapchain_fence);
    }
    DestroyResidencySet(m_core_residency_set);

    m_graphics_queue->WaitIdle();
    DestroyQueue(m_graphics_queue);

    if (m_callback_registered)
    {
        m_info_queue->UnregisterMessageCallback(m_callback_cookie);
    }
}

RM::Fence* RM::Device::CreateFence()
{
    return new Fence(this);
}

void RM::Device::DestroyFence(const Fence* fence)
{
    delete fence;
}

RM::Queue* RM::Device::CreateQueue(QueueType type)
{
    return new Queue(this, type);
}

void RM::Device::DestroyQueue(const Queue* queue)
{
    delete queue;
}

RM::Command* RM::Device::CreateCommand(CommandType type)
{
    return new Command(this, type);
}

void RM::Device::DestroyCommand(const Command* command) const
{
    delete command;
}

RM::CommandRing* RM::Device::CreateCommandRing(QueueType type)
{
    return new CommandRing(this, type);
}

void RM::Device::DestroyCommandRing(const CommandRing* command_ring) const
{
    delete command_ring;
}

RM::ResidencySet* RM::Device::CreateResidencySet()
{
    return new ResidencySet(this);
}

void RM::Device::DestroyResidencySet(const ResidencySet* residency_set) const
{
    delete residency_set;
}

RM::Pipeline* RM::Device::CreatePipeline(const ComputePipelineCreateInfo& info)
{
    return new Pipeline(this, info);
}

RM::Pipeline* RM::Device::CreatePipeline(const GraphicsPipelineCreateInfo& info)
{
    return new Pipeline(this, info);
}

void RM::Device::DestroyPipeline(const Pipeline* pipeline)
{
    delete pipeline;
}

RM::Texture* RM::Device::CreateTexture(const TextureCreateInfo& info)
{
    return new Texture(this, info);
}

void RM::Device::DestroyTexture(const Texture* texture) const
{
    delete texture;
}

RM::TextureView* RM::Device::CreateTextureView(const TextureViewCreateInfo& info)
{
    return new TextureView(this, info);
}

void RM::Device::DestroyTextureView(const TextureView* texture_view) const
{
    delete texture_view;
}

RM::Buffer* RM::Device::CreateBuffer(const BufferCreateInfo& info)
{
    return new Buffer(this, info);
}

void RM::Device::DestroyBuffer(const Buffer* buffer)
{
    delete buffer;
}

RM::Sampler* RM::Device::CreateSampler(const SamplerCreateInfo& info)
{
    return new Sampler(this, info);
}

void RM::Device::DestroySampler(const Sampler* sampler)
{
    delete sampler;
}

uint32_t RM::Device::AddDescriptor(const Buffer* buffer)
{
    uint32_t index;
    if (!m_srv_heap.free_descriptors.empty())
    {
        index = m_srv_heap.free_descriptors.back();
        m_srv_heap.free_descriptors.pop_back();
    }
    else
    {
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

uint32_t RM::Device::AddDescriptor(TextureViewType view_type)
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
    uint32_t index;
    if (!heap->free_descriptors.empty())
    {
        index = heap->free_descriptors.back();
        heap->free_descriptors.pop_back();
    }
    else
    {
        index = heap->index++;
    }

    return index;
}

uint32_t RM::Device::AddDescriptor(Sampler* sampler)
{
    uint32_t index;
    if (!m_sampler_heap.free_descriptors.empty())
    {
        index = m_sampler_heap.free_descriptors.back();
        m_sampler_heap.free_descriptors.pop_back();
    }
    else
    {
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

void RM::Device::RemoveDescriptor(const TextureView* texture)
{
    DescriptorHeap* heap = nullptr;
    switch (texture->m_info.view_type)
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
    heap->free_descriptors.push_back(texture->GetResourceIndex());
}

void RM::Device::RemoveDescriptor(const Sampler* sampler)
{
    m_sampler_heap.free_descriptors.push_back(sampler->GetSamplerIndex());
}

void* RM::Device::GetNativeRootSignature() const
{
    return m_root_sig.Get();
}

RM::TextureView* RM::Device::GetCurrentBackBuffer()
{
    return m_frame_datas[m_swapchain->GetCurrentBackBufferIndex()].swapchain_rtv;
}

void RM::Device::NewFrame(Queue*)
{
    m_frame_datas[m_current_frame].swapchain_fence->
                                   Wait(m_frame_datas[m_current_frame].fence_value);
}

void RM::Device::Resize(uint32_t width, uint32_t height)
{
    for (auto& frame_data : m_frame_datas)
    {
        delete frame_data.swapchain_texture;
        delete frame_data.swapchain_rtv;
        frame_data.swapchain_texture = nullptr;
        frame_data.swapchain_rtv = nullptr;
    }
    m_swapchain->ResizeBuffers(3,
                               width,
                               height,
                               DXGI_FORMAT_R8G8B8A8_UNORM,
                               DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);

    for (int i = 0; i < 3; i++)
    {
        ID3D12Resource* swapchain_texture = nullptr;
        m_swapchain->GetBuffer(i, IID_PPV_ARGS(&swapchain_texture));
        m_frame_datas[i].swapchain_texture = new Texture(this,
                                                         swapchain_texture,
                                                         TextureUsage::RenderTarget);
        m_frame_datas[i].swapchain_rtv = CreateTextureView({
            .texture = m_frame_datas[i].swapchain_texture,
            .view_type = TextureViewType::RenderTarget,
        });
    }
}

void* RM::Device::GetNativeHandle() const
{
    return m_device.Get();
}

void RM::Device::CreateRootSignature()
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
        throw std::runtime_error("Failed to serialize root signature");
    }

    result = m_device->CreateRootSignature(0,
                                           root_signature_blob->GetBufferPointer(),
                                           root_signature_blob->GetBufferSize(),
                                           IID_PPV_ARGS(m_root_sig.GetAddressOf()));
    if (FAILED(result))
    {
        throw std::runtime_error("Failed to create root signature");
    }
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

RM::Device::DescriptorHeap RM::Device::CreateDescriptorHeap(
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
        throw std::runtime_error("Failed to create descriptor heap");
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