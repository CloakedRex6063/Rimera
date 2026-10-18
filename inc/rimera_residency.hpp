#pragma once
#include "rimera_result.hpp"

namespace RM
{
    class Buffer;
    class Texture;
    class Device;

    class ResidencySet
    {
    public:
        ResidencySet(const ResidencySet&) = delete;
        ResidencySet& operator=(const ResidencySet&) = delete;
        ~ResidencySet();

        void AddAllocation(Texture* resource);
        void RemoveAllocation(const Texture* resource);
        void AddAllocation(Buffer* resource);
        void RemoveAllocation(const Buffer* resource);

        void RemoveAllAllocations();
        void Commit() const;

        void RequestResidency() const;
        void EndResidency() const;

        [[nodiscard]] void* GetNative() const;

    private:
        ResidencySet() = default;
#ifdef RIMERA_MAC
        void AddAllocation(const MTL::Resource* resource) const;
        void RemoveAllocation(const MTL::Resource* resource) const;
        NS::SharedPtr<MTL::ResidencySet> m_residency_set;
#elifdef RIMERA_WINDOWS
        std::vector<ID3D12Pageable*> m_resources;
#endif
        friend class Device;
        [[nodiscard]] Result<void> Initialize(Device* device);
        Device* m_device = nullptr;
    };
} // namespace RM