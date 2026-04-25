#pragma once

#include <ceryx/fs/Vnode.hpp>
#include <FoundationKitMemory/Management/VmObject.hpp>
#include <FoundationKitMemory/Management/PageFrameAllocator.hpp>
#include <FoundationKitMemory/Core/MemoryOperations.hpp>
#include <FoundationKitCxxStl/Base/String.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <arch/x86_64/boot/requests.hpp>
#include <ceryx/Errno.h>
#include <ceryx/mm/MemoryManager.hpp>

#include <FoundationKitCxxStl/Base/Vector.hpp>

namespace ceryx::fs::ramfs {

using namespace FoundationKitCxxStl;
using namespace FoundationKitMemory;

/// @brief Represents a node in the RamFs.
class RamFsNode : public Vnode {
public:
    FoundationKitMemory::SharedPtr<VmObject> vmo;
    Vector<RefPtr<Vnode>> children;

    RamFsNode(VnodeType t, const VnodeOps* o, StringView node_name) noexcept
        : Vnode(t, o) {
        // Store name in the base Vnode::name field.
        this->name = node_name;
        if (t == VnodeType::Regular) {
            vmo = FoundationKitMemory::MakeShared<VmObject>();
        }
    }

    /// @brief Insert any Vnode as a child (used for pseudo-fs nodes in /dev, /proc).
    bool InsertChild(RefPtr<Vnode> child) noexcept {
        FK_BUG_ON(type != VnodeType::Directory,
            "RamFsNode::InsertChild: called on non-directory node");
        return children.PushBack(child);
    }

    // Deallocation is handled by the ops->Destroy table entry.
};

/// @brief Implementation of VnodeOps for RamFs
struct RamFsOpsImpl {
    static void Destroy(Vnode& vnode) noexcept {
        auto* ram_node = static_cast<RamFsNode*>(&vnode);
        if (ram_node->vmo) {
            // We cannot call RemoveBlock() inside ForEachPage() — ForEachPage holds
            // a SharedLock and RemoveBlock takes a UniqueLock on the same lock.
            // We also cannot keep a 4096-element pointer array on the stack (32KB,
            // blows the 2048-byte frame limit).
            //
            // Strategy: drain the tree in small batches.
            //   1. Collect up to kBatch page pointers under the SharedLock.
            //   2. Release the lock.
            //   3. Call RemoveBlock + FreeFolio + delete for each collected pointer.
            //   4. Repeat until the tree is empty.
            //
            // This is safe because Destroy() is called only when the refcount hits
            // zero — no other thread can be inserting pages at this point.
            static constexpr usize kBatch = 32; // 32 * 8 bytes = 256 bytes on stack
            bool done = false;
            while (!done) {
                VmPage* batch[kBatch];
                usize   batch_count = 0;

                // Snapshot up to kBatch pages under the ForEachPage SharedLock.
                ram_node->vmo->ForEachPage([&](const VmPage& p) noexcept {
                    if (batch_count < kBatch) {
                        batch[batch_count++] = const_cast<VmPage*>(&p);
                    }
                });

                if (batch_count == 0) {
                    done = true;
                    break;
                }

                // Remove and free outside the lock window.
                for (usize i = 0; i < batch_count; ++i) {
                    VmPage* p = batch[i];
                    ram_node->vmo->RemoveBlock(p);
                    ceryx::mm::MemoryManager::GetKmm().FreeFolio(
                        Folio::FromSinglePage(
                            &ceryx::mm::MemoryManager::GetPdArray().Get(p->pfn)));
                    delete p;
                }
            }
        }
        // Children are RefPtr<Vnode> — dropping them decrements refcounts.
        // Each child's own Destroy() fires when its refcount hits zero.
        ram_node->children.Clear();
        delete ram_node;
    }

    static Expected<usize, int> Read(Vnode& vnode, void* buffer, usize size, usize offset) noexcept {
        if (vnode.type != VnodeType::Regular) return Unexpected<int>(-EISDIR);
        if (offset >= vnode.size) return 0;
        usize count = size;
        if (offset + size > vnode.size) count = vnode.size - offset;

        auto& ram_node = static_cast<RamFsNode&>(vnode);
        if (!ram_node.vmo) return 0;

        usize copied = 0;
        u64 hhdm = get_hhdm_request()->response->offset;

        while (copied < count) {
            usize curr_offset = offset + copied;
            usize page_offset = curr_offset & (kPageSize - 1);
            usize page_remain = kPageSize - page_offset;
            usize to_copy = (count - copied) < page_remain ? (count - copied) : page_remain;

            auto phys_opt = ram_node.vmo->Lookup(curr_offset);
            if (phys_opt) {
                void* src = reinterpret_cast<void*>(hhdm + phys_opt.Value().value);
                FoundationKitMemory::MemoryCopy(
                    static_cast<u8*>(buffer) + copied, src, to_copy
                );
            } else {
                FoundationKitMemory::MemoryZero(static_cast<u8*>(buffer) + copied, to_copy);
            }
            copied += to_copy;
        }
        return copied;
    }
    
    static Expected<usize, int> Write(Vnode& vnode, const void* buffer, usize size, usize offset) noexcept {
        if (vnode.type != VnodeType::Regular) return Unexpected<int>(-EISDIR);
        auto& ram_node = static_cast<RamFsNode&>(vnode);
        if (!ram_node.vmo) return Unexpected<int>(-ENOMEM); 

        usize copied = 0;
        u64 hhdm = get_hhdm_request()->response->offset;

        while (copied < size) {
            usize curr_offset = offset + copied;
            usize page_offset = curr_offset & (kPageSize - 1);
            usize page_remain = kPageSize - page_offset;
            usize to_copy = (size - copied) < page_remain ? (size - copied) : page_remain;

            auto phys_opt = ram_node.vmo->Lookup(curr_offset);
            if (!phys_opt) {
                auto alloc_res = ceryx::mm::MemoryManager::GetKmm().AllocatePage();
                if (!alloc_res) return Unexpected<int>(-ENOMEM);

                auto folio = alloc_res.Value();
                auto* page = new VmPage();
                page->offset = curr_offset & ~(static_cast<u64>(kPageSize) - 1);
                page->size_bytes = kPageSize;
                page->pfn = folio.HeadPfn();
                
                void* pg_addr = reinterpret_cast<void*>(hhdm + PfnToPhysical(page->pfn).value);
                FoundationKitMemory::MemoryZero(pg_addr, kPageSize);
                
                ram_node.vmo->InsertBlock(page);
                phys_opt = PfnToPhysical(folio.HeadPfn());
            }

            void* dst = reinterpret_cast<void*>(hhdm + phys_opt.Value().value + page_offset);
            FoundationKitMemory::MemoryCopy(
                dst, static_cast<const u8*>(buffer) + copied, to_copy
            );
            copied += to_copy;
        }

        if (offset + size > vnode.size) vnode.size = offset + size;
        return copied;
    }
    
    static Expected<RefPtr<Vnode>, int> Lookup(Vnode& vnode, StringView name) noexcept {
        if (vnode.type != VnodeType::Directory) return Unexpected<int>(-ENOTDIR);
        auto& ram_node = static_cast<RamFsNode&>(vnode);
        
        for (auto& child : ram_node.children) {
            if (child->name == name) return child;
        }
        return Unexpected<int>(-ENOENT);
    }

    static const VnodeOps* GetOps() noexcept;

    static Expected<RefPtr<Vnode>, int> Create(Vnode& dir, StringView name) noexcept {
        if (dir.type != VnodeType::Directory) return Unexpected<int>(-ENOTDIR);
        auto& ram_dir = static_cast<RamFsNode&>(dir);
        
        for (auto& child : ram_dir.children) {
            if (child->name == name) return Unexpected<int>(-EEXIST);
        }

        auto node = RefPtr<RamFsNode>(new RamFsNode(VnodeType::Regular, GetOps(), name));
        if (!node) return Unexpected<int>(-ENOMEM);
        
        if (!ram_dir.children.PushBack(RefPtr<Vnode>(node))) {
            return Unexpected<int>(-ENOMEM);
        }
        return RefPtr<Vnode>(node);
    }

    static Expected<RefPtr<Vnode>, int> Mkdir(Vnode& dir, StringView name) noexcept {
        if (dir.type != VnodeType::Directory) return Unexpected<int>(-ENOTDIR);
        auto& ram_dir = static_cast<RamFsNode&>(dir);

        for (auto& child : ram_dir.children) {
            if (child->name == name) return child;
        }

        auto node = RefPtr<RamFsNode>(new RamFsNode(VnodeType::Directory, GetOps(), name));
        if (!node) return Unexpected<int>(-ENOMEM);

        if (!ram_dir.children.PushBack(RefPtr<Vnode>(node))) {
            return Unexpected<int>(-ENOMEM);
        }
        return RefPtr<Vnode>(node);
    }

    static Expected<RefPtr<Vnode>, int> Mknod(Vnode& dir, StringView name, VnodeType type) noexcept {
        if (dir.type != VnodeType::Directory) return Unexpected<int>(-ENOTDIR);
        auto& ram_dir = static_cast<RamFsNode&>(dir);

        auto node = RefPtr<RamFsNode>(new RamFsNode(type, GetOps(), name));
        if (!node) return Unexpected<int>(-ENOMEM);

        if (!ram_dir.children.PushBack(RefPtr<Vnode>(node))) {
            return Unexpected<int>(-ENOMEM);
        }
        return RefPtr<Vnode>(node);
    }

    /// @brief Iterate directory entries into a linux_dirent64 buffer.
    ///
    /// @param dir     Directory vnode.
    /// @param buf     Caller buffer for linux_dirent64 records.
    /// @param buf_len Size of buf in bytes.
    /// @param offset  Child index to start from (0 = first entry).
    /// @return Bytes written, 0 if no more entries, or negative errno.
    ///
    /// linux_dirent64 layout (variable-length, name immediately follows header):
    ///   u64  d_ino      — stable inode (vnode address)
    ///   i64  d_off      — next cookie (current index + 1)
    ///   u16  d_reclen   — total record size, 8-byte aligned
    ///   u8   d_type     — DT_* constant
    ///   char d_name[]   — null-terminated name
    static Expected<usize, int> Iterate(Vnode& dir, void* buf, usize buf_len, usize offset) noexcept {
        if (dir.type != VnodeType::Directory) return Unexpected<int>(-ENOTDIR);
        auto& ram_dir = static_cast<RamFsNode&>(dir);

        struct Dirent64Header {
            u64 d_ino;
            i64 d_off;
            u16 d_reclen;
            u8  d_type;
            // char d_name[] follows immediately
        };

        // DT_* type constants matching Linux uapi/linux/dirent.h
        auto vtype_to_dt = [](VnodeType t) noexcept -> u8 {
            switch (t) {
                case VnodeType::Regular:     return 8;  // DT_REG
                case VnodeType::Directory:   return 4;  // DT_DIR
                case VnodeType::BlockDevice: return 6;  // DT_BLK
                case VnodeType::CharDevice:  return 2;  // DT_CHR
                case VnodeType::SymLink:     return 10; // DT_LNK
                case VnodeType::Socket:      return 12; // DT_SOCK
                case VnodeType::Fifo:        return 1;  // DT_FIFO
            }
            return 0; // DT_UNKNOWN
        };

        u8*   out     = static_cast<u8*>(buf);
        usize written = 0;

        for (usize i = offset; i < ram_dir.children.Size(); ++i) {
            auto& child     = ram_dir.children[i];
            StringView cname = child->name;
            usize name_len  = cname.Size();

            // Record size: header + name + null terminator, rounded up to 8 bytes.
            usize rec_size = (sizeof(Dirent64Header) + name_len + 1 + 7) & ~usize{7};

            if (written + rec_size > buf_len) break; // Buffer full — caller retries.

            auto* hdr     = reinterpret_cast<Dirent64Header*>(out + written);
            hdr->d_ino    = reinterpret_cast<u64>(child.Get());
            hdr->d_off    = static_cast<i64>(i + 1);
            hdr->d_reclen = static_cast<u16>(rec_size);
            hdr->d_type   = vtype_to_dt(child->type);

            char* name_dst = reinterpret_cast<char*>(hdr + 1);
            for (usize j = 0; j < name_len; ++j) name_dst[j] = cname[j];
            name_dst[name_len] = '\0';

            written += rec_size;
        }

        return written;
    }
};

inline const VnodeOps* RamFsOpsImpl::GetOps() noexcept {
    static const VnodeOps ops = {
        .Read    = RamFsOpsImpl::Read,
        .Write   = RamFsOpsImpl::Write,
        .Lookup  = RamFsOpsImpl::Lookup,
        .Create  = RamFsOpsImpl::Create,
        .Mkdir   = RamFsOpsImpl::Mkdir,
        .Mknod   = RamFsOpsImpl::Mknod,
        .Open    = nullptr,
        .Close   = nullptr,
        .Destroy = RamFsOpsImpl::Destroy,
        .Iterate = RamFsOpsImpl::Iterate,
    };
    return &ops;
}

} // namespace ceryx::fs::ramfs
