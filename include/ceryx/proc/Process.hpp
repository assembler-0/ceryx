#pragma once

#include <ceryx/mm/UserAddressSpace.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Structure/XArray.hpp>
#include <ceryx/fs/FileDescription.hpp>
#include <ceryx/fs/Vnode.hpp>

namespace ceryx::proc {

using namespace FoundationKitCxxStl;

/// @brief Represents a running process in the system.
class Process {
public:
    /// @brief Create a new process.
    static FoundationKitCxxStl::Expected<Process*, FoundationKitMemory::MemoryError> Create(RefPtr<fs::Vnode> root, RefPtr<fs::Vnode> cwd) noexcept;

    /// @brief Destroy the process.
    void Destroy() noexcept;

    /// @brief Get the process address space.
    [[nodiscard]] mm::UserAddressSpace& GetAddressSpace() noexcept { return *m_address_space; }

    /// @brief Get the unique process identifier.
    [[nodiscard]] u64 GetPid() const noexcept { return m_pid; }

    /// @brief Allocate an FD for the given FileDescription.
    Expected<u32, int> AllocateFd(RefPtr<fs::FileDescription> file) noexcept;

    /// @brief Retrieve an FD's FileDescription.
    RefPtr<fs::FileDescription> GetFd(u32 fd) noexcept;

    /// @brief Close an FD.
    void FreeFd(u32 fd) noexcept;

    [[nodiscard]] RefPtr<fs::Vnode> GetRoot() const noexcept { return m_root; }
    [[nodiscard]] RefPtr<fs::Vnode> GetCwd() const noexcept { return m_cwd; }

private:
    Process(u64 pid, mm::UserAddressSpace* address_space, RefPtr<fs::Vnode> root, RefPtr<fs::Vnode> cwd) noexcept
        : m_pid(pid), m_address_space(address_space), m_root(root), m_cwd(cwd) {}

    u64 m_pid;
    mm::UserAddressSpace* m_address_space;
    RefPtr<fs::Vnode> m_root;
    RefPtr<fs::Vnode> m_cwd;
    Structure::XArray<fs::FileDescription> m_fd_table;
    u32 m_next_fd{0};
};

} // namespace ceryx::proc
