#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/RefCounted.hpp>
#include <FoundationKitCxxStl/Base/RefPtr.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>
#include <FoundationKitCxxStl/Base/StringView.hpp>

namespace ceryx::fs {

using namespace FoundationKitCxxStl;

enum class VnodeType {
    Regular, Directory, BlockDevice, CharDevice, SymLink, Socket, Fifo
};

class Vnode;

/// @brief Vnode operational interface (Function Pointer Table)
struct VnodeOps {
    /// @brief Read data from the vnode.
    Expected<usize, int> (*Read)(Vnode& vnode, void* buffer, usize size, usize offset) noexcept;
    
    /// @brief Write data to the vnode.
    Expected<usize, int> (*Write)(Vnode& vnode, const void* buffer, usize size, usize offset) noexcept;
    
    /// @brief Lookup a child vnode by name (for directories).
    Expected<RefPtr<Vnode>, int> (*Lookup)(Vnode& dir, StringView name) noexcept;

    /// @brief Create a regular file in the directory.
    Expected<RefPtr<Vnode>, int> (*Create)(Vnode& dir, StringView name) noexcept;

    /// @brief Create a directory in the directory.
    Expected<RefPtr<Vnode>, int> (*Mkdir)(Vnode& dir, StringView name) noexcept;

    /// @brief Create a special device node.
    Expected<RefPtr<Vnode>, int> (*Mknod)(Vnode& dir, StringView name, VnodeType type) noexcept;

    /// @brief Optional cleanup for the vnode when it's being destroyed.
    void (*Destroy)(Vnode& vnode) noexcept;
};

/// @brief Represents a file system node.
class Vnode : public RefCounted<Vnode> {
public:
    VnodeType type;
    const VnodeOps* ops; // Operations table

    // Generic metadata
    usize size{0};
    u32 permissions{0};

    // No virtual methods to avoid vtable issues in early boot.
    constexpr Vnode(VnodeType t, const VnodeOps* o) noexcept 
        : type(t), ops(o) {}

    void Destroy() const noexcept {
        if (ops && ops->Destroy) {
            ops->Destroy(const_cast<Vnode&>(*this));
        } else {
            delete this;
        }
    }

    [[nodiscard]] bool IsDirectory() const noexcept { return type == VnodeType::Directory; }
};

} // namespace ceryx::fs
