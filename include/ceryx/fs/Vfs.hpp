#pragma once

#include <ceryx/fs/Vnode.hpp>
#include <FoundationKitCxxStl/Base/StringView.hpp>

namespace ceryx::fs {

using namespace FoundationKitCxxStl;

class Vfs {
public:
    /// @brief Resolve a path string to a Vnode starting from root.
    /// @param root The root vnode to start from.
    /// @param path The absolute or relative path.
    /// @return The resolved vnode or an error code.
    static Expected<RefPtr<Vnode>, int> PathToVnode(RefPtr<Vnode> root, StringView path) noexcept {
        if (path.Empty()) return root;
        
        RefPtr<Vnode> current = root;
        usize offset = 0;
        
        // Skip leading slash
        if (path[0] == '/') {
            offset = 1;
        }

        while (offset < path.Size()) {
            usize next_slash = path.Find('/', offset);
            StringView component;
            
            if (next_slash == StringView::NPos) {
                component = path.SubView(offset);
                offset = path.Size();
            } else {
                component = path.SubView(offset, next_slash - offset);
                offset = next_slash + 1;
            }

            if (component.Empty() || component == ".") {
                continue;
            }
            
            // TODO: Handle ".." in the future.

            if (!current->IsDirectory()) return Unexpected<int>(-ENOTDIR);
            
            auto res = current->ops->Lookup(*current, component);
            if (!res) return Unexpected<int>(res.Error());
            current = res.Value();
        }
        
        return current;
    }
};

} // namespace ceryx::fs
