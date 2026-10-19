#include "rimera_buffer.hpp"
#include "rimera_device.hpp"
#include "rimera_heap.hpp"

void* RM::Buffer::GetNativeHandle() const noexcept { return m_buffer.get(); }

RM::Result<void*> RM::Buffer::Map() { return m_buffer->contents(); }

void RM::Buffer::Unmap()
{
}

uint64_t RM::Buffer::GetGPUAddress() const { return m_buffer->gpuAddress(); }

RM::Result<void> RM::Buffer::Initialize(Device* device, const BufferCreateInfo& info)
{
    m_device = device;
    if (!device || !info.size) return std::unexpected("Invalid buffer creation parameters");
    auto* native_device = static_cast<MTL::Device*>(device->GetNativeHandle());
    m_length = info.size;
    if (info.data)
    {
        m_buffer = NS::TransferPtr(
            native_device->newBuffer(info.data, info.size, MTL::ResourceStorageModeShared));
    }
    else
    {
        m_buffer = NS::TransferPtr(
            native_device->newBuffer(info.size, MTL::ResourceStorageModeShared));
    }
    if (!m_buffer) return std::unexpected("Metal buffer allocation failed");
    auto resource_index_result = m_device->AddDescriptor(this);
    if (!resource_index_result) return std::unexpected(resource_index_result.error());
    m_resource_index = std::move(*resource_index_result);
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
    auto* native_heap = static_cast<MTL::Heap*>(heap->GetNativeHandle());
    m_buffer =
        NS::TransferPtr(native_heap->newBuffer(info.size, native_heap->resourceOptions(), offset));
    if (!m_buffer) return std::unexpected("Metal placed buffer creation failed");
    if (info.data) std::memcpy(m_buffer->contents(), info.data, info.size);
    auto resource_index_result = m_device->AddDescriptor(this);
    if (!resource_index_result) return std::unexpected(resource_index_result.error());
    m_resource_index = std::move(*resource_index_result);
    return {};
}

RM::Buffer::~Buffer()
{
    if (m_device && m_resource_index != UINT32_MAX) m_device->RemoveDescriptor(this);
    if (m_heap && m_heap_allocated) m_heap->Free(m_heap_offset);
}