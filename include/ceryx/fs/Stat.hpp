#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <ceryx/fs/Vnode.hpp>

namespace ceryx::fs {

using namespace FoundationKitCxxStl;

/// @brief Linux x86_64 ABI-compatible stat structure (struct stat64 / statx layout).
///        Field order and sizes match what glibc and musl expect on x86_64.
struct KernelStat {
    u64 st_dev;
    u64 st_ino;
    u64 st_nlink;
    u32 st_mode;
    u32 st_uid;
    u32 st_gid;
    u32 __pad0;
    u64 st_rdev;
    i64 st_size;
    i64 st_blksize;
    i64 st_blocks;
    u64 st_atime;
    u64 st_atime_nsec;
    u64 st_mtime;
    u64 st_mtime_nsec;
    u64 st_ctime;
    u64 st_ctime_nsec;
    i64 __unused[3];
};

static_assert(sizeof(KernelStat) == 144, "KernelStat must be 144 bytes (Linux x86_64 ABI)");

/// @brief Map a VnodeType to the POSIX S_IF* file-type bits embedded in st_mode.
[[nodiscard]] constexpr u32 VnodeTypeToMode(VnodeType type) noexcept {
    switch (type) {
        case VnodeType::Regular:     return 0100000u; // S_IFREG
        case VnodeType::Directory:   return 0040000u; // S_IFDIR
        case VnodeType::BlockDevice: return 0060000u; // S_IFBLK
        case VnodeType::CharDevice:  return 0020000u; // S_IFCHR
        case VnodeType::SymLink:     return 0120000u; // S_IFLNK
        case VnodeType::Socket:      return 0140000u; // S_IFSOCK
        case VnodeType::Fifo:        return 0010000u; // S_IFIFO
    }
    return 0u;
}

/// @brief Populate a KernelStat from a Vnode.
///
/// @param vnode  Source vnode (read-only).
/// @param out    Destination stat structure. Zeroed before filling.
///
/// @note st_ino is derived from the vnode's kernel address. This is stable
///       within a single boot but not persistent across reboots — acceptable
///       for an in-memory filesystem.
inline void VnodeToStat(const Vnode& vnode, KernelStat& out) noexcept {
    out            = KernelStat{};
    out.st_mode    = VnodeTypeToMode(vnode.type) | (vnode.permissions & 0777u);
    out.st_size    = static_cast<i64>(vnode.size);
    out.st_blksize = 4096;
    out.st_blocks  = static_cast<i64>((vnode.size + 511u) / 512u);
    out.st_nlink   = 1;
    // Use the vnode's kernel virtual address as a stable inode number.
    // All vnodes are heap-allocated and unique within a boot session.
    out.st_ino     = reinterpret_cast<u64>(&vnode);
}

} // namespace ceryx::fs
