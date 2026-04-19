#pragma once

#include <ceryx/mm/MemoryManager.hpp>
#include <FoundationKitMemory/Management/AddressSpace.hpp>

namespace ceryx::mm {

using namespace FoundationKitMemory;

/// @brief Specialized AddressSpace for userspace processes.
class UserAddressSpace {
public:
    using AddressSpaceType = AddressSpace<
        MemoryManager::PageTableMgrType,
        MemoryManager::PageFrameAllocType,
        MemoryManager::VmaAllocType,
        MemoryManager::PdArrayType
    >;

    /// @brief Create a new user address space with kernel mappings mirrored.
    static FoundationKitCxxStl::Expected<UserAddressSpace*, MemoryError> Create() noexcept;

    /// @brief Destroy the address space and all associated mappings.
    void Destroy() noexcept;

    /// @brief Get the underlying FoundationKit AddressSpace.
    [[nodiscard]] AddressSpaceType& GetInner() noexcept { return *m_inner; }

    /// @brief Get the page table root physical address.
    [[nodiscard]] PhysicalAddress GetRootPa() const noexcept { return m_ptm->RootPhysicalAddress(); }

    /// @brief Map a physical page into the user range.
    void MapUserPage(VirtualAddress va, PhysicalAddress pa, RegionFlags flags) noexcept;

private:
    UserAddressSpace(AddressSpaceType* inner, MemoryManager::PageTableMgrType* ptm) noexcept
        : m_inner(inner), m_ptm(ptm) {}

    AddressSpaceType* m_inner;
    MemoryManager::PageTableMgrType* m_ptm;
};

} // namespace ceryx::mm
