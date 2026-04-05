#pragma once

#include <FoundationKitMemory/BuddyAllocator.hpp>
#include <FoundationKitMemory/GlobalAllocator.hpp>
#include <FoundationKitMemory/AllocatorLocking.hpp>
#include <FoundationKitMemory/SynchronizedAllocator.hpp>
#include <FoundationKitCxxStl/Sync/SharedSpinLock.hpp>
#include <arch/x86_64/boot/requests.hpp>
#include <drivers/debugcon.hpp>

namespace ceryx::mm {

using namespace FoundationKitMemory;
using namespace FoundationKitCxxStl;

class Pmm {
public:
    static Pmm& Get() {
        static Pmm instance;
        return instance;
    }

    void Initialize() {
        auto* memmap_req = get_memmap_request();
        if (!memmap_req || !memmap_req->response) {
            FK_LOG_ERR("PMM: Error: Memory map request failed!");
            return;
        }

        auto* hhdm_req = get_hhdm_request();
        u64 hhdm_offset = hhdm_req && hhdm_req->response ? hhdm_req->response->offset : 0;

        auto* response = memmap_req->response;
        u64 largest_size = 0;
        u64 largest_base = 0;

        FK_LOG_INFO("PMM: Memory map:");
        for (u64 i = 0; i < response->entry_count; i++) {
            auto* entry = response->entries[i];
            
            const char* type_str = "Unknown";
            switch (entry->type) {
                case LIMINE_MEMMAP_USABLE:                  type_str = "Usable"; break;
                case LIMINE_MEMMAP_RESERVED:                type_str = "Reserved"; break;
                case LIMINE_MEMMAP_ACPI_RECLAIMABLE:       type_str = "ACPI Reclaimable"; break;
                case LIMINE_MEMMAP_ACPI_NVS:               type_str = "ACPI NVS"; break;
                case LIMINE_MEMMAP_BAD_MEMORY:             type_str = "Bad Memory"; break;
                case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: type_str = "Bootloader Reclaimable"; break;
                case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:  type_str = "Kernel/Modules"; break;
                case LIMINE_MEMMAP_FRAMEBUFFER:             type_str = "Framebuffer"; break;
            }
            FK_LOG_INFO("PMM: Region: {:016x} - {:016x} ({:08x}) [{}]", 
                entry->base, entry->base + entry->length, entry->length, type_str);

            if (entry->type == LIMINE_MEMMAP_USABLE && entry->length > largest_size) {
                largest_size = entry->length;
                largest_base = entry->base;
            }
        }

        if (largest_size > 0) {
            FK_LOG_INFO("PMM: Initializing BuddyAllocator with the largest usable region.");
            // Map the physical address to a virtual address using HHDM
            void* virtual_base = reinterpret_cast<void*>(largest_base + hhdm_offset);
            m_base_buddy.Initialize(virtual_base, largest_size);
            m_initialized = true;
        } else {
            FK_LOG_ERR("PMM: Error: No usable memory found!");
        }
    }

    bool IsInitialized() const { return m_initialized; }

    // Physical Memory Manager interface using the BuddyAllocator
    AllocationResult AllocatePages(usize count) {
        if (!m_initialized) return AllocationResult::Failure();
        return m_buddy.Allocate(count * 4096, 4096);
    }

    void DeallocatePages(void* ptr, usize count) {
        if (!m_initialized) return;
        m_buddy.Deallocate(ptr, count * 4096);
    }

    // This allows using the PMM directly as an IAllocator for FoundationKit containers
    [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
        return m_buddy.Allocate(size, align);
    }

    void Deallocate(void* ptr, usize size) noexcept {
        m_buddy.Deallocate(ptr, size);
    }

    [[nodiscard]] bool Owns(const void* ptr) const noexcept {
        return m_buddy.Owns(ptr);
    }

    /// @brief Lightweight proxy to the PMM singleton.
    /// @desc Allows using the PMM as a fallback in complex allocator trees without moving the singleton.
    struct Proxy {
        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            return Pmm::Get().Allocate(size, align);
        }
        void Deallocate(void* ptr, usize size) noexcept {
            Pmm::Get().Deallocate(ptr, size);
        }
        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return Pmm::Get().Owns(ptr);
        }
    };

private:
    Pmm() : m_buddy(m_base_buddy) {}

    // We use a SynchronizedAllocator to make the BuddyAllocator thread-safe
    // We use a SharedSpinLock to satisfy the requirement of SynchronizedAllocator::Owns() using SharedLock
    using RawBuddy = BuddyAllocator<20, 4096>;
    using LockedBuddy = SynchronizedAllocator<RawBuddy, Sync::SharedSpinLock>;
    
    RawBuddy m_base_buddy;
    LockedBuddy m_buddy;
    bool m_initialized = false;
};

} // namespace ceryx::mm
