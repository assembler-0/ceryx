#pragma once

#include <FoundationKitMemory/Management/KernelMemoryManager.hpp>
#include <FoundationKitMemory/Management/PageFrameAllocator.hpp>
#include <FoundationKitMemory/Management/PageDescriptorArray.hpp>
#include <FoundationKitMemory/Allocators/BuddyAllocator.hpp>
#include <FoundationKitMemory/Core/MemoryOperations.hpp>
#include <FoundationKitPlatform/Amd64/Amd64PageTableManager.hpp>
#include <arch/x86_64/boot/limine.hpp>

namespace ceryx::mm {

using namespace FoundationKitMemory;
using namespace FoundationKitPlatform::Amd64;

/// @brief Physical memory accessor for HHDM-based systems.
struct HhdmAccessor {
    HhdmOffset offset;

    [[nodiscard]] constexpr void* Access(PhysicalAddress pa) const noexcept {
        return offset.ToVirtual(pa.value);
    }

    [[nodiscard]] constexpr void* Access(uptr pa) const noexcept {
        return offset.ToVirtual(pa);
    }

    [[nodiscard]] constexpr void* ToVirtual(PhysicalAddress pa) const noexcept {
        return offset.ToVirtual(pa.value);
    }

    [[nodiscard]] constexpr PhysicalAddress ToPhysical(const void* vptr) const noexcept {
        return PhysicalAddress{offset.ToPhysical(const_cast<void*>(vptr))};
    }

    void ZeroPage(PhysicalAddress pa) const noexcept {
        MemoryZero(ToVirtual(pa), kPageSize);
    }

    void CopyPage(PhysicalAddress src, PhysicalAddress dst) const noexcept {
        MemoryCopy(ToVirtual(dst), ToVirtual(src), kPageSize);
    }
};

static_assert(IPhysicalMemoryAccessor<HhdmAccessor>);

/// @brief Central memory management orchestration for Ceryx.
class MemoryManager {
public:
    using PageFrameAllocType = PageFrameAllocator<32, 20>;
    using PageTableMgrType   = Amd64PageTableManager<PageFrameAllocType, HhdmAccessor>;
    using PdArrayType        = PageDescriptorArray<1024 * 1024 * 8>; // 8M pages = 32GB
    using VmaAllocType       = BuddyAllocator<16, kPageSize>;
    using HeapAllocType      = BuddyAllocator<20, 128>; // 128 * 2^20 = 128MB.
    using KmmType            = KernelMemoryManager<PageFrameAllocType, PageTableMgrType, VmaAllocType, PdArrayType, 4>;

    /// @brief Dynamic virtual memory layout based on detected paging mode.
    struct Layout {
        static VirtualAddress GetUserTop() noexcept {
            return (Paging::DetectPagingMode() == Paging::PagingMode::Level5)
                ? VirtualAddress{0x00FFFFFFFFFFF000ull}  // 56-bit user space
                : VirtualAddress{0x00007FFFFFFFF000ull}; // 47-bit user space
        }

        static VirtualAddress GetKernelBase() noexcept {
            // We start KMM above the HHDM.
            // Limine HHDM is at 0xFFFF800000000000 (4-level) or 0xFF00000000000000 (5-level).
            return (Paging::DetectPagingMode() == Paging::PagingMode::Level5)
                ? VirtualAddress{0xFF10000000000000ull}  // Give 5-level HHDM 1PB
                : VirtualAddress{0xFFFF900000000000ull}; // Give 4-level HHDM 1TB
        }

        static VirtualAddress GetKernelTop() noexcept {
            return VirtualAddress{0xFFFFFFFFFFFFF000ull};
        }
    };

    static void Initialize(limine_memmap_response* memmap_response, limine_hhdm_response* hhdm_response) noexcept;

    [[nodiscard]] static PageFrameAllocType& GetPfa() noexcept {
        FK_BUG_ON(!s_instance, "MemoryManager::GetPfa: accessed before initialization");
        return *s_instance->m_pfa;
    }

    [[nodiscard]] static PageTableMgrType& GetPtm() noexcept {
        FK_BUG_ON(!s_instance || !s_instance->m_ptm, "MemoryManager::GetPtm: not initialized");
        return *s_instance->m_ptm;
    }

    [[nodiscard]] static KmmType& GetKmm() noexcept {
        FK_BUG_ON(!s_instance || !s_instance->m_kmm, "MemoryManager::GetKmm: not initialized");
        return *s_instance->m_kmm;
    }

    [[nodiscard]] static VmaAllocType& GetVmaAllocator() noexcept {
        FK_BUG_ON(!s_instance || !s_instance->m_vma_alloc, "MemoryManager::GetVmaAllocator: not initialized");
        return *s_instance->m_vma_alloc;
    }

    [[nodiscard]] static PdArrayType& GetPdArray() noexcept {
        FK_BUG_ON(!s_instance || !s_instance->m_pd_array, "MemoryManager::GetPdArray: not initialized");
        return *s_instance->m_pd_array;
    }

    [[nodiscard]] static PageQueueSet& GetQueues() noexcept {
        FK_BUG_ON(!s_instance || !s_instance->m_kmm, "MemoryManager::GetQueues: not initialized");
        return s_instance->m_kmm->Queues();
    }

private:
    MemoryManager() noexcept = default;

    PageFrameAllocType* m_pfa      = nullptr;
    PageTableMgrType*   m_ptm      = nullptr;
    PdArrayType*        m_pd_array = nullptr;
    VmaAllocType*       m_vma_alloc = nullptr;
    KmmType*            m_kmm       = nullptr;
    HeapAllocType*      m_heap_alloc = nullptr;

    static MemoryManager* s_instance;
};

} // namespace ceryx::mm
