#include "rimera_buffer.hpp"
#include "rimera_d3d12.hpp"
#include "rimera_device.hpp"
#include "rimera_heap.hpp"

RM::Buffer::~Buffer()
{
    ReleaseResidency();
    if (m_device && m_resource_index != UINT32_MAX) m_device->RemoveDescriptor(this);
    Unmap();
    if (m_heap && m_heap_allocated) m_heap->Free(m_heap_offset);
}

void* RM::Buffer::GetNativeHandle() const noexcept { return m_buffer.Get(); }

RM::Result<void*> RM::Buffer::Map()
{
    if (m_mapped)
    {
        return m_data;
    }
    const auto result = m_buffer->Map(0, nullptr, &m_data);
    if (FAILED(result))
    {
        return std::unexpected("Failed to map buffer");
    }
    m_mapped = true;
    return m_data;
}

void RM::Buffer::Unmap()
{
    if (m_mapped)
    {
        m_buffer->Unmap(0, nullptr);
        m_mapped = false;
    }
}

uint64_t RM::Buffer::GetGPUAddress() const
{
    return m_buffer->GetGPUVirtualAddress();
}

RM::Result<void> RM::Buffer::Initialize(Device* device, const BufferCreateInfo& info)
{
    m_device = device;
    if (!device || !info.size) return std::unexpected("Invalid buffer creation parameters");
    m_length = info.size;
    m_flags = info.flags;
    auto resource_info = D3D12::GetResourceDesc(info);

    if (info.flags & BufferFlags::eUnorderedAccess)
    {
        resource_info.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    constexpr D3D12MA::ALLOCATION_DESC alloc_desc = {
        .HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD,
    };
    const auto result = m_device->m_allocator->CreateResource(&alloc_desc,
                                                              &resource_info,
                                                              D3D12_RESOURCE_STATE_COMMON,
                                                              nullptr,
                                                              &m_allocation,
                                                              IID_PPV_ARGS(&m_buffer));
    if (FAILED(result))
    {
        return std::unexpected("Failed to create buffer");
    }
    if (info.data)
    {
        auto mapped = Map();
        if (!mapped) return std::unexpected(mapped.error());
        std::memcpy(*mapped, info.data, info.size);
        Unmap();
    }
    auto descriptor = m_device->AddDescriptor(this);
    if (!descriptor) return std::unexpected(descriptor.error());
    m_resource_index = *descriptor;
    return {};
}

RM::Result<void> RM::Buffer::Initialize(Device* device,
                                        Heap* heap,
                                        uint64_t offset,
                                        const BufferCreateInfo& info)
{
    m_device = device;
    if (!device || !heap || !info.size)
        return std::unexpected("Invalid buffer creation parameters");
    if (info.data && heap->GetMemory() != HeapMemory::HostVisible)
        return std::unexpected("Initial buffer data requires a host visible heap");
    m_length = info.size;
    m_flags = info.flags;
    m_heap = heap;
    m_heap_offset = offset;
    auto resource_info = D3D12::GetResourceDesc(info);

    if (info.flags & BufferFlags::eUnorderedAccess)
    {
        resource_info.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    const D3D12_RESOURCE_DESC1 desc1{
        .Dimension = resource_info.Dimension,
        .Alignment = resource_info.Alignment,
        .Width = resource_info.Width,
        .Height = resource_info.Height,
        .DepthOrArraySize = resource_info.DepthOrArraySize,
        .MipLevels = resource_info.MipLevels,
        .Format = resource_info.Format,
        .SampleDesc = resource_info.SampleDesc,
        .Layout = resource_info.Layout,
        .Flags = resource_info.Flags,
        .SamplerFeedbackMipRegion = {},
    };
    auto* native_device = static_cast<ID3D12Device15*>(m_device->GetNativeHandle());
    const auto result = native_device->CreatePlacedResource2(
        static_cast<ID3D12Heap*>(heap->GetNativeHandle()),
        offset,
        &desc1,
        D3D12_BARRIER_LAYOUT_UNDEFINED,
        nullptr,
        0,
        nullptr,
        IID_PPV_ARGS(&m_buffer));
    if (FAILED(result))
    {
        return std::unexpected("Failed to create placed buffer");
    }
    if (info.data)
    {
        auto mapped = Map();
        if (!mapped) return std::unexpected(mapped.error());
        std::memcpy(*mapped, info.data, info.size);
        Unmap();
    }
    auto descriptor = m_device->AddDescriptor(this);
    if (!descriptor) return std::unexpected(descriptor.error());
    m_resource_index = *descriptor;
    return {};
}
