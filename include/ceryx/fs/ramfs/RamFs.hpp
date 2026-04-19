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
    StringView name;
    FoundationKitMemory::SharedPtr<VmObject> vmo;
    Vector<RefPtr<RamFsNode>> children;

    RamFsNode(VnodeType t, const VnodeOps* o, StringView node_name) noexcept
        : Vnode(t, o), name(node_name) {
        if (t == VnodeType::Regular) {
            vmo = FoundationKitMemory::MakeShared<VmObject>();
        }
    }
    
    // Deallocation is handled by the ops->Destroy table entry.
};

/// @brief Implementation of VnodeOps for RamFs
struct RamFsOpsImpl {
    static void Destroy(Vnode& vnode) noexcept {
        auto* ram_node = static_cast<RamFsNode*>(&vnode);
        if (ram_node->vmo) {
            ram_node->vmo->ForEachPage([](const VmPage& p) noexcept {
                ceryx::mm::MemoryManager::GetKmm().FreeFolio(Folio::FromSinglePage(&ceryx::mm::MemoryManager::GetPdArray().Get(p.pfn)));
                delete &p;
            });
        }
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
            if (child->name == name) return RefPtr<Vnode>(child);
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
        
        if (!ram_dir.children.PushBack(node)) {
            return Unexpected<int>(-ENOMEM);
        }
        return RefPtr<Vnode>(node);
    }

    static Expected<RefPtr<Vnode>, int> Mkdir(Vnode& dir, StringView name) noexcept {
        if (dir.type != VnodeType::Directory) return Unexpected<int>(-ENOTDIR);
        auto& ram_dir = static_cast<RamFsNode&>(dir);

        for (auto& child : ram_dir.children) {
            if (child->name == name) return RefPtr<Vnode>(child);
        }

        auto node = RefPtr<RamFsNode>(new RamFsNode(VnodeType::Directory, GetOps(), name));
        if (!node) return Unexpected<int>(-ENOMEM);

        if (!ram_dir.children.PushBack(node)) {
            return Unexpected<int>(-ENOMEM);
        }
        return RefPtr<Vnode>(node);
    }

    static Expected<RefPtr<Vnode>, int> Mknod(Vnode& dir, StringView name, VnodeType type) noexcept {
        if (dir.type != VnodeType::Directory) return Unexpected<int>(-ENOTDIR);
        auto& ram_dir = static_cast<RamFsNode&>(dir);

        auto node = RefPtr<RamFsNode>(new RamFsNode(type, GetOps(), name));
        if (!node) return Unexpected<int>(-ENOMEM);

        if (!ram_dir.children.PushBack(node)) {
            return Unexpected<int>(-ENOMEM);
        }
        return RefPtr<Vnode>(node);
    }
};

inline const VnodeOps* RamFsOpsImpl::GetOps() noexcept {
    static const VnodeOps ops = {
        .Read = RamFsOpsImpl::Read,
        .Write = RamFsOpsImpl::Write,
        .Lookup = RamFsOpsImpl::Lookup,
        .Create = RamFsOpsImpl::Create,
        .Mkdir = RamFsOpsImpl::Mkdir,
        .Mknod = RamFsOpsImpl::Mknod,
        .Destroy = RamFsOpsImpl::Destroy,
    };
    return &ops;
}

} // namespace ceryx::fs::ramfs
