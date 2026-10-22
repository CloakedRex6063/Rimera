#pragma once
#include "rimera_structs.hpp"
#include "rimera_residency.hpp"

namespace RM
{
    class Device;
    class Heap;

    class Buffer : public Resident
    {
    public:
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        ~Buffer();
        void* GetNativeHandle() const noexcept;
        [[nodiscard]] Result<void*> Map();
        void Unmap();

        [[nodiscard]] uint32_t GetResourceIndex() const { return m_resource_index; }
        [[nodiscard]] uint64_t GetGPUAddress() const;
        [[nodiscard]] uint32_t GetLength() const { return m_length; }
        [[nodiscard]] Heap* GetHeap() const noexcept { return m_heap; }
        [[nodiscard]] uint64_t GetHeapOffset() const noexcept { return m_heap_offset; }

    private:
        Buffer() = default;
        [[nodiscard]] Result<void> Initialize(Device* device, const BufferCreateInfo& info);
        [[nodiscard]] Result<void> Initialize(Device* device,
                                              Heap* heap,
                                              uint64_t offset,
                                              const BufferCreateInfo& info);
        friend class Device;
        friend class Heap;
        Heap* m_heap = nullptr;
        uint64_t m_heap_offset = 0;
        bool m_heap_allocated = false;
        uint32_t m_resource_index = UINT32_MAX;
        uint32_t m_length = 0;
        EnumFlags<BufferFlags> m_flags;
        void* m_data = nullptr;
        bool m_mapped = false;
#ifdef RIMERA_MAC
        NS::SharedPtr<MTL::Buffer> m_buffer;
#elifdef RIMERA_WINDOWS
        ComPtr<ID3D12Resource> m_buffer;
        ComPtr<D3D12MA::Allocation> m_allocation = nullptr;
#endif
    };
} // namespace RM