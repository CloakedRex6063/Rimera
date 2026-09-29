#include "rimera_buffer.hpp"
#include "rimera_device.hpp"
#include "stdexcept"

void* RM::Buffer::GetNativeHandle() const { return m_buffer.get(); }

void* RM::Buffer::GetContents() const { return m_buffer->contents(); }

uint64_t RM::Buffer::GetGPUAddress() const { return m_buffer->gpuAddress(); }

RM::Buffer::Buffer(Device* device, const BufferCreateInfo& info) : m_device(device)
{
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
    if (!m_buffer) throw std::runtime_error("Metal buffer allocation failed");
    m_resource_index = m_device->AddDescriptor(this);
}

RM::Buffer::~Buffer() { m_device->RemoveDescriptor(m_resource_index); }