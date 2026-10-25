#include "rimera_queue.hpp"

RM::Result<void> RM::Queue::WaitIdle()
{
    if (auto result = Submit(SubmitInfo{}); !result) return result;
    return m_fence->Wait();
}

void RM::Queue::Submit(const Command* command)
{
    static_cast<void>(Submit(SubmitInfo{.commands = std::span<const Command* const>(&command, 1)}));
}

bool RM::Queue::IsBusySince(uint64_t value) const
{
    const uint64_t signaled = m_fence->GetSignaledValue();
    return signaled > value && !m_fence->IsComplete(signaled);
}
