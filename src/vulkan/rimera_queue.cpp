#include "rimera_queue.hpp"
#include "rimera_device.hpp"
#include "rimera_vulkan.hpp"

RM::Fence::~Fence()
{
    if (m_semaphore)
        vkDestroySemaphore(static_cast<VkDevice>(m_device->GetNativeHandle()),
                           m_semaphore,
                           nullptr);
}

void* RM::Fence::GetNativeHandle() const
{
    return Vulkan::ToNative(m_semaphore);
}

uint64_t RM::Fence::GetCompletedValue() const
{
    uint64_t value = 0;
    vkGetSemaphoreCounterValue(static_cast<VkDevice>(m_device->GetNativeHandle()),
                               m_semaphore,
                               &value);
    return value;
}

RM::Result<void> RM::Fence::Wait(uint64_t value) const
{
    if (value <= GetCompletedValue()) return {};
    const VkSemaphoreWaitInfo wait_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &m_semaphore,
        .pValues = &value,
    };
    if (vkWaitSemaphores(static_cast<VkDevice>(m_device->GetNativeHandle()),
                         &wait_info,
                         UINT64_MAX) != VK_SUCCESS)
        return std::unexpected("Failed to wait for fence");
    return {};
}

RM::Result<void> RM::Fence::Initialize(Device* device)
{
    m_device = device;
    const VkSemaphoreTypeCreateInfo type_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 0,
    };
    const VkSemaphoreCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &type_info,
    };
    if (const auto result = vkCreateSemaphore(static_cast<VkDevice>(device->GetNativeHandle()),
                                              &create_info,
                                              nullptr,
                                              &m_semaphore);
        result != VK_SUCCESS)
        return std::unexpected("Failed to create fence, " + Vulkan::ResultMessage(result));
    return {};
}

RM::Queue::~Queue()
{
#ifdef RIMERA_DEBUG
    if (m_device) m_device->m_queues.erase(m_id);
#endif
    while (!m_residency_sets.empty()) RemoveResidencySet(m_residency_sets.back());
    if (m_fence) m_device->DestroyImmediate(m_fence);
}

void RM::Queue::AddResidencySet(ResidencySet* residency_set)
{
    residency_set->TrackQueue(this);
}

void RM::Queue::RemoveResidencySet(ResidencySet* residency_set)
{
    residency_set->UntrackQueue(this);
}

RM::Result<void> RM::Queue::Submit(const SubmitInfo& info)
{
    for (auto* residency_set : m_residency_sets) residency_set->Commit();

    std::vector<VkCommandBufferSubmitInfo> commands;
    commands.reserve(info.commands.size());
    for (const auto* command : info.commands)
    {
        commands.push_back({.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                            .commandBuffer = static_cast<VkCommandBuffer>(command->GetNativeCommand())});
    }

    std::vector<VkSemaphoreSubmitInfo> waits;
    for (const auto& wait : info.waits)
    {
        waits.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                         .semaphore = wait.fence->m_semaphore,
                         .value = wait.value,
                         .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT});
    }
    std::vector<VkSemaphoreSubmitInfo> signals;
    for (const auto& signal : info.signals)
    {
        signals.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                           .semaphore = signal.fence->m_semaphore,
                           .value = signal.value,
                           .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT});
    }
    for (auto* swapchain : info.swapchains)
    {
        if (auto result = swapchain->AddSubmitSync(waits, signals); !result) return result;
    }

    const uint64_t queue_value = m_fence->m_value + 1;
    signals.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                       .semaphore = m_fence->m_semaphore,
                       .value = queue_value,
                       .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT});

    const VkSubmitInfo2 submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size()),
        .pWaitSemaphoreInfos = waits.data(),
        .commandBufferInfoCount = static_cast<uint32_t>(commands.size()),
        .pCommandBufferInfos = commands.data(),
        .signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size()),
        .pSignalSemaphoreInfos = signals.data(),
    };
    if (const auto result = vkQueueSubmit2(m_queue, 1, &submit_info, VK_NULL_HANDLE);
        result != VK_SUCCESS)
        return std::unexpected("Failed to submit to queue, " + Vulkan::ResultMessage(result));

    for (const auto& signal : info.signals)
        signal.fence->m_value = std::max(signal.fence->m_value, signal.value);
    m_fence->m_value = queue_value;
    return {};
}

void* RM::Queue::GetNativeHandle() const
{
    return m_queue;
}

RM::Result<void> RM::Queue::Initialize(Device* device, QueueType type)
{
    m_device = device;
    auto& family = device->m_queue_families[Vulkan::GetQueueIndex(type)];
    vkGetDeviceQueue(device->m_device, family.index, family.next++ % family.count, &m_queue);
    m_family = family.index;
    if (!m_queue) return std::unexpected("Failed to get device queue");
    auto fence = m_device->CreateFence();
    if (!fence) return std::unexpected(fence.error());
    m_fence = *fence;
    return {};
}
