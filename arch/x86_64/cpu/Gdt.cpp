#include <ceryx/cpu/Gdt.hpp>
#include <ceryx/cpu/CpuData.hpp>
#include <FoundationKitMemory/Core/MemoryOperations.hpp>

namespace ceryx::cpu {

// Per-CPU GDT and TSS storage
struct PerCpuGdtStorage {
    Gdt::GdtTable table;
    Tss tss;
};

// This would normally be in CpuData or similar, but for now we'll use a static per-cpu storage 
// (assuming we only have one CPU for now, or we'll move it to CpuData soon)
static PerCpuGdtStorage g_bsp_gdt_storage;

void Gdt::Initialize() {
    auto& storage = g_bsp_gdt_storage;
    
    FoundationKitMemory::MemoryZero(&storage, sizeof(storage));

    // 0x00: Null
    SetupDescriptor(storage.table.entries[0], 0, 0, 0, 0);
    // 0x08: Kernel Code (Long Mode, DPL 0)
    SetupDescriptor(storage.table.entries[1], 0, 0xFFFFFFFF, 0x9A, 0xAF);
    // 0x10: Kernel Data (DPL 0)
    SetupDescriptor(storage.table.entries[2], 0, 0xFFFFFFFF, 0x92, 0xCF);
    // 0x18: User Data (DPL 3)
    SetupDescriptor(storage.table.entries[3], 0, 0xFFFFFFFF, 0xF2, 0xCF);
    // 0x20: User Code (Long Mode, DPL 3)
    SetupDescriptor(storage.table.entries[4], 0, 0xFFFFFFFF, 0xFA, 0xAF);

    // TSS
    SetupTssDescriptor(storage.table, reinterpret_cast<u64>(&storage.tss), sizeof(Tss) - 1);
    storage.tss.iopb_offset = sizeof(Tss);

    GdtPointer ptr;
    ptr.limit = sizeof(GdtTable) - 1;
    ptr.base = reinterpret_cast<u64>(&storage.table);

    __asm__ volatile (
        "lgdt %0\n"
        "push %1\n"
        "lea 1f(%%rip), %%rax\n"
        "push %%rax\n"
        "lretq\n"
        "1:\n"
        "mov %2, %%ds\n"
        "mov %2, %%es\n"
        "mov %2, %%fs\n"
        "mov %2, %%ss\n"
        "ltr %3\n"
        :
        : "m"(ptr), "i"(kKernelCodeSelector), "r"(kKernelDataSelector), "r"(static_cast<u16>(kTssSelector))
        : "rax", "memory"
    );
}

void Gdt::SetupDescriptor(GdtDescriptor& desc, u32 base, u32 limit, u8 access, u8 gran) {
    desc.limit_low = limit & 0xFFFF;
    desc.base_low = base & 0xFFFF;
    desc.base_mid = (base >> 16) & 0xFF;
    desc.access = access;
    desc.granularity = (gran & 0xF0) | ((limit >> 16) & 0x0F);
    desc.base_high = (base >> 24) & 0xFF;
}

void Gdt::SetupTssDescriptor(GdtTable& table, u64 base, u32 limit) {
    SetupDescriptor(table.tss.low, base & 0xFFFFFFFF, limit, 0x89, 0x40);
    table.tss.base_upper = (base >> 32) & 0xFFFFFFFF;
    table.tss.reserved = 0;
}

void Gdt::SetStack(PrivilegeLevel level, u64 stack) {
    auto& storage = g_bsp_gdt_storage;
    if (level == PrivilegeLevel::Ring0) {
        storage.tss.rsp[0] = stack;
    }
}

} // namespace ceryx::cpu
