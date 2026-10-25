#include "rimera_heap.hpp"
#include "rimera_device.hpp"
#include "rimera_vulkan.hpp"

RM::Heap::~Heap()
{
#ifdef RIMERA_DEBUG
    if (!m_allocations.empty())
        std::fprintf(stderr,
                     "[Rimera] Heap destroyed with %zu live allocations\n",
                     m_allocations.size());
#endif
    ReleaseResidency();
    if (m_device && m_allocation) vmaFreeMemory(m_device->m_allocator, m_allocation);
}

void* RM::Heap::GetNativeHandle() const
{
    return Vulkan::ToNative(m_allocation);
}

RM::Result<void> RM::Heap::Initialize(Device* device, const HeapCreateInfo& info)
{
    if (!device || !info.size) return std::unexpected("Invalid heap creation parameters");
    m_device = device;
    m_memory = info.memory;
    constexpr uint64_t alignment = 64 * 1024;
    m_size = (info.size + alignment - 1) / alignment * alignment;
    m_free_blocks = {Block{.offset = 0, .size = m_size}};

    constexpr BufferCreateInfo probe_buffer{.size = 64 * 1024, .data = nullptr, .flags = {}};
    const auto buffer_info = Vulkan::GetBufferCreateInfo(probe_buffer,
                                                         device->m_unique_queue_families);
    const VkDeviceBufferMemoryRequirements buffer_requirements_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS,
        .pCreateInfo = &buffer_info,
    };
    VkMemoryRequirements2 buffer_requirements{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    vkGetDeviceBufferMemoryRequirements(device->m_device,
                                        &buffer_requirements_info,
                                        &buffer_requirements);

    constexpr TextureCreateInfo probe_texture{
        .width = 256,
        .height = 256,
        .format = Format::RGBA8_UNorm,
        .usage = {TextureUsage::ShaderRead, TextureUsage::ShaderWrite, TextureUsage::RenderTarget},
    };
    const auto image_info = Vulkan::GetImageCreateInfo(probe_texture,
                                                       device->m_unique_queue_families,
                                                       device->SupportsHostImageTransfer(
                                                           probe_texture.format));
    const VkDeviceImageMemoryRequirements image_requirements_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS,
        .pCreateInfo = &image_info,
        .planeAspect = VK_IMAGE_ASPECT_COLOR_BIT,
    };
    VkMemoryRequirements2 image_requirements{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    vkGetDeviceImageMemoryRequirements(device->m_device,
                                       &image_requirements_info,
                                       &image_requirements);

    const VkMemoryRequirements requirements{
        .size = m_size,
        .alignment = alignment,
        .memoryTypeBits = buffer_requirements.memoryRequirements.memoryTypeBits &
                          image_requirements.memoryRequirements.memoryTypeBits,
    };

    VmaAllocationCreateInfo allocation_info{.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT};
    if (info.memory == MemoryType::HostVisible)
    {
        allocation_info.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocation_info.requiredFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (device->m_gpu_upload_heap)
            allocation_info.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    else
    {
        allocation_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    VmaAllocationInfo result_info{};
    if (const auto result = vmaAllocateMemory(device->m_allocator,
                                              &requirements,
                                              &allocation_info,
                                              &m_allocation,
                                              &result_info);
        result != VK_SUCCESS)
        return std::unexpected("Failed to create heap, " + Vulkan::ResultMessage(result));
    m_mapped_data = result_info.pMappedData;
    m_device->GetCoreResidencySet()->AddAllocation(this);
    return {};
}
