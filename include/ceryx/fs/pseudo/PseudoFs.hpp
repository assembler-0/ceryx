#pragma once

#include <ceryx/fs/Vnode.hpp>
#include <FoundationKitCxxStl/Base/String.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <ceryx/Errno.h>

namespace ceryx::fs::pseudo {

using namespace FoundationKitCxxStl;

/// @brief Read callback signature.
using PseudoReadCallback = Expected<usize, int> (*)(void* buffer, usize size, usize offset) noexcept;

/// @brief Write callback signature.
using PseudoWriteCallback = Expected<usize, int> (*)(const void* buffer, usize size, usize offset) noexcept;

class PseudoFsNode : public Vnode {
public:
    StringView name;
    PseudoReadCallback read_cb;
    PseudoWriteCallback write_cb;

    PseudoFsNode(const VnodeOps* o, StringView n, PseudoReadCallback r_cb, PseudoWriteCallback w_cb = nullptr) noexcept
        : Vnode(VnodeType::Regular, o), name(n), read_cb(r_cb), write_cb(w_cb) {}
};

struct PseudoFsOpsImpl {
    static Expected<usize, int> Read(Vnode& vnode, void* buffer, usize size, usize offset) noexcept {
        auto& pseudo_node = static_cast<PseudoFsNode&>(vnode);
        if (pseudo_node.read_cb) {
            return pseudo_node.read_cb(buffer, size, offset);
        }
        return 0;
    }

    static Expected<usize, int> Write(Vnode& vnode, const void* buffer, usize size, usize offset) noexcept {
        auto& pseudo_node = static_cast<PseudoFsNode&>(vnode);
        if (pseudo_node.write_cb) {
            return pseudo_node.write_cb(buffer, size, offset);
        }
        return Unexpected<int>(-EPERM);
    }
    
    static Expected<RefPtr<Vnode>, int> Lookup(Vnode& dir, StringView name) noexcept {
        return Unexpected<int>(-ENOENT);
    }

    static Expected<RefPtr<Vnode>, int> Create(Vnode& dir, StringView name) noexcept {
        return Unexpected<int>(-EPERM);
    }

    static Expected<RefPtr<Vnode>, int> Mkdir(Vnode& dir, StringView name) noexcept {
        return Unexpected<int>(-EPERM);
    }

    static Expected<RefPtr<Vnode>, int> Mknod(Vnode& dir, StringView name, VnodeType type) noexcept {
        return Unexpected<int>(-EPERM);
    }

    static void Destroy(Vnode& vnode) noexcept {
        delete static_cast<PseudoFsNode*>(&vnode);
    }

    static const VnodeOps* GetOps() noexcept {
        static const VnodeOps ops = {
            .Read = PseudoFsOpsImpl::Read,
            .Write = PseudoFsOpsImpl::Write,
            .Lookup = PseudoFsOpsImpl::Lookup,
            .Create = PseudoFsOpsImpl::Create,
            .Mkdir = PseudoFsOpsImpl::Mkdir,
            .Mknod = PseudoFsOpsImpl::Mknod,
            .Destroy = PseudoFsOpsImpl::Destroy,
        };
        return &ops;
    }
};

} // namespace ceryx::fs::pseudo
