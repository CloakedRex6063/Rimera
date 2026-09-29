#include "rimera_buffer.hpp"
#include "rimera_d3d12.hpp"
#include "rimera_device.hpp"
#include "stdexcept"

RM::Buffer::~Buffer()
{
    m_device->RemoveDescriptor(this);
    Unmap();
}

void* RM::Buffer::GetNativeHandle() const noexcept { return m_buffer.Get(); }

void* RM::Buffer::Map()
{
    if (m_mapped)
    {
        return m_data;
    }
    const auto result = m_buffer->Map(0, nullptr, &m_data);
    if (FAILED(result))
    {
        throw std::runtime_error("Failed to map buffer");
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

RM::Buffer::Buffer(Device* device, const BufferCreateInfo& info) : m_device(device)
{
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
        throw std::runtime_error("Failed to create buffer");
    }
}