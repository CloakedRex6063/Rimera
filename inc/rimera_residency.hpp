#pragma once

namespace RM
{
    class Buffer;
    class Texture;
    class ResidencySet
    {
    public:
        ResidencySet(const NS::SharedPtr<MTL::Device>& device);
        ~ResidencySet();

        void AddAllocation(const Texture* resource);
        void RemoveAllocation(const Texture* resource);
        void AddAllocation(const Buffer* resource);
        void RemoveAllocation(const Buffer* resource);
        void Commit() const;

        void RequestResidency() const;
        void EndResidency() const;

        [[nodiscard]] void* GetNative() const;

    private:
#ifdef RIMERA_MAC
        friend class Device;
        void AddAllocation(const MTL::Resource* resource) const;
        void RemoveAllocation(const MTL::Resource* resource) const;
        NS::SharedPtr<MTL::ResidencySet> m_residency_set;
#endif
    };
}  // namespace RM
