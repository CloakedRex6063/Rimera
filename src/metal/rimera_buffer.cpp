#include "rimera_buffer.hpp"
#include "rimera_device.hpp"

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

RM::Buffer::~Buffer()
{
    if (m_device && m_resource_index != UINT32_MAX) m_device->RemoveDescriptor(this);
}