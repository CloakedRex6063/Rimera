#include "rimera_buffer.hpp"
#include "rimera_d3d12.hpp"
#include "rimera_device.hpp"

RM::Buffer::~Buffer()
{
    if (m_device && m_resource_index != UINT32_MAX) m_device->RemoveDescriptor(this);
    Unmap();
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