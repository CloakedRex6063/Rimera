#include "rimera_buffer.hpp"
#include "rimera_device.hpp"

RM::Result<void> RM::Buffer::WriteInitialData(const BufferCreateInfo& info)
{
    if (!info.data) return {};
    if (!IsHostVisible()) return m_device->UploadBuffer(this, info.data);
    auto mapped = Map();
    if (!mapped) return std::unexpected(mapped.error());
    std::memcpy(*mapped, info.data, info.size);
    Unmap();
    return {};
}
