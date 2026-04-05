#pragma once

#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitMemory/SlabAllocator.hpp>
#include <FoundationKitMemory/TrackingAllocator.hpp>
#include <FoundationKitMemory/SynchronizedAllocator.hpp>
#include <FoundationKitCxxStl/Sync/TicketLock.hpp>
#include <mm/pmm.hpp>

#include <FoundationKitOsl/Osl.hpp>

namespace ceryx::mm {

using namespace FoundationKitMemory;
using namespace FoundationKitCxxStl;

class KHeap {
public:
    static KHeap& Get() {
        static KHeap instance;
        return instance;
    }

    void Initialize(Pmm& pmm, usize heap_size_pages = 8192) { // Default 32MB
        if (m_initialized) return;

        // 1. Grab large contiguous region from PMM
        auto res = pmm.AllocatePages(heap_size_pages);
        if (!res) {
            FK_LOG_ERR("KHeap: Fatal: Failed to allocate {} pages from PMM for kernel heap!", heap_size_pages);
            return;
        }

        // 2. Initialize SlabAllocator
        // Its fallback is the PMM itself (for allocations > 512 bytes)
        m_base.Initialize(res.ptr, res.size, Pmm::Proxy{}, DefaultSlabClasses);
        
        m_initialized = true;
        FK_LOG_INFO("KHeap: Initialized 32MB kernel heap with 6-class slab strategy (TrackingEnabled).");
    }

    bool IsInitialized() const { return m_initialized; }

    [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
        return m_synchronized.Allocate(size, align);
    }

    void Deallocate(void* ptr, usize size) noexcept {
        m_synchronized.Deallocate(ptr, size);
    }

    void Deallocate(void* ptr) noexcept {
        m_synchronized.Deallocate(ptr);
    }

    [[nodiscard]] bool Owns(const void* ptr) const noexcept {
        return m_synchronized.Owns(ptr);
    }

private:
    KHeap() : m_synchronized(m_tracked) {}

    using BaseSlab = SlabAllocator<6, Pmm::Proxy>;
    using TrackedSlab = TrackingAllocator<BaseSlab>;
    using SynchronizedSlab = SynchronizedAllocator<TrackedSlab, Sync::TicketLock>;

    BaseSlab m_base;
    TrackedSlab m_tracked{m_base};
    SynchronizedSlab m_synchronized;
    bool m_initialized = false;
};

} // namespace ceryx::mm
