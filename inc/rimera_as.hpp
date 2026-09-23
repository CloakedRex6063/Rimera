#pragma once
#include "rimera_structs.hpp"

namespace RM
{
    class Device;
    class BLAS
    {
    public:
        [[nodiscard]] void* GetNativeHandle() const;
        
    private:
        friend class Device;
        BLAS(Device* device, const BLASCreateInfo& info);
#ifdef RIMERA_MAC
        NS::SharedPtr<MTL::AccelerationStructure> m_blas;
#endif
    };
    
    class TLAS
    {
    public:
        [[nodiscard]] void* GetNativeHandle() const;
        
    private:
        friend class Device;
        TLAS(Device* device, const TLASCreateInfo& info);
#ifdef RIMERA_MAC
        NS::SharedPtr<MTL::Buffer> m_data_buffer;
        uint32_t scratch_offset = 0;
        NS::SharedPtr<MTL::AccelerationStructure> m_tlas;
#endif
    };
}