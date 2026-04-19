#pragma once

#include <ceryx/fs/Vnode.hpp>
#include <FoundationKitCxxStl/Base/RefCounted.hpp>
#include <FoundationKitCxxStl/Base/RefPtr.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <ceryx/Errno.h>

namespace ceryx::fs {

using namespace FoundationKitCxxStl;

/// @brief Represents an open file instance (file descriptor metadata).
class FileDescription : public RefCounted<FileDescription> {
public:
    RefPtr<Vnode> vnode;
    usize offset{0};
    u32 flags{0};

    /// @brief Construct a new FileDescription for a given Vnode.
    explicit FileDescription(RefPtr<Vnode> v, u32 f = 0) noexcept : vnode(v), flags(f) {
        if (vnode && vnode->ops && vnode->ops->Open) {
            vnode->ops->Open(*vnode, flags);
        }
    }

    void Destroy() const noexcept {
        if (vnode && vnode->ops && vnode->ops->Close) {
            vnode->ops->Close(*vnode, flags);
        }
        delete this;
    }

    /// @brief Read data from the file.
    Expected<usize, int> Read(void* buffer, usize size) noexcept {
        if (!vnode || !vnode->ops) return Unexpected<int>(-EBADF);
        auto result = vnode->ops->Read(*vnode, buffer, size, offset);
        if (result) {
            offset += result.Value();
        }
        return result;
    }

    /// @brief Write data to the file.
    Expected<usize, int> Write(const void* buffer, usize size) noexcept {
        if (!vnode || !vnode->ops) return Unexpected<int>(-EBADF);
        auto result = vnode->ops->Write(*vnode, buffer, size, offset);
        if (result) {
            offset += result.Value();
        }
        return result;
    }
};

} // namespace ceryx::fs
