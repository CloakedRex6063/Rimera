#pragma once
#include "rimera_structs.hpp"

namespace RM
{
    class Device;

    class Buffer
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

    private:
        Buffer() = default;
        [[nodiscard]] Result<void> Initialize(Device* device, const BufferCreateInfo& info);
        friend class Device;
        Device* m_device = nullptr;
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