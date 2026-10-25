#pragma once
#include "unordered_map"
#include "rimera_structs.hpp"
#include "rimera_residency.hpp"

namespace RM
{
    class Device;
    class Buffer;
    class Texture;

    class Heap : public Resident
    {
    public:
        Heap(const Heap&) = delete;
        Heap& operator=(const Heap&) = delete;
        ~Heap();

        [[nodiscard]] Result<Texture*> CreateTexture(const TextureCreateInfo& info);
        [[nodiscard]] Result<Buffer*> CreateBuffer(const BufferCreateInfo& info);

        [[nodiscard]] Result<Texture*> CreatePlacedTexture(const TextureCreateInfo& info,
                                                           uint64_t offset);
        [[nodiscard]] Result<Buffer*> CreatePlacedBuffer(const BufferCreateInfo& info,
                                                         uint64_t offset);

        [[nodiscard]] Result<ResourceAllocationInfo> GetAllocationInfo(
            const TextureCreateInfo& info) const;
        [[nodiscard]] Result<ResourceAllocationInfo> GetAllocationInfo(
            const BufferCreateInfo& info) const;

        [[nodiscard]] uint64_t GetSize() const noexcept { return m_size; }
        [[nodiscard]] uint64_t GetUsedSize() const noexcept { return m_used; }
        [[nodiscard]] HeapMemory GetMemory() const noexcept { return m_memory; }
        [[nodiscard]] void* GetNativeHandle() const;

    private:
        Heap() = default;
        friend class Device;
        friend class Texture;
        friend class Buffer;

        struct Block
        {
            uint64_t offset = 0;
            uint64_t size = 0;
        };

        [[nodiscard]] Result<void> Initialize(Device* device, const HeapCreateInfo& info);
        [[nodiscard]] Result<void> ValidatePlacement(const ResourceAllocationInfo& allocation,
                                                     uint64_t offset) const;
        [[nodiscard]] Result<uint64_t> Allocate(const ResourceAllocationInfo& allocation);
        void Free(uint64_t offset);

        uint64_t m_size = 0;
        uint64_t m_used = 0;
        HeapMemory m_memory = HeapMemory::DeviceLocal;
        std::vector<Block> m_free_blocks;
        std::unordered_map<uint64_t, Block> m_allocations;
#ifdef RIMERA_METAL
        NS::SharedPtr<MTL::Heap> m_heap;
#elifdef RIMERA_D3D12
        ComPtr<ID3D12Heap> m_heap;
#endif
    };
} // namespace RM
