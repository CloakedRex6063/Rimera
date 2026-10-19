#include "rimera_heap.hpp"
#include "rimera_buffer.hpp"
#include "rimera_device.hpp"
#include "rimera_texture.hpp"

namespace
{
    constexpr uint64_t AlignUp(uint64_t value, uint64_t alignment)
    {
        return alignment > 1 ? (value + alignment - 1) / alignment * alignment : value;
    }
}

RM::Result<RM::Heap*> RM::Device::CreateHeap(const HeapCreateInfo& info)
{
    return CreateObject<Heap>(this, info);
}

void RM::Device::DestroyHeap(const Heap* heap) const
{
    delete heap;
}

RM::Result<RM::Texture*> RM::Heap::CreateTexture(const TextureCreateInfo& info)
{
    auto allocation = GetAllocationInfo(info);
    if (!allocation) return std::unexpected(allocation.error());
    auto offset = Allocate(*allocation);
    if (!offset) return std::unexpected(offset.error());
    auto texture = Device::CreateObject<Texture>(m_device, this, *offset, info);
    if (!texture)
    {
        Free(*offset);
        return std::unexpected(texture.error());
    }
    (*texture)->m_heap_allocated = true;
    return *texture;
}

RM::Result<RM::Buffer*> RM::Heap::CreateBuffer(const BufferCreateInfo& info)
{
    auto allocation = GetAllocationInfo(info);
    if (!allocation) return std::unexpected(allocation.error());
    auto offset = Allocate(*allocation);
    if (!offset) return std::unexpected(offset.error());
    auto buffer = Device::CreateObject<Buffer>(m_device, this, *offset, info);
    if (!buffer)
    {
        Free(*offset);
        return std::unexpected(buffer.error());
    }
    (*buffer)->m_heap_allocated = true;
    return *buffer;
}

RM::Result<RM::Texture*> RM::Heap::CreatePlacedTexture(const TextureCreateInfo& info,
                                                       uint64_t offset)
{
    auto allocation = GetAllocationInfo(info);
    if (!allocation) return std::unexpected(allocation.error());
    if (auto result = ValidatePlacement(*allocation, offset); !result)
        return std::unexpected(result.error());
    return Device::CreateObject<Texture>(m_device, this, offset, info);
}

RM::Result<RM::Buffer*> RM::Heap::CreatePlacedBuffer(const BufferCreateInfo& info, uint64_t offset)
{
    auto allocation = GetAllocationInfo(info);
    if (!allocation) return std::unexpected(allocation.error());
    if (auto result = ValidatePlacement(*allocation, offset); !result)
        return std::unexpected(result.error());
    return Device::CreateObject<Buffer>(m_device, this, offset, info);
}

RM::Result<RM::ResourceAllocationInfo> RM::Heap::GetAllocationInfo(
    const TextureCreateInfo& info) const
{
    return m_device->GetAllocationInfo(info, m_memory);
}

RM::Result<RM::ResourceAllocationInfo> RM::Heap::GetAllocationInfo(
    const BufferCreateInfo& info) const
{
    return m_device->GetAllocationInfo(info, m_memory);
}

RM::Result<void> RM::Heap::ValidatePlacement(const ResourceAllocationInfo& allocation,
                                             uint64_t offset) const
{
    if (allocation.alignment > 1 && offset % allocation.alignment != 0)
        return std::unexpected("Placed resource offset does not satisfy the resource alignment");
    if (offset > m_size || allocation.size > m_size - offset)
        return std::unexpected("Placed resource does not fit inside the heap");
    return {};
}

RM::Result<uint64_t> RM::Heap::Allocate(const ResourceAllocationInfo& allocation)
{
    if (!allocation.size) return std::unexpected("Cannot allocate an empty range from a heap");
    for (auto it = m_free_blocks.begin(); it != m_free_blocks.end(); ++it)
    {
        const uint64_t aligned = AlignUp(it->offset, allocation.alignment);
        const uint64_t padding = aligned - it->offset;
        if (padding > it->size || allocation.size > it->size - padding) continue;

        const Block used{.offset = it->offset, .size = padding + allocation.size};
        it->offset += used.size;
        it->size -= used.size;
        if (!it->size) m_free_blocks.erase(it);
        m_allocations.emplace(aligned, used);
        m_used += used.size;
        return aligned;
    }
    return std::unexpected("Heap does not have enough free memory for the allocation");
}

void RM::Heap::Free(uint64_t offset)
{
    const auto allocation = m_allocations.find(offset);
    if (allocation == m_allocations.end()) return;
    const Block block = allocation->second;
    m_allocations.erase(allocation);
    m_used -= block.size;

    auto it = std::ranges::lower_bound(m_free_blocks, block.offset, {}, &Block::offset);
    it = m_free_blocks.insert(it, block);
    if (auto next = std::next(it);
        next != m_free_blocks.end() && it->offset + it->size == next->offset)
    {
        it->size += next->size;
        m_free_blocks.erase(next);
    }
    if (it != m_free_blocks.begin())
    {
        auto previous = std::prev(it);
        if (previous->offset + previous->size == it->offset)
        {
            previous->size += it->size;
            m_free_blocks.erase(it);
        }
    }
}
