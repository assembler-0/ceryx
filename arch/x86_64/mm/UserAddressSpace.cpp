#include <ceryx/mm/UserAddressSpace.hpp>
#include <ceryx/mm/MemoryManager.hpp>
#include <FoundationKitMemory/Heap/GlobalAllocator.hpp>
#include <FoundationKitPlatform/Amd64/ControlRegs.hpp>
#include <arch/x86_64/boot/requests.hpp>

namespace ceryx::mm {

Expected<UserAddressSpace*, MemoryError> UserAddressSpace::Create() noexcept {
    auto& pfa = MemoryManager::GetPfa();
    auto& vma_alloc = MemoryManager::GetVmaAllocator();
    auto& pd_array = MemoryManager::GetPdArray();
    auto& queues = MemoryManager::GetQueues();
    
    HhdmOffset hhdm{get_hhdm_request()->response->offset};

    // 1. Allocate a new top-level page table (PML4 or PML5)
    auto root_res = pfa.AllocatePages(1, RegionType::Generic);
    if (!root_res.HasValue()) return Unexpected(MemoryError::OutOfMemory);
    
    PhysicalAddress root_pa = PfnToPhysical(root_res.Value());
    void* root_va = hhdm.ToVirtual(root_pa.value);
    MemoryZero(root_va, kPageSize);
    
    // 2. Mirror the kernel mappings (higher half)
    void* kernel_root_va = hhdm.ToVirtual(FoundationKitPlatform::Amd64::ControlRegs::Cr3PhysBase());
    u64* user_root = static_cast<u64*>(root_va);
    u64* kernel_root = static_cast<u64*>(kernel_root_va);
    
    // Higher half entries start at 256 for 4-level paging.
    for (int i = 256; i < 512; ++i) {
        user_root[i] = kernel_root[i];
    }
    
    // 3. Create the PageTableManager for this address space
    auto* ptm = new MemoryManager::PageTableMgrType(pfa, root_pa, Paging::DetectPagingMode(), HhdmAccessor{hhdm});
    
    // 4. Create the AddressSpace object
    VirtualAddress va_base{0x1000};
    VirtualAddress va_top{0x00007FFFFFFFF000ull}; 
    if (Paging::DetectPagingMode() == Paging::PagingMode::Level5) {
        va_top = VirtualAddress{0x00FFFFFFFFFFF000ull};
    }
    
    auto* inner = new AddressSpaceType(*ptm, pfa, vma_alloc, pd_array, queues, va_base, va_top);
    
    return new UserAddressSpace(inner, ptm);
}

void UserAddressSpace::Destroy() noexcept {
    // TODO: Implementation
}

void UserAddressSpace::MapUserPage(VirtualAddress va, PhysicalAddress pa, RegionFlags flags) noexcept {
    // Add the 'User' flag automatically since this is a user address space.
    m_ptm->Map(va, pa, kPageSize, flags | RegionFlags::User);
    m_ptm->FlushTlb(va);
}

} // namespace ceryx::mm
