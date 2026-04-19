#pragma once

#include <ceryx/mm/UserAddressSpace.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Structure/XArray.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveDoublyLinkedList.hpp>
#include <ceryx/fs/FileDescription.hpp>
#include <ceryx/fs/Vnode.hpp>
#include <ceryx/proc/Signal.hpp>
#include <ceryx/cpu/InterruptFrame.hpp>

namespace ceryx::proc {

using namespace FoundationKitCxxStl;
using namespace FoundationKitCxxStl::Structure;

/// @brief Process lifecycle state.
enum class ProcessState {
    Running, ///< Process is actively executing.
    Zombie,  ///< Process has exited; parent has not yet called waitpid().
};

class Thread;

/// @brief Represents a running process in the system.
class Process {
public:
    /// @brief Create a new process with an empty address space.
    static Expected<Process*, FoundationKitMemory::MemoryError>
        Create(RefPtr<fs::Vnode> root, RefPtr<fs::Vnode> cwd) noexcept;

    /// @brief Spawn a new process from an ELF binary at `path`.
    static Expected<Process*, int>
        Spawn(RefPtr<fs::Vnode> root, StringView path) noexcept;

    /// @brief Fork this process: CoW address space clone + child thread.
    /// @param regs Parent's InterruptFrame at the point of the fork() syscall.
    /// @return The new child Process* (added to scheduler), or an errno.
    [[nodiscard]] Expected<Process*, int> Fork(cpu::InterruptFrame* regs) noexcept;

    /// @brief Mark process as exited, close FDs, transition to Zombie, wake parent.
    /// Does NOT delete the Process object — that is done by waitpid via Reap().
    void Exit(int status) noexcept;

    /// @brief Final memory cleanup: free address space and delete this.
    /// Called by waitpid after Exit() has been observed.
    void Reap() noexcept;

    /// @brief Immediate teardown (no zombie state). Used for error paths.
    void Destroy() noexcept;

    // ── Address space ────────────────────────────────────────────────────────

    [[nodiscard]] mm::UserAddressSpace& GetAddressSpace() noexcept { return *m_address_space; }

    [[nodiscard]] mm::UserAddressSpace* SetAddressSpace(mm::UserAddressSpace* s) noexcept {
        mm::UserAddressSpace* old = m_address_space;
        m_address_space = s;
        return old;
    }

    // ── Identity ─────────────────────────────────────────────────────────────

    [[nodiscard]] u64 GetPid()  const noexcept { return m_pid; }

    // ── Process tree ─────────────────────────────────────────────────────────

    [[nodiscard]] Process* GetParent() const noexcept { return m_parent; }
    void SetParent(Process* p) noexcept { m_parent = p; }

    [[nodiscard]] ProcessState GetState() const noexcept { return m_state; }
    [[nodiscard]] bool IsZombie()  const noexcept { return m_state == ProcessState::Zombie; }
    [[nodiscard]] int  GetExitStatus() const noexcept { return m_exit_status; }

    /// @brief Find a direct child by PID; pid=0 means any child.
    [[nodiscard]] Process* FindChild(u64 pid) noexcept;

    /// @brief Remove a child from the children list (called after Reap).
    void RemoveChild(Process* child) noexcept;

    /// @brief Intrusive node for being on a parent's m_children list.
    IntrusiveDoublyLinkedListNode child_node;

    // ── Heap bookkeeping ─────────────────────────────────────────────────────

    [[nodiscard]] u64 GetHeapStart() const noexcept { return m_heap_start; }
    [[nodiscard]] u64 GetHeapEnd()   const noexcept { return m_heap_end; }
    void SetHeap(u64 start, u64 end) noexcept { m_heap_start = start; m_heap_end = end; }

    // ── File descriptors ─────────────────────────────────────────────────────

    Expected<u32, int> AllocateFd(RefPtr<fs::FileDescription> file) noexcept;
    RefPtr<fs::FileDescription> GetFd(u32 fd) noexcept;
    void FreeFd(u32 fd) noexcept;

    // ── Filesystem context ───────────────────────────────────────────────────

    [[nodiscard]] RefPtr<fs::Vnode> GetRoot() const noexcept { return m_root; }
    [[nodiscard]] RefPtr<fs::Vnode> GetCwd()  const noexcept { return m_cwd; }

    // ── Signals ──────────────────────────────────────────────────────────────

    [[nodiscard]] const sigaction& GetSignalAction(int sig) const noexcept {
        if (sig < 1 || sig >= NSIG) return m_signal_actions[0];
        return m_signal_actions[sig - 1];
    }
    [[nodiscard]] sigaction& GetSignalAction(int sig) noexcept {
        if (sig < 1 || sig >= NSIG) return m_signal_actions[0];
        return m_signal_actions[sig - 1];
    }

private:
    Process(u64 pid, mm::UserAddressSpace* uas,
            RefPtr<fs::Vnode> root, RefPtr<fs::Vnode> cwd) noexcept
        : m_pid(pid), m_address_space(uas), m_root(root), m_cwd(cwd) {}

    u64  m_pid;
    u64  m_heap_start{0};
    u64  m_heap_end{0};
    mm::UserAddressSpace* m_address_space;
    RefPtr<fs::Vnode>     m_root;
    RefPtr<fs::Vnode>     m_cwd;
    Structure::XArray<fs::FileDescription> m_fd_table;
    u32 m_next_fd{0};
    sigaction m_signal_actions[NSIG]{};

    // Process tree state.
    Process*              m_parent{nullptr};
    IntrusiveDoublyLinkedList m_children; ///< Children linked via child_node.
    ProcessState          m_state{ProcessState::Running};
    int                   m_exit_status{0};
    // Note: PID allocation handled by bitmap pool in Process.cpp — no static here.
};

} // namespace ceryx::proc
