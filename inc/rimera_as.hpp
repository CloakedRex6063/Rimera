#pragma once
#include "rimera_structs.hpp"

namespace RM
{
    class Device;
    class BLAS
    {
    public:
        BLAS(const BLAS&) = delete;
        BLAS& operator=(const BLAS&) = delete;
        [[nodiscard]] void* GetNativeHandle() const;
        
    private:
        BLAS() = default;
        friend class Device;
        [[nodiscard]] Result<void> Initialize(Device* device, const BLASCreateInfo& info);
#ifdef RIMERA_METAL
        NS::SharedPtr<MTL::AccelerationStructure> m_blas;
#endif
    };
    
    class TLAS
    {
    public:
        TLAS(const TLAS&) = delete;
        TLAS& operator=(const TLAS&) = delete;
        [[nodiscard]] void* GetNativeHandle() const;
        
    private:
        TLAS() = default;
        friend class Device;
        [[nodiscard]] Result<void> Initialize(Device* device, const TLASCreateInfo& info);
#ifdef RIMERA_METAL
        NS::SharedPtr<MTL::Buffer> m_data_buffer;
        uint32_t scratch_offset = 0;
        NS::SharedPtr<MTL::AccelerationStructure> m_tlas;
#endif
    };
}