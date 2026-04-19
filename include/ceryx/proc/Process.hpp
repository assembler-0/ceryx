#pragma once

#include <ceryx/mm/UserAddressSpace.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>

namespace ceryx::proc {

using namespace FoundationKitCxxStl;

/// @brief Represents a running process in the system.
class Process {
public:
    /// @brief Create a new process.
    static FoundationKitCxxStl::Expected<Process*, FoundationKitMemory::MemoryError> Create() noexcept;

    /// @brief Destroy the process.
    void Destroy() noexcept;

    /// @brief Get the process address space.
    [[nodiscard]] mm::UserAddressSpace& GetAddressSpace() noexcept { return *m_address_space; }

    /// @brief Get the unique process identifier.
    [[nodiscard]] u64 GetPid() const noexcept { return m_pid; }

private:
    Process(u64 pid, mm::UserAddressSpace* address_space) noexcept
        : m_pid(pid), m_address_space(address_space) {}

    u64 m_pid;
    mm::UserAddressSpace* m_address_space;
};

} // namespace ceryx::proc
