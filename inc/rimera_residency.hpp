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
        void Commit();

        void RequestResidency();
        void EndResidency();

        [[nodiscard]] void* GetNative() const;

    private:
        ResidencySet() = default;
        void AddResident(Resident* resident, void* native);
        void RemoveResident(Resident* resident);
        void AddNativeAllocation(void* native);
        void RemoveNativeAllocation(void* native);
        void RemoveAllNativeAllocations();
        void CommitNative() const;
        void RequestNativeResidency() const;
        void EndNativeResidency() const;
        void TrackQueue(Queue* queue);
        void UntrackQueue(Queue* queue);
        std::unordered_map<Resident*, void*> m_residents;
        std::vector<Queue*> m_queues;
        bool m_resident = false;
        bool m_dirty = false;
#ifdef RIMERA_DEBUG
        void ValidateIdle() const;
#endif
#ifdef RIMERA_METAL
        void AddAllocation(const MTL::Allocation* allocation);
        void RemoveAllocation(const MTL::Allocation* allocation);
        NS::SharedPtr<MTL::ResidencySet> m_residency_set;
#elifdef RIMERA_D3D12
        std::vector<ID3D12Pageable*> m_resources;
#endif
        friend class Device;
        friend class Resident;
        friend class Queue;
        [[nodiscard]] Result<void> Initialize(Device* device);
        Device* m_device = nullptr;
    };
} // namespace RM
