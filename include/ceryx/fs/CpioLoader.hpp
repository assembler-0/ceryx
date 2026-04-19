#pragma once

#include <FoundationKitCxxStl/Base/StringView.hpp>
#include <FoundationKitMemory/Core/MemoryOperations.hpp>
#include <ceryx/fs/Vfs.hpp>
#include <ceryx/fs/Vnode.hpp>

namespace ceryx::fs {

    using namespace FoundationKitCxxStl;

    class CpioLoader {
    public:
        struct [[gnu::packed]] CpioHeader {
            char magic[6];
            char ino[8];
            char mode[8];
            char uid[8];
            char gid[8];
            char nlink[8];
            char mtime[8];
            char filesize[8];
            char devmajor[8];
            char devminor[8];
            char rdevmajor[8];
            char rdevminor[8];
            char namesize[8];
            char check[8];
        };

        static void Populate(RefPtr<Vnode> root, void *address, usize size) noexcept {
            u8 *ptr = static_cast<u8 *>(address);
            u8 *end = ptr + size;

            while (ptr < end) {
                if (MemoryCompare(ptr, "070701", 6) != 0)
                    break;

                CpioHeader *header = reinterpret_cast<CpioHeader *>(ptr);

                usize filesize = ParseHex(header->filesize, 8);
                usize namesize = ParseHex(header->namesize, 8);
                u32 mode = static_cast<u32>(ParseHex(header->mode, 8));

                char *filename = reinterpret_cast<char *>(ptr + sizeof(CpioHeader));
                if (MemoryCompare(filename, "TRAILER!!!", 10) == 0)
                    break;

                ptr += sizeof(CpioHeader) + namesize;
                // Alignment to 4 bytes
                ptr = reinterpret_cast<u8 *>((reinterpret_cast<uptr>(ptr) + 3) & ~3ULL);

                void *data = ptr;
                ptr += filesize;
                ptr = reinterpret_cast<u8 *>((reinterpret_cast<uptr>(ptr) + 3) & ~3ULL);

                CreateEntry(root, StringView(filename, namesize - 1), mode, data, filesize);
            }
        }

    private:
        static usize ParseHex(const char *str, usize len) noexcept {
            usize val = 0;
            for (usize i = 0; i < len; ++i) {
                val <<= 4;
                if (str[i] >= '0' && str[i] <= '9')
                    val |= (str[i] - '0');
                else if (str[i] >= 'a' && str[i] <= 'f')
                    val |= (str[i] - 'a' + 10);
                else if (str[i] >= 'A' && str[i] <= 'F')
                    val |= (str[i] - 'A' + 10);
            }
            return val;
        }

        static void CreateEntry(RefPtr<Vnode> root, StringView path, u32 mode, void *data, usize size) noexcept {
            if (path == ".")
                return;

            // Extract directories from path
            RefPtr<Vnode> current = root;
            usize offset = 0;

            while (true) {
                usize next_slash = path.Find('/', offset);
                if (next_slash == StringView::NPos)
                    break;

                StringView dir_name = path.SubView(offset, next_slash - offset);
                offset = next_slash + 1;

                if (dir_name.Empty() || dir_name == ".")
                    continue;

                auto lookup_res = current->ops->Lookup(*current, dir_name);
                if (lookup_res) {
                    current = lookup_res.Value();
                } else {
                    auto mkdir_res = current->ops->Mkdir(*current, dir_name);
                    if (!mkdir_res)
                        return;
                    current = mkdir_res.Value();
                }
            }

            StringView leaf_name = path.SubView(offset);
            if (leaf_name.Empty())
                return;

            // Mode check for directory vs file
            if ((mode & 0xF000) == 0x4000) { // S_IFDIR
                current->ops->Mkdir(*current, leaf_name);
            } else if ((mode & 0xF000) == 0x8000) { // S_IFREG
                auto file_res = current->ops->Create(*current, leaf_name);
                if (file_res) {
                    file_res.Value()->ops->Write(*file_res.Value(), data, size, 0);
                }
            }
        }
    };
} // namespace ceryx::fs
