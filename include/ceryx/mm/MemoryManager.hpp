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
    using KmmType            = KernelMemoryManager<PageFrameAllocType, PageTableMgrType, VmaAllocType, PdArrayType, 4>;

    static void Initialize(limine_memmap_response* memmap_response, limine_hhdm_response* hhdm_response) noexcept;

    [[nodiscard]] static PageFrameAllocType& GetPfa() noexcept {
        FK_BUG_ON(!s_instance, "MemoryManager::GetPfa: accessed before initialization");
        return *s_instance->m_pfa;
    }

    [[nodiscard]] static KmmType& GetKmm() noexcept {
        FK_BUG_ON(!s_instance || !s_instance->m_kmm, "MemoryManager::GetKmm: not initialized");
        return *s_instance->m_kmm;
    }

private:
    MemoryManager() noexcept = default;

    PageFrameAllocType* m_pfa      = nullptr;
    PageTableMgrType*   m_ptm      = nullptr;
    PdArrayType*        m_pd_array = nullptr;
    VmaAllocType*       m_vma_alloc = nullptr;
    KmmType*            m_kmm       = nullptr;

    static MemoryManager* s_instance;
};

} // namespace ceryx::mm
