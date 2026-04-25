#pragma once

#include <ceryx/fs/Vnode.hpp>
#include <ceryx/proc/Process.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <ceryx/mm/MemoryManager.hpp>
#include <ceryx/Errno.h>
#include <FoundationKitPlatform/Amd64/ControlRegs.hpp>

namespace ceryx::proc {

using namespace FoundationKitCxxStl;

/// @brief Minimal ELF64 header definition.
struct Elf64_Ehdr {
    u8  e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
};

/// @brief Minimal ELF64 Program Header definition.
struct Elf64_Phdr {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
};

class ElfLoader {
public:
    struct LoadResult {
        uptr entry_point;
        uptr stack_top;
        uptr heap_base;
    };

    /// @brief Load an ELF binary into a process address space.
    /// @param proc Process to populate.
    /// @param executable The vnode to the executable file.
    /// @param load_bias Offset to apply to all segment addresses (for PIE).
    /// @return The LoadResult on success, or an error code.
    static Expected<LoadResult, int> Load(Process& proc, fs::Vnode& executable, uptr load_bias = 0) noexcept {
        Elf64_Ehdr header{};
        u64 max_vaddr = 0;
        
        if (!executable.ops) return Unexpected<int>(-ENOEXEC); 

        auto read_res = executable.ops->Read(executable, &header, sizeof(header), 0);
        if (!read_res || read_res.Value() != sizeof(header)) {
            return Unexpected<int>(-ENOEXEC); 
        }

        // Check magic number \x7fELF
        if (header.e_ident[0] != 0x7F || header.e_ident[1] != 'E' ||
            header.e_ident[2] != 'L' || header.e_ident[3] != 'F') {
            return Unexpected<int>(-ENOEXEC); 
        }

        // Determine effective load bias.
        //
        // ET_EXEC (type 2): segments have absolute virtual addresses baked in
        //   by the linker. No bias — the binary must be loaded exactly where
        //   it says. Ignore any caller-supplied bias.
        //
        // ET_DYN (type 3): PIE or shared object. Segments start at 0 and are
        //   relocated at load time. If the caller supplies a bias, use it.
        //   If not (e.g. execve path), apply a canonical default so that the
        //   first segment doesn't land at virtual address 0 — which is the
        //   null page and cannot be mapped with MAP_FIXED.
        //
        // Any other type: reject.
        uptr effective_bias;
        if (header.e_type == 2) {          // ET_EXEC
            effective_bias = 0;
        } else if (header.e_type == 3) {   // ET_DYN / PIE
            // Default PIE base: 0x400000 (matches the bias used by Process::Spawn
            // and is above the null-page guard region on x86_64).
            effective_bias = (load_bias != 0) ? load_bias : 0x400000ULL;
        } else {
            return Unexpected<int>(-ENOEXEC);
        }

        auto prev_cr3 = FoundationKitPlatform::Amd64::ControlRegs::ReadCr3();
        FoundationKitPlatform::Amd64::ControlRegs::WriteCr3(proc.GetAddressSpace().GetRootPa().value);

        for (u16 i = 0; i < header.e_phnum; ++i) {
            Elf64_Phdr phdr{};
            auto phdr_res = executable.ops->Read(executable, &phdr, sizeof(phdr), header.e_phoff + i * header.e_phentsize);
            if (!phdr_res || phdr_res.Value() != sizeof(phdr)) continue;

            if (phdr.p_type == 1) { // PT_LOAD
                u64 vaddr = phdr.p_vaddr + effective_bias;
                u64 page_aligned_vaddr = vaddr & ~0xFFFULL;
                u64 padding = vaddr - page_aligned_vaddr;
                u64 mem_size = (phdr.p_memsz + padding + 0xFFF) & ~0xFFFULL;

                // Guard: never map the null page. A segment landing at VA 0
                // after bias application means the ELF is malformed or the
                // bias is wrong. Reject the load rather than panic in FK.
                if (page_aligned_vaddr == 0) {
                    FK_LOG_ERR("ElfLoader: PT_LOAD segment maps to VA 0 "
                               "(p_vaddr={:#x} bias={:#x}) — rejecting",
                               phdr.p_vaddr, effective_bias);
                    FoundationKitPlatform::Amd64::ControlRegs::WriteCr3(prev_cr3);
                    return Unexpected<int>(-ENOEXEC);
                }

                auto map_res = proc.GetAddressSpace().GetInner().MapAnonymous(
                    FoundationKitMemory::VirtualAddress{page_aligned_vaddr}, mem_size,
                    FoundationKitMemory::VmaProt::UserAll,
                    FoundationKitMemory::VmaFlags::Private | FoundationKitMemory::VmaFlags::Anonymous | FoundationKitMemory::VmaFlags::Fixed
                );

                if (!map_res) {
                    FoundationKitPlatform::Amd64::ControlRegs::WriteCr3(prev_cr3);
                    return Unexpected<int>(-ENOMEM);
                }

                for (u64 offset = 0; offset < mem_size; offset += 0x1000) {
                    auto fault_res = proc.GetAddressSpace().GetInner().HandleFault(
                        FoundationKitMemory::VirtualAddress{page_aligned_vaddr + offset},
                        FoundationKitMemory::PageFaultFlags::Write
                    );
                    if (!fault_res.HasValue()) {
                        FoundationKitPlatform::Amd64::ControlRegs::WriteCr3(prev_cr3);
                        return Unexpected<int>(-ENOMEM);
                    }
                }

                executable.ops->Read(executable, reinterpret_cast<void*>(vaddr), phdr.p_filesz, phdr.p_offset);

                if (phdr.p_memsz > phdr.p_filesz) {
                    FoundationKitMemory::MemoryZero(reinterpret_cast<void*>(vaddr + phdr.p_filesz), phdr.p_memsz - phdr.p_filesz);
                }

                if (vaddr + phdr.p_memsz > max_vaddr) {
                    max_vaddr = vaddr + phdr.p_memsz;
                }
            }
        }

        // Pre-allocate 8MB user stack. Position it at the top of the address space.
        u64 stack_size = 0x800000; // 8 MiB
        FoundationKitMemory::VirtualAddress stack_hint{mm::MemoryManager::Layout::GetUserTop().value - stack_size - 0x1000};
        auto stack_res = proc.GetAddressSpace().GetInner().MapAnonymous(
            stack_hint, stack_size,
            FoundationKitMemory::VmaProt::UserReadWrite,
            FoundationKitMemory::VmaFlags::Private | FoundationKitMemory::VmaFlags::Anonymous
        );

        if (!stack_res) {
            FoundationKitPlatform::Amd64::ControlRegs::WriteCr3(prev_cr3);
            return Unexpected<int>(-ENOMEM);
        }

        u64 stack_bottom = stack_res.Value().value;
        u64 stack_top = stack_bottom + stack_size;

        // Fault in the last page of the stack immediately so it's ready.
        proc.GetAddressSpace().GetInner().HandleFault(
            FoundationKitMemory::VirtualAddress{stack_top - 0x1000},
            FoundationKitMemory::PageFaultFlags::Write
        );

        FoundationKitPlatform::Amd64::ControlRegs::WriteCr3(prev_cr3);

        return LoadResult{
            .entry_point = header.e_entry + effective_bias,
            .stack_top = stack_top,
            .heap_base = (max_vaddr + 0xFFF) & ~0xFFFULL
        };
    }
};

} // namespace ceryx::proc
