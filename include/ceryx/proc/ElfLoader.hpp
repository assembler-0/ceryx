#pragma once

#include <ceryx/fs/Vnode.hpp>
#include <ceryx/proc/Process.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
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
    /// @brief Load an ELF binary into a process address space.
    /// @param proc Process to populate.
    /// @param executable The vnode to the executable file.
    /// @return The entry point virtual address on success, or an error code.
    static Expected<uptr, int> Load(Process& proc, fs::Vnode& executable) noexcept {
        Elf64_Ehdr header{};
        
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

        auto prev_cr3 = FoundationKitPlatform::Amd64::ControlRegs::ReadCr3();
        FoundationKitPlatform::Amd64::ControlRegs::WriteCr3(proc.GetAddressSpace().GetRootPa().value);

        for (u16 i = 0; i < header.e_phnum; ++i) {
            Elf64_Phdr phdr{};
            auto phdr_res = executable.ops->Read(executable, &phdr, sizeof(phdr), header.e_phoff + i * header.e_phentsize);
            if (!phdr_res || phdr_res.Value() != sizeof(phdr)) continue;

            if (phdr.p_type == 1) { // PT_LOAD
                u64 page_aligned_vaddr = phdr.p_vaddr & ~0xFFFULL;
                u64 padding = phdr.p_vaddr - page_aligned_vaddr;
                u64 mem_size = (phdr.p_memsz + padding + 0xFFF) & ~0xFFFULL;

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

                executable.ops->Read(executable, reinterpret_cast<void*>(phdr.p_vaddr), phdr.p_filesz, phdr.p_offset);

                if (phdr.p_memsz > phdr.p_filesz) {
                    FoundationKitMemory::MemoryZero(reinterpret_cast<void*>(phdr.p_vaddr + phdr.p_filesz), phdr.p_memsz - phdr.p_filesz);
                }
            }
        }

        // Pre-allocate user stack at 0x7FFFF0000000 (128 KiB)
        u64 stack_size = 0x20000; 
        u64 stack_top = 0x7FFFF0000000ULL;
        u64 stack_bottom = stack_top - stack_size;

        proc.GetAddressSpace().GetInner().MapAnonymous(
            FoundationKitMemory::VirtualAddress{stack_bottom}, stack_size,
            FoundationKitMemory::VmaProt::UserReadWrite,
            FoundationKitMemory::VmaFlags::Private | FoundationKitMemory::VmaFlags::Anonymous | FoundationKitMemory::VmaFlags::Fixed
        );
        for (u64 offset = 0; offset < stack_size; offset += 0x1000) {
            auto fault_res = proc.GetAddressSpace().GetInner().HandleFault(
                FoundationKitMemory::VirtualAddress{stack_bottom + offset},
                FoundationKitMemory::PageFaultFlags::Write
            );
            if (!fault_res.HasValue()) {
                 FoundationKitPlatform::Amd64::ControlRegs::WriteCr3(prev_cr3);
                 return Unexpected<int>(-ENOMEM);
            }
        }

        FoundationKitPlatform::Amd64::ControlRegs::WriteCr3(prev_cr3);

        return header.e_entry;  
    }
};

} // namespace ceryx::proc
