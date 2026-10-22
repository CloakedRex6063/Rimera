#pragma once
#include "unordered_map"
#include "vector"
#include "string_view"
#include "rimera_result.hpp"

namespace RM
{
    class Buffer;
    class Texture;
    class Heap;
    class Device;
    class Queue;
    class ResidencySet;

    class Resident
    {
    public:
        Resident(const Resident&) = delete;
        Resident& operator=(const Resident&) = delete;

    protected:
        Resident() = default;
        ~Resident() = default;
        void ReleaseResidency();
        Device* m_device = nullptr;

    private:
        friend class ResidencySet;
        friend class Device;
        std::vector<ResidencySet*> m_residency_sets;
#ifdef RIMERA_DEBUG
        struct QueueUse
        {
            uint64_t queue = 0;
            uint64_t value = 0;
        };

        void TrackQueue(const Queue* queue);
        void ValidateIdle(std::string_view type) const;
        std::vector<QueueUse> m_queue_uses;
#endif
    };

    class ResidencySet
    {
    public:
        ResidencySet(const ResidencySet&) = delete;
        ResidencySet& operator=(const ResidencySet&) = delete;
        ~ResidencySet();

        void AddAllocation(Texture* resource);
        void RemoveAllocation(Texture* resource);
        void AddAllocation(Buffer* resource);
        void RemoveAllocation(Buffer* resource);
        void AddAllocation(Heap* heap);
        void RemoveAllocation(Heap* heap);

        void RemoveAllAllocations();
        void Commit() const;

        void RequestResidency() const;
        void EndResidency() const;

        [[nodiscard]] void* GetNative() const;

    private:
        ResidencySet() = default;
        void AddResident(Resident* resident, void* native);
        void RemoveResident(Resident* resident);
        void AddNativeAllocation(void* native);
        void RemoveNativeAllocation(void* native);
        void RemoveAllNativeAllocations();
        std::unordered_map<Resident*, void*> m_residents;
#ifdef RIMERA_DEBUG
        void TrackQueue(const Queue* queue);
        void UntrackQueue(const Queue* queue);
        std::vector<uint64_t> m_queues;
#endif
#ifdef RIMERA_MAC
        void AddAllocation(const MTL::Allocation* allocation) const;
        void RemoveAllocation(const MTL::Allocation* allocation) const;
        NS::SharedPtr<MTL::ResidencySet> m_residency_set;
#elifdef RIMERA_WINDOWS
        std::vector<ID3D12Pageable*> m_resources;
#endif
        friend class Device;
        friend class Resident;
        friend class Queue;
        [[nodiscard]] Result<void> Initialize(Device* device);
        Device* m_device = nullptr;
    };
} // namespace RM
