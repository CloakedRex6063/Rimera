#include "rimera_queue.hpp"

RM::Result<uint64_t> RM::Queue::Signal(Fence* fence) const
{
    const uint64_t value = fence->m_value + 1;
    if (auto result = Signal(fence, value); !result) return std::unexpected(result.error());
    return value;
}

RM::Result<void> RM::Queue::WaitIdle() const
{
    auto value = Signal(m_fence);
    if (!value) return std::unexpected(value.error());
    return m_fence->Wait(*value);
}

bool RM::Queue::IsBusySince(uint64_t value) const
{
    const uint64_t signaled = m_fence->GetSignaledValue();
    return signaled > value && !m_fence->IsComplete(signaled);
}
