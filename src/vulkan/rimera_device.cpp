#include <numeric>
#include "rimera_device.hpp"
#include "rimera_helpers.hpp"
#include "rimera_vulkan.hpp"

RM::Result<void> RM::Device::Initialize(const DeviceCreateInfo& info)
{
    if (!info.resource_descriptor_count || !info.sampler_descriptor_count)
        return std::unexpected("Invalid device creation parameters");

    if (const auto result = volkInitialize(); result != VK_SUCCESS)
        return std::unexpected("Failed to load the Vulkan loader, " +
                               Vulkan::ResultMessage(result));

    uint32_t extension_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> available_extensions(extension_count);
    vkEnumerateInstanceExtensionProperties(nullptr,
                                           &extension_count,
                                           available_extensions.data());

    std::vector<const char*> instance_extensions = {VK_KHR_SURFACE_EXTENSION_NAME};
    bool has_surface_extension = false;
    for (const char* name : Vulkan::GetSurfaceExtensions())
    {
        if (!Vulkan::HasExtension(available_extensions, name)) continue;
        instance_extensions.push_back(name);
        has_surface_extension = true;
    }
    if (!has_surface_extension)
        return std::unexpected("No Vulkan surface extension for this platform is available");
    m_swapchain_colorspace =
        Vulkan::HasExtension(available_extensions, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
    std::vector<std::string> missing;
    for (const char* name : instance_extensions)
    {
        if (!Vulkan::HasExtension(available_extensions, name)) missing.emplace_back(name);
    }
    if (!missing.empty())
        return std::unexpected("Missing Vulkan instance extensions: " + Vulkan::Join(missing));
    if (m_swapchain_colorspace)
        instance_extensions.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);

    std::vector<const char*> layers;
    bool debug_utils = false;
#ifdef RIMERA_DEBUG
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
    constexpr auto kValidationLayer = "VK_LAYER_KHRONOS_validation";
    if (std::ranges::any_of(available_layers,
                            [&](const VkLayerProperties& layer)
                            {
                                return std::strcmp(layer.layerName, kValidationLayer) == 0;
                            }))
        layers.push_back(kValidationLayer);
    else
        std::fprintf(stderr, "[Rimera] Warning: Vulkan validation layer is not installed\n");
    if (Vulkan::HasExtension(available_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
    {
        instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        debug_utils = true;
    }
#endif

    constexpr VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Rimera",
        .pEngineName = "Rimera",
        .apiVersion = VK_API_VERSION_1_4,
    };
    const VkInstanceCreateInfo instance_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size()),
        .ppEnabledExtensionNames = instance_extensions.data(),
    };
    if (const auto result = vkCreateInstance(&instance_info, nullptr, &m_instance);
        result != VK_SUCCESS)
        return std::unexpected("Failed to create Vulkan instance, " +
                               Vulkan::ResultMessage(result));
    volkLoadInstance(m_instance);

    if (debug_utils)
    {
        const auto messenger_info = Vulkan::GetMessengerInfo();
        vkCreateDebugUtilsMessengerEXT(m_instance, &messenger_info, nullptr, &m_debug_messenger);
    }

    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(m_instance, &device_count, nullptr);
    if (!device_count) return std::unexpected("No Vulkan capable GPU found");
    std::vector<VkPhysicalDevice> physical_devices(device_count);
    vkEnumeratePhysicalDevices(m_instance, &device_count, physical_devices.data());

    std::vector<Vulkan::PhysicalDeviceCandidate> candidates;
    for (const auto physical_device : physical_devices)
        candidates.push_back(Vulkan::EvaluatePhysicalDevice(physical_device));
    std::ranges::stable_sort(candidates, std::greater{}, &Vulkan::PhysicalDeviceCandidate::score);

    const Vulkan::PhysicalDeviceCandidate* selected = nullptr;
    for (const auto& candidate : candidates)
    {
        if (!candidate.missing.empty()) continue;
        selected = &candidate;
        break;
    }
    if (!selected)
    {
        std::string error = "No GPU supports the required Vulkan features.";
        for (const auto& candidate : candidates)
            error += " " + candidate.name + " is missing: " + Vulkan::Join(candidate.missing) + ".";
        return std::unexpected(error);
    }
    m_physical_device = selected->device;
    m_unified_image_layouts = selected->unified_image_layouts;
    m_memory_priority = selected->memory_priority;
    m_hdr_metadata = selected->hdr_metadata;

    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(m_physical_device, &memory_properties);
    VkDeviceSize largest_local_heap = 0;
    for (uint32_t i = 0; i < memory_properties.memoryHeapCount; ++i)
    {
        const auto& memory_heap = memory_properties.memoryHeaps[i];
        if (memory_heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            largest_local_heap = std::max(largest_local_heap, memory_heap.size);
    }
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
    {
        const auto& memory_type = memory_properties.memoryTypes[i];
        constexpr VkMemoryPropertyFlags kLocalVisible =
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        if ((memory_type.propertyFlags & kLocalVisible) == kLocalVisible &&
            memory_properties.memoryHeaps[memory_type.heapIndex].size >= largest_local_heap / 4 * 3)
            m_gpu_upload_heap = true;
    }

    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &family_count, families.data());

    const auto find_family = [&](VkQueueFlags required, VkQueueFlags excluded)
    {
        for (uint32_t i = 0; i < family_count; ++i)
        {
            const auto flags = families[i].queueFlags;
            if ((flags & required) == required && !(flags & excluded)) return i;
        }
        return VK_QUEUE_FAMILY_IGNORED;
    };
    const uint32_t graphics_family = find_family(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0);
    if (graphics_family == VK_QUEUE_FAMILY_IGNORED)
        return std::unexpected("No Vulkan queue family supports graphics and compute");
    uint32_t compute_family = find_family(VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT);
    if (compute_family == VK_QUEUE_FAMILY_IGNORED) compute_family = graphics_family;
    uint32_t transfer_family =
        find_family(VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
    if (transfer_family == VK_QUEUE_FAMILY_IGNORED) transfer_family = graphics_family;

    const std::array family_indices = {graphics_family, compute_family, transfer_family};
    std::vector<uint32_t> unique_families;
    for (uint32_t type = 0; type < family_indices.size(); ++type)
    {
        const uint32_t family = family_indices[type];
        m_queue_families[type] = {.index = family,
                                  .count = std::min(families[family].queueCount,
                                                    Vulkan::kMaxQueuesPerFamily)};
        if (!std::ranges::contains(unique_families, family)) unique_families.push_back(family);
    }

    m_unique_queue_families = unique_families;

    constexpr std::array<float, Vulkan::kMaxQueuesPerFamily> priorities = {1.f, 1.f, 1.f, 1.f};
    std::vector<VkDeviceQueueCreateInfo> queue_infos;
    for (const uint32_t family : unique_families)
    {
        queue_infos.push_back({
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = family,
            .queueCount = std::min(families[family].queueCount, Vulkan::kMaxQueuesPerFamily),
            .pQueuePriorities = priorities.data(),
        });
    }

    Vulkan::FeatureChain supported;
    Vulkan::FeatureChain enabled;
    std::vector<const char*> device_extensions(Vulkan::kRequiredDeviceExtensions.begin(),
                                               Vulkan::kRequiredDeviceExtensions.end());
    if (m_unified_image_layouts)
        device_extensions.push_back(VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME);
    if (m_memory_priority) device_extensions.push_back(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);
    if (m_hdr_metadata) device_extensions.push_back(VK_EXT_HDR_METADATA_EXTENSION_NAME);
    supported.Link(m_unified_image_layouts, m_memory_priority);
    enabled.Link(m_unified_image_layouts, m_memory_priority);
    vkGetPhysicalDeviceFeatures2(m_physical_device, &supported.features);
    Vulkan::EnableFeatures(enabled, supported);

    const VkDeviceCreateInfo device_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabled.features,
        .queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size()),
        .pQueueCreateInfos = queue_infos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
    };
    if (const auto result = vkCreateDevice(m_physical_device, &device_info, nullptr, &m_device);
        result != VK_SUCCESS)
        return std::unexpected("Failed to create Vulkan device, " +
                               Vulkan::ResultMessage(result));
    volkLoadDevice(m_device);

    VmaVulkanFunctions functions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
    };
    VmaAllocatorCreateFlags allocator_flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT |
                                              VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    if (m_memory_priority) allocator_flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT;
    const VmaAllocatorCreateInfo allocator_info{
        .flags = allocator_flags,
        .physicalDevice = m_physical_device,
        .device = m_device,
        .pVulkanFunctions = &functions,
        .instance = m_instance,
        .vulkanApiVersion = VK_API_VERSION_1_4,
    };
    if (const auto result = vmaCreateAllocator(&allocator_info, &m_allocator);
        result != VK_SUCCESS)
        return std::unexpected("Failed to create memory allocator, " +
                               Vulkan::ResultMessage(result));

    auto core_residency_set_result = CreateResidencySet();
    if (!core_residency_set_result) return std::unexpected(core_residency_set_result.error());
    m_core_residency_set = std::move(*core_residency_set_result);
    auto frame_fence_result = CreateFence();
    if (!frame_fence_result) return std::unexpected(frame_fence_result.error());
    m_frame_fence = *frame_fence_result;

    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                           &m_heap_properties};
    m_heap_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;
    vkGetPhysicalDeviceProperties2(m_physical_device, &properties);
    if (m_heap_properties.maxPushDataSize < Command::kPushDataSize)
        return std::unexpected("The device push data size is too small");

    const uint64_t resource_stride = std::lcm(std::lcm(m_heap_properties.imageDescriptorSize,
                                                       m_heap_properties.bufferDescriptorSize),
                                              std::lcm(m_heap_properties.imageDescriptorAlignment,
                                                       m_heap_properties.
                                                       bufferDescriptorAlignment));
    const uint64_t sampler_stride = std::lcm(m_heap_properties.samplerDescriptorSize,
                                             m_heap_properties.samplerDescriptorAlignment);
    auto resource_heap = CreateDescriptorHeap(info.resource_descriptor_count,
                                              resource_stride,
                                              m_heap_properties.resourceHeapAlignment,
                                              m_heap_properties.minResourceHeapReservedRange);
    if (!resource_heap) return std::unexpected(resource_heap.error());
    m_resource_heap = std::move(*resource_heap);
    if (m_resource_heap.size > m_heap_properties.maxResourceHeapSize)
        return std::unexpected("Resource descriptor count exceeds the device heap size");
    auto sampler_heap = CreateDescriptorHeap(info.sampler_descriptor_count,
                                             sampler_stride,
                                             m_heap_properties.samplerHeapAlignment,
                                             m_heap_properties.minSamplerHeapReservedRange);
    if (!sampler_heap) return std::unexpected(sampler_heap.error());
    m_sampler_heap = std::move(*sampler_heap);
    if (m_sampler_heap.size > m_heap_properties.maxSamplerHeapSize)
        return std::unexpected("Sampler descriptor count exceeds the device heap size");

    if (auto result = CreateRootSignature(); !result) return std::unexpected(result.error());

    for (const auto format : {Format::RGBA16_Float, Format::BGRA8_UNorm})
    {
        auto blit_result = Helpers::CreateBlitShader(this, format);
        if (!blit_result) return std::unexpected(blit_result.error());
        m_blit_pipelines[format] = *blit_result;
    }

    return {};
}

RM::Device::~Device()
{
    if (m_frame_fence) static_cast<void>(m_frame_fence->Wait());
    FlushRetired(nullptr);
    if (m_upload_command) DestroyImmediate(m_upload_command);
    if (m_upload_queue) DestroyImmediate(m_upload_queue);
    if (m_upload_fence) DestroyImmediate(m_upload_fence);
    if (m_frame_fence) DestroyImmediate(m_frame_fence);
    for (auto [format, pipeline] : m_blit_pipelines) DestroyImmediate(pipeline);
    if (m_core_residency_set) DestroyImmediate(m_core_residency_set);
#ifdef RIMERA_DEBUG
    ValidateLiveObjects();
#endif

    DestroyDescriptorHeap(m_resource_heap);
    DestroyDescriptorHeap(m_sampler_heap);
    if (m_allocator) vmaDestroyAllocator(m_allocator);
    if (m_device) vkDestroyDevice(m_device, nullptr);
    if (m_debug_messenger) vkDestroyDebugUtilsMessengerEXT(m_instance, m_debug_messenger, nullptr);
    if (m_instance) vkDestroyInstance(m_instance, nullptr);
}

void* RM::Device::GetNativeHandle() const
{
    return m_device;
}

RM::Result<RM::ResourceAllocationInfo> RM::Device::GetAllocationInfo(
    const TextureCreateInfo& info,
    MemoryType) const
{
    if (auto result = Texture::ValidateCreateInfo(info); !result)
        return std::unexpected(result.error());
    if (Vulkan::GetFormat(info.format) == VK_FORMAT_UNDEFINED)
        return std::unexpected("Texture format is not supported by Vulkan");
    const auto image_info = Vulkan::GetImageCreateInfo(info,
                                                       m_unique_queue_families,
                                                       SupportsHostImageTransfer(info.format));
    const VkDeviceImageMemoryRequirements requirements_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS,
        .pCreateInfo = &image_info,
        .planeAspect = VK_IMAGE_ASPECT_COLOR_BIT,
    };
    VkMemoryRequirements2 requirements{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    vkGetDeviceImageMemoryRequirements(m_device, &requirements_info, &requirements);
    return ResourceAllocationInfo{.size = requirements.memoryRequirements.size,
                                  .alignment = requirements.memoryRequirements.alignment};
}

RM::Result<RM::ResourceAllocationInfo> RM::Device::GetAllocationInfo(
    const BufferCreateInfo& info,
    MemoryType) const
{
    if (!info.size) return std::unexpected("Invalid buffer creation parameters");
    const auto buffer_info = Vulkan::GetBufferCreateInfo(info, m_unique_queue_families);
    const VkDeviceBufferMemoryRequirements requirements_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS,
        .pCreateInfo = &buffer_info,
    };
    VkMemoryRequirements2 requirements{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    vkGetDeviceBufferMemoryRequirements(m_device, &requirements_info, &requirements);
    return ResourceAllocationInfo{.size = requirements.memoryRequirements.size,
                                  .alignment = requirements.memoryRequirements.alignment};
}

RM::TextureCopyFootprint RM::Device::GetCopyFootprint(const Texture* texture,
                                                      uint32_t mip_level) const
{
    const auto block = Vulkan::GetFormatBlock(texture->GetFormat());
    if (!block.bytes) return {};
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(m_physical_device, &properties);
    const uint64_t pitch_alignment =
        std::max<uint64_t>({1, properties.limits.optimalBufferCopyRowPitchAlignment, block.bytes});

    const auto dimensions = texture->GetDimensions();
    const uint32_t width = std::max(1u, dimensions.x >> mip_level);
    const uint32_t height = std::max(1u, dimensions.y >> mip_level);
    const uint32_t row_size = (width + block.width - 1) / block.width * block.bytes;
    const uint32_t row_count = (height + block.height - 1) / block.height;
    const uint32_t bytes_per_row =
        static_cast<uint32_t>((row_size + pitch_alignment - 1) / pitch_alignment * pitch_alignment);
    return {.bytes_per_row = bytes_per_row,
            .row_size = row_size,
            .row_count = row_count,
            .size = uint64_t{bytes_per_row} * (row_count - 1) + row_size};
}

RM::Result<void> RM::Device::TransitionImageLayout(VkImage image,
                                                   VkImageAspectFlags aspect,
                                                   uint32_t mip_levels,
                                                   uint32_t layer_count,
                                                   VkImageLayout layout)
{
    if (auto result = EnsureUploadObjects(); !result) return std::unexpected(result.error());

    m_upload_command->Begin();
    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_NONE,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {aspect, 0, mip_levels, 0, layer_count},
    };
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(m_upload_command->m_command_buffer, &dependency);
    m_upload_command->End();
    return SubmitUpload();
}

bool RM::Device::SupportsHostImageTransfer(Format format) const
{
    VkFormatProperties3 format_properties3{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
    VkFormatProperties2 format_properties{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
                                          &format_properties3};
    vkGetPhysicalDeviceFormatProperties2(m_physical_device,
                                         Vulkan::GetFormat(format),
                                         &format_properties);
    return format_properties3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT;
}

void* RM::Device::GetNativeRootSignature() const
{
    return const_cast<VkShaderDescriptorSetAndBindingMappingInfoEXT*>(&m_mapping_info);
}

RM::Result<void> RM::Device::CreateRootSignature()
{
    constexpr uint32_t kRootConstantBytes = Command::kRootConstantCount * sizeof(uint32_t);
    for (uint32_t slot = 0; slot < Command::kConstantBufferSlotCount; ++slot)
    {
        m_mappings[slot] = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT,
            .descriptorSet = 0,
            .firstBinding = slot,
            .bindingCount = 1,
            .resourceMask = VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
            .source = VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_ADDRESS_EXT,
            .sourceData = {
                .pushAddressOffset = kRootConstantBytes + slot * static_cast<uint32_t>(sizeof(
                                         uint64_t))},
        };
    }
    m_mappings[3] = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT,
        .descriptorSet = 0,
        .firstBinding = Command::kRootConstantSlot,
        .bindingCount = 1,
        .resourceMask = VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        .source = VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_DATA_EXT,
        .sourceData = {.pushDataOffset = 0},
    };
    m_mappings[4] = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT,
        .descriptorSet = 1,
        .firstBinding = 0,
        .bindingCount = 1,
        .resourceMask = VK_SPIRV_RESOURCE_TYPE_ALL_EXT,
        .source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT,
        .sourceData = {.constantOffset = {.heapOffset = 0,
                                          .heapArrayStride = static_cast<uint32_t>(
                                              m_resource_heap.stride)}},
    };
    m_mappings[5] = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT,
        .descriptorSet = 2,
        .firstBinding = 0,
        .bindingCount = 1,
        .resourceMask = VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT,
        .source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT,
        .sourceData = {.constantOffset = {.heapOffset = 0,
                                          .heapArrayStride = static_cast<uint32_t>(
                                              m_sampler_heap.stride),
                                          .samplerHeapOffset = 0,
                                          .samplerHeapArrayStride = static_cast<uint32_t>(
                                              m_sampler_heap.stride)}},
    };
    m_mapping_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT,
        .mappingCount = static_cast<uint32_t>(m_mappings.size()),
        .pMappings = m_mappings.data(),
    };
    return {};
}

RM::Result<RM::Device::DescriptorHeap> RM::Device::CreateDescriptorHeap(uint32_t count,
    uint64_t stride,
    uint64_t alignment,
    uint64_t reserved_size)
{
    DescriptorHeap heap;
    heap.stride = stride;
    heap.count = count;
    heap.reserved_offset = (uint64_t{count} * stride + alignment - 1) / alignment * alignment;
    heap.reserved_size = reserved_size;
    heap.size = heap.reserved_offset + reserved_size;

    const bool concurrent = m_unique_queue_families.size() > 1;
    const VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = heap.size,
        .usage = VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = concurrent ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = concurrent
                                     ? static_cast<uint32_t>(m_unique_queue_families.size())
                                     : 0,
        .pQueueFamilyIndices = concurrent ? m_unique_queue_families.data() : nullptr,
    };
    constexpr VmaAllocationCreateInfo allocation_info{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        .priority = 1.f,
    };
    VmaAllocationInfo result_info{};
    if (const auto result = vmaCreateBufferWithAlignment(m_allocator,
                                                         &buffer_info,
                                                         &allocation_info,
                                                         alignment,
                                                         &heap.buffer,
                                                         &heap.allocation,
                                                         &result_info);
        result != VK_SUCCESS)
        return std::unexpected("Failed to create descriptor heap, " +
                               Vulkan::ResultMessage(result));
    heap.data = static_cast<uint8_t*>(result_info.pMappedData);
    const VkBufferDeviceAddressInfo address_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = heap.buffer,
    };
    heap.address = vkGetBufferDeviceAddress(m_device, &address_info);
    return heap;
}

void RM::Device::DestroyDescriptorHeap(DescriptorHeap& heap)
{
    if (heap.buffer) vmaDestroyBuffer(m_allocator, heap.buffer, heap.allocation);
    heap = {};
}

RM::Result<uint32_t> RM::Device::AllocateDescriptor(DescriptorHeap& heap)
{
    if (!heap.free_descriptors.empty())
    {
        const uint32_t index = heap.free_descriptors.back();
        heap.free_descriptors.pop_back();
        return index;
    }
    if (heap.index >= heap.count) return std::unexpected("Descriptor heap is full");
    return heap.index++;
}

void RM::Device::FlushDescriptor(const DescriptorHeap& heap, uint32_t index) const
{
    vmaFlushAllocation(m_allocator, heap.allocation, uint64_t{index} * heap.stride, heap.stride);
}

RM::Result<uint32_t> RM::Device::AddDescriptor(const Buffer* buffer)
{
    auto index = AllocateDescriptor(m_resource_heap);
    if (!index) return std::unexpected(index.error());
    const VkDeviceAddressRangeEXT range{.address = buffer->GetGPUAddress(),
                                        .size = buffer->GetLength()};
    const VkResourceDescriptorInfoEXT descriptor{
        .sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT,
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .data = {.pAddressRange = &range},
    };
    const VkHostAddressRangeEXT destination{
        .address = m_resource_heap.data + uint64_t{*index} * m_resource_heap.stride,
        .size = m_resource_heap.stride,
    };
    if (const auto result = vkWriteResourceDescriptorsEXT(m_device, 1, &descriptor, &destination);
        result != VK_SUCCESS)
        return std::unexpected("Failed to write buffer descriptor, " +
                               Vulkan::ResultMessage(result));
    FlushDescriptor(m_resource_heap, *index);
    return *index;
}

RM::Result<uint32_t> RM::Device::AddDescriptor(const Texture* texture, TextureViewType type)
{
    auto index = AllocateDescriptor(m_resource_heap);
    if (!index) return std::unexpected(index.error());
    const auto view_info = texture->GetViewCreateInfo(type);
    const VkImageDescriptorInfoEXT image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT,
        .pView = &view_info,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkResourceDescriptorInfoEXT descriptor{
        .sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT,
        .type = type == TextureViewType::ShaderReadWrite
                    ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                    : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .data = {.pImage = &image_info},
    };
    const VkHostAddressRangeEXT destination{
        .address = m_resource_heap.data + uint64_t{*index} * m_resource_heap.stride,
        .size = m_resource_heap.stride,
    };
    if (const auto result = vkWriteResourceDescriptorsEXT(m_device, 1, &descriptor, &destination);
        result != VK_SUCCESS)
        return std::unexpected("Failed to write texture descriptor, " +
                               Vulkan::ResultMessage(result));
    FlushDescriptor(m_resource_heap, *index);
    return *index;
}

RM::Result<uint32_t> RM::Device::AddDescriptor(Sampler*)
{
    return AllocateDescriptor(m_sampler_heap);
}

void RM::Device::WriteSamplerDescriptor(uint32_t index, const VkSamplerCreateInfo& info)
{
    const VkHostAddressRangeEXT destination{
        .address = m_sampler_heap.data + uint64_t{index} * m_sampler_heap.stride,
        .size = m_sampler_heap.stride,
    };
    vkWriteSamplerDescriptorsEXT(m_device, 1, &info, &destination);
    FlushDescriptor(m_sampler_heap, index);
}

void RM::Device::RemoveDescriptor(const Buffer* buffer)
{
    if (buffer->m_resource_index != UINT32_MAX)
        m_resource_heap.free_descriptors.push_back(buffer->m_resource_index);
}

void RM::Device::RemoveDescriptor(const Texture* texture)
{
    for (const auto index : texture->m_descriptors)
    {
        if (index != UINT32_MAX) m_resource_heap.free_descriptors.push_back(index);
    }
}

void RM::Device::RemoveDescriptor(const Sampler* sampler)
{
    if (sampler->m_index != UINT32_MAX) m_sampler_heap.free_descriptors.push_back(sampler->m_index);
}