#include "rimera_buffer.hpp"
#include "rimera_device.hpp"
#include "rimera_heap.hpp"
#include "rimera_vulkan.hpp"

RM::Buffer::~Buffer()
{
    ReleaseResidency();
    if (m_device && m_resource_index != UINT32_MAX) m_device->RemoveDescriptor(this);
    Unmap();
    if (m_heap && m_heap_allocated) m_heap->Free(m_heap_offset);
    if (!m_device || !m_buffer) return;
    if (m_allocation)
        vmaDestroyBuffer(m_device->m_allocator, m_buffer, m_allocation);
    else
        vkDestroyBuffer(m_device->m_device, m_buffer, nullptr);
}

void* RM::Buffer::GetNativeHandle() const noexcept
{
    return Vulkan::ToNative(m_buffer);
}

RM::Result<void*> RM::Buffer::Map()
{
    if (!m_data) return std::unexpected("Buffer is not host visible");
    return m_data;
}

void RM::Buffer::Unmap()
{
    if (m_allocation && m_data) vmaFlushAllocation(m_device->m_allocator, m_allocation, 0, VK_WHOLE_SIZE);
}

uint64_t RM::Buffer::GetGPUAddress() const
{
    const VkBufferDeviceAddressInfo address_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = m_buffer,
    };
    return vkGetBufferDeviceAddress(m_device->m_device, &address_info);
}

bool RM::Buffer::IsHostVisible() const
{
    return m_data != nullptr;
}

RM::Result<void> RM::Buffer::Initialize(Device* device, const BufferCreateInfo& info)
{
    m_device = device;
    if (!device || !info.size) return std::unexpected("Invalid buffer creation parameters");
    m_length = info.size;
    m_flags = info.flags;

    const auto buffer_info = Vulkan::GetBufferCreateInfo(info, device->m_unique_queue_families);
    VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    if (device->m_gpu_upload_heap)
    {
        allocation_info.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocation_info.requiredFlags =
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    }
    else if (info.memory_type == MemoryType::HostVisible)
    {
        allocation_info.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocation_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    }
    else
    {
        allocation_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    VmaAllocationInfo result_info{};
    if (const auto result = vmaCreateBuffer(device->m_allocator,
                                            &buffer_info,
                                            &allocation_info,
                                            &m_buffer,
                                            &m_allocation,
                                            &result_info);
        result != VK_SUCCESS)
        return std::unexpected("Failed to create buffer, " + Vulkan::ResultMessage(result));
    m_data = result_info.pMappedData;
    if (auto result = WriteInitialData(info); !result) return result;
    return AddResourceDescriptor();
}

RM::Result<void> RM::Buffer::Initialize(Device* device,
                                        Heap* heap,
                                        uint64_t offset,
                                        const BufferCreateInfo& info)
{
    m_device = device;
    if (!device || !heap || !info.size)
        return std::unexpected("Invalid buffer creation parameters");
    m_length = info.size;
    m_flags = info.flags;
    m_heap = heap;
    m_heap_offset = offset;

    const auto buffer_info = Vulkan::GetBufferCreateInfo(info, device->m_unique_queue_families);
    if (const auto result = vmaCreateAliasingBuffer2(device->m_allocator,
                                                     heap->m_allocation,
                                                     offset,
                                                     &buffer_info,
                                                     &m_buffer);
        result != VK_SUCCESS)
        return std::unexpected("Failed to create placed buffer, " + Vulkan::ResultMessage(result));
    if (heap->m_mapped_data) m_data = static_cast<uint8_t*>(heap->m_mapped_data) + offset;
    if (auto result = WriteInitialData(info); !result) return result;
    return AddResourceDescriptor();
}

RM::Result<void> RM::Buffer::AddResourceDescriptor()
{
    auto descriptor = m_device->AddDescriptor(this);
    if (!descriptor) return std::unexpected(descriptor.error());
    m_resource_index = *descriptor;
    return {};
}
