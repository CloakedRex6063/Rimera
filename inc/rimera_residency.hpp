#pragma once

namespace RM
{
    class Buffer;
    class Texture;
    class Device;

    class ResidencySet
    {
    public:
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
#ifdef RIMERA_MAC
        void AddAllocation(const MTL::Resource* resource) const;
        void RemoveAllocation(const MTL::Resource* resource) const;
        NS::SharedPtr<MTL::ResidencySet> m_residency_set;
#elifdef RIMERA_WINDOWS
        std::vector<ID3D12Pageable*> m_resources;
#endif
        friend class Device;
        ResidencySet(Device* device);
        Device* m_device;
    };
} // namespace RM