#pragma once
#include "rimera_structs.hpp"

namespace RM
{
    class Device;
    class Buffer
    {
    public:
        ~Buffer();

        [[nodiscard]] void* GetNativeHandle() const;
        [[nodiscard]] void* GetContents() const;
        [[nodiscard]] uint64_t GetGPUAddress() const;
        [[nodiscard]] uint32_t GetLength() const { return m_length; }

    private:
        Buffer(Device* device, const BufferCreateInfo& info);
        friend class Device;
        Device* m_device;
        uint32_t m_resource_index = UINT32_MAX;
        uint32_t m_length = 0;
#ifdef RIMERA_MAC
        NS::SharedPtr<MTL::Buffer> m_buffer;
#endif
    };
}  // namespace RM