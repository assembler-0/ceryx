#include <ceryx/mm/MemoryManager.hpp>
#include <ceryx/cpu/Idt.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitMemory/Heap/GlobalAllocator.hpp>
#include <FoundationKitPlatform/Amd64/ControlRegs.hpp>

namespace ceryx::mm {

MemoryManager* MemoryManager::s_instance = nullptr;

void MemoryManager::Initialize(limine_memmap_response* memmap_response, limine_hhdm_response* hhdm_response) noexcept {
    FK_BUG_ON(s_instance != nullptr, "MemoryManager::Initialize: already initialized");
    FK_BUG_ON(!memmap_response, "MemoryManager::Initialize: null memmap response");
    FK_BUG_ON(!hhdm_response, "MemoryManager::Initialize: null HHDM response");

    static MemoryManager instance;
    s_instance = &instance;

    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Starting memory management initialization...");

    // 1. Setup HHDM Accessor
    HhdmAccessor hhdm{HhdmOffset{hhdm_response->offset}};

    // 2. Prepare Page Descriptor Array
    // We search for a usable memory region that is large enough to hold our PD array.
    
    // Determine the actual maximum physical address present in the system memmap.
    u64 max_phys_addr = 0;
    u64 total_usable_mem = 0;
    for (u64 i = 0; i < memmap_response->entry_count; ++i) {
        auto* entry = memmap_response->entries[i];
        if (entry->base + entry->length > max_phys_addr) {
            max_phys_addr = entry->base + entry->length;
        }
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            total_usable_mem += entry->length;
        }
    }

    // Support up to PdArrayType's maximum (8M pages = 32GB).
    // We only manage up to the detected max_phys_addr or 32GB, whichever is smaller.
    constexpr u64 kMaxManagedPages = 1024 * 1024 * 8;
    constexpr u64 kMaxManagedPhys  = kMaxManagedPages * kPageSize;
    
    if (max_phys_addr > kMaxManagedPhys) {
        FK_LOG_WARN("ceryx::mm::MemoryManager::Initialize: WARNING: System physical address range ({:#x}) exceeds managed capacity ({:#x}).",
                    max_phys_addr, kMaxManagedPhys);
        FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: USABLE memory above 32GB will be ignored.");
        max_phys_addr = kMaxManagedPhys;
    }

    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Managed physical range: 0 - {:#x}, Total usable memory: {} MB", 
                max_phys_addr, total_usable_mem / (1024 * 1024));

    u64 total_pages = (max_phys_addr + kPageSize - 1) / kPageSize;
    usize pd_array_size = PdArrayType::StorageSize(total_pages);
    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: PD array size required: {} KB for {} pages", 
                pd_array_size / 1024, total_pages);
    
    limine_memmap_entry* best_entry = nullptr;
    for (u64 i = 0; i < memmap_response->entry_count; ++i) {
        auto* entry = memmap_response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            // Region must at least start within managed range to be considered for PD array
            if (entry->base >= kMaxManagedPhys) continue;
            
            // Available length within managed range
            u64 available_len = entry->length;
            if (entry->base + available_len > kMaxManagedPhys) {
                available_len = kMaxManagedPhys - entry->base;
            }

            if (available_len >= pd_array_size) {
                if (!best_entry || available_len > best_entry->length) {
                    best_entry = entry;
                }
            }
        }
    }

    if (!best_entry) {
        FK_LOG_ERR("ceryx::mm::MemoryManager::Initialize: CRITICAL: Could not find {} KB of contiguous usable memory for PD array!", pd_array_size / 1024);
        for (u64 i = 0; i < memmap_response->entry_count; ++i) {
            auto* entry = memmap_response->entries[i];
            FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Region {}: base={:#x}, len={:#x}, type={}", 
                        i, entry->base, entry->length, entry->type);
        }
    }

    FK_BUG_ON(!best_entry, "MemoryManager::Initialize: no usable memory region large enough for PD array found");

    void* pd_array_storage = hhdm.offset.ToVirtual(best_entry->base);
    
    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Initializing PD array at {}...", pd_array_storage);
    static byte pd_array_storage_obj[sizeof(PdArrayType)] __attribute__((aligned(alignof(PdArrayType))));
    auto* pd_array_inst = new (pd_array_storage_obj) PdArrayType();
    instance.m_pd_array = pd_array_inst;
    instance.m_pd_array->Initialize(pd_array_storage, Pfn{0}, total_pages);
    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: PD array initialized.");

    // Consume space used by PD array for the best_entry
    u64 used_by_pd = (pd_array_size + kPageSize - 1) & ~(kPageSize - 1);
    
    // 3. Setup Page Frame Allocator
    static byte pfa_storage[sizeof(PageFrameAllocType)] __attribute__((aligned(alignof(PageFrameAllocType))));
    auto* pfa_inst = new (pfa_storage) PageFrameAllocType();
    instance.m_pfa = pfa_inst;

    // Use a small buffer to store zone information temporarily if needed, 
    // but RegisterZone stores them internally.

    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Registering memory zones...");
    for (u64 i = 0; i < memmap_response->entry_count; ++i) {
        auto* entry = memmap_response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            u64 base = entry->base;
            u64 length = entry->length;

            // Ignore memory regions (or parts of them) that exceed our managed capacity.
            if (base >= kMaxManagedPhys) {
                FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Ignoring usable region {:#x} - {:#x} (exceeds 32GB)", base, base + length);
                continue;
            }
            if (base + length > kMaxManagedPhys) {
                FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Truncating usable region {:#x} - {:#x} to 32GB limit", base, base + length);
                length = kMaxManagedPhys - base;
            }

            if (entry == best_entry) {
                // Skip the part used by PD array
                if (length <= used_by_pd) continue;
                base += used_by_pd;
                length -= used_by_pd;
            }

            // BuddyAllocator requires alignment for the start address relative to its base.
            // For BuddyOrder=20, MaxBlockSize is 4GB. If a zone is larger than 4GB, it will fail.
            // We split large zones into 4GB chunks.
            constexpr u64 kMaxZoneSize = (1ULL << 20) * kPageSize; // 4GB for order 20
            
            while (length >= kPageSize) {
                u64 chunk_size = length;
                if (chunk_size > kMaxZoneSize) chunk_size = kMaxZoneSize;
                
                // Ensure chunk doesn't cross MaxZoneSize boundary if possible, 
                // but BuddyAllocator handles offset internally.
                
                instance.m_pfa->RegisterZone(PhysicalAddress{base}, chunk_size / kPageSize, RegionType::Generic, hhdm.offset.ToVirtual(base));
                
                base += chunk_size;
                length -= chunk_size;
            }
        }
    }
    
    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Booting Page Frame Allocator...");
    instance.m_pfa->Boot();
    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Page Frame Allocator booted.");

    // 4. Setup Page Table Manager
    auto paging_mode = Paging::DetectPagingMode();
    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Detected paging mode: {}-level", static_cast<u8>(paging_mode));

    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Setting up Page Table Manager...");
    PhysicalAddress kernel_pt_root{ControlRegs::Cr3PhysBase()};
    static byte ptm_storage[sizeof(PageTableMgrType)] __attribute__((aligned(alignof(PageTableMgrType))));
    auto* ptm_inst = new (ptm_storage) PageTableMgrType(*pfa_inst, kernel_pt_root, paging_mode, HhdmAccessor{hhdm});
    instance.m_ptm = ptm_inst;

    // 5. Setup VMA Allocator (using BuddyAllocator)
    // We need to allocate some memory for the VMA allocator itself.
    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Allocating memory for VMA allocator...");
    auto vma_alloc_res = pfa_inst->AllocatePages(8192, RegionType::Generic);
    FK_BUG_ON(!vma_alloc_res.HasValue(), "Failed to allocate memory for VMA allocator");
    void* vma_alloc_storage = hhdm.offset.ToVirtual(vma_alloc_res.Value().value * kPageSize);
    
    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Initializing VMA allocator...");
    static byte vma_storage[sizeof(VmaAllocType)] __attribute__((aligned(alignof(VmaAllocType))));
    auto* vma_alloc_inst = new (vma_storage) VmaAllocType();
    vma_alloc_inst->Initialize(vma_alloc_storage, 8192 * kPageSize);
    instance.m_vma_alloc = vma_alloc_inst;

    // 6. Setup Kernel Memory Manager
    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Initializing Kernel Memory Manager...");

    VirtualAddress k_base = Layout::GetKernelBase();
    VirtualAddress k_top = Layout::GetKernelTop();

    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: KMM VA Range: {:#x} - {:#x}", k_base.value, k_top.value);
    
    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Constructing KernelMemoryManager object...");
    
    // Use manual placement new for all static instances to bypass thread-safe initialization guards.
    // The memory for these objects is allocated in the .bss section.
    static byte kmm_storage[sizeof(KmmType)] __attribute__((aligned(alignof(KmmType))));
    auto* kmm_inst = new (kmm_storage) KmmType(*pfa_inst, *instance.m_ptm, *vma_alloc_inst, *pd_array_inst, k_base, k_top);
    
    instance.m_kmm = kmm_inst;

    // 6.05 Populate Page Queues
    // All physical pages managed by the PageDescriptorArray must be placed into the Free queue.
    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Populating page queues...");
    {
        auto& free_queue = kmm_inst->GetQueues().FreeQueue();
        FoundationKitCxxStl::Sync::UniqueLock guard(free_queue.m_lock);
        for (usize i = 0; i < pd_array_inst->TotalPages(); ++i) {
            PageDescriptor& desc = pd_array_inst->Get(Pfn{pd_array_inst->BasePfn().value + i});
            free_queue.EnqueueUnlocked(&desc);
        }
    }
    
    // 6.1 Configure Memory Pressure Manager
    // Set reasonable watermarks: Min 1MB, Low 4MB, High 16MB in pages
    constexpr usize kPages1MB = (1 * 1024 * 1024) / kPageSize;
    constexpr usize kPages4MB = (4 * 1024 * 1024) / kPageSize;
    constexpr usize kPages16MB = (16 * 1024 * 1024) / kPageSize;
    kmm_inst->SetWatermarks(kPages1MB, kPages4MB, kPages16MB);
    
    // 7. Setup Global Heap Allocator
    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Initializing Global Heap Allocator...");
    
    // Allocate 128MB for the kernel heap initially.
    // BuddyAllocator requires naturally aligned power-of-two blocks for large allocations.
    constexpr usize kHeapInitialSize = 128 * 1024 * 1024;
    constexpr usize kHeapInitialPages = kHeapInitialSize / kPageSize;
    
    auto heap_base_res = kmm_inst->AllocateKernel(kHeapInitialPages, RegionFlags::Writable | RegionFlags::Readable, RegionType::Generic);
    FK_BUG_ON(!heap_base_res.HasValue(), "Failed to allocate memory for kernel heap");
    
    VirtualAddress heap_base = heap_base_res.Value();
    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Heap range: {:#x} - {:#x} ({} MB)", 
                heap_base.value, heap_base.value + kHeapInitialSize, kHeapInitialSize / (1024 * 1024));
    
    static byte heap_alloc_storage[sizeof(HeapAllocType)] __attribute__((aligned(alignof(HeapAllocType))));
    auto* heap_alloc_inst = new (heap_alloc_storage) HeapAllocType();
    heap_alloc_inst->Initialize(reinterpret_cast<void*>(heap_base.value), kHeapInitialSize);
    instance.m_heap_alloc = heap_alloc_inst;
    
    // Initialize the FoundationKit Global Allocator System
    FoundationKitMemory::InitializeGlobalAllocator(*heap_alloc_inst);
    
    // 8. Setup Page Fault Handler
    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Registering Page Fault handler...");
    cpu::Idt::RegisterHandler(14, [](cpu::InterruptFrame* frame) {
        VirtualAddress fault_va{FoundationKitPlatform::Amd64::ControlRegs::ReadCr2()};
        
        PageFaultFlags flags{PageFaultFlags::None};
        if (frame->error_code & (1 << 0)) flags = flags | PageFaultFlags::Present;
        if (frame->error_code & (1 << 1)) flags = flags | PageFaultFlags::Write;
        if (frame->error_code & (1 << 2)) flags = flags | PageFaultFlags::User;
        if (frame->error_code & (1 << 4)) flags = flags | PageFaultFlags::Instruction;

        auto res = MemoryManager::GetKmm().HandlePageFault(fault_va, flags);
        if (!res.HasValue()) {
            FK_LOG_ERR("Page Fault UNRESOLVED: {:#x} at RIP {:#x}, error {:#x}", 
                       fault_va.value, frame->rip, frame->error_code);
            
            // Dump registers for easier debugging
            FK_LOG_ERR("RAX: {:#x} RBX: {:#x} RCX: {:#x} RDX: {:#x}", frame->rax, frame->rbx, frame->rcx, frame->rdx);
            FK_LOG_ERR("RSI: {:#x} RDI: {:#x} RBP: {:#x} RSP: {:#x}", frame->rsi, frame->rdi, frame->rbp, frame->rsp);

            FK_BUG("Unresolved Page Fault");
        }
    });
    
    // Test the allocator internally to ensure it's ready
    auto internal_test_res = heap_alloc_inst->Allocate(64, 16);
    FK_BUG_ON(!internal_test_res, "MemoryManager::Initialize: Internal heap test allocation failed");
    heap_alloc_inst->Deallocate(internal_test_res.ptr, 64);
    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Internal heap test passed.");

    FK_LOG_INFO("ceryx::mm::MemoryManager::Initialize: Memory management initialized successfully.");
}

} // namespace ceryx::mm
