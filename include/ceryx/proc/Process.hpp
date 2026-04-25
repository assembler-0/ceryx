#pragma once

#include <ceryx/mm/UserAddressSpace.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>
#include <FoundationKitCxxStl/Base/Vector.hpp>
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

    /// @brief Number of direct children.
    [[nodiscard]] usize ChildCount() const noexcept { return m_children.Size(); }

    /// @brief Intrusive node for being on a parent's m_children list.
    IntrusiveDoublyLinkedListNode child_node;

    // ── Heap bookkeeping ─────────────────────────────────────────────────────

    [[nodiscard]] u64 GetHeapStart() const noexcept { return m_heap_start; }
    [[nodiscard]] u64 GetHeapEnd()   const noexcept { return m_heap_end; }
    void SetHeap(u64 start, u64 end) noexcept { m_heap_start = start; m_heap_end = end; }

    // ── File descriptors ─────────────────────────────────────────────────────

    /// @brief Allocate the next available FD slot and store the FileDescription.
    /// @return The allocated FD number, or -EMFILE if the table is full.
    Expected<u32, int> AllocateFd(RefPtr<fs::FileDescription> file) noexcept;

    /// @brief Allocate a specific FD number (for dup2 / O_CLOEXEC paths).
    /// Closes any existing description at that slot first.
    /// @return 0 on success, -EBADF if fd >= kMaxFds.
    Expected<u32, int> AllocateFdAt(u32 fd, RefPtr<fs::FileDescription> file) noexcept;

    /// @brief Retrieve the FileDescription for an open FD.
    /// @return A valid RefPtr, or null if the FD is not open.
    [[nodiscard]] RefPtr<fs::FileDescription> GetFd(u32 fd) noexcept;

    /// @brief Close an FD slot. No-op if already closed.
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

    // ── FD table limits ──────────────────────────────────────────────────────

    /// @brief Hard limit on open file descriptors per process (matches Linux default).
    static constexpr u32 kMaxFds = 1024;

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

    // ── FD table ─────────────────────────────────────────────────────────────
    //
    // Previously XArray<fs::FileDescription> — that stored raw T* pointers with
    // no ownership, causing the RefPtr passed to AllocateFd to destruct at end
    // of scope and immediately free the FileDescription.
    //
    // Now: a flat Vector of Optional<RefPtr<FileDescription>>.
    //   - Optional<RefPtr<...>> distinguishes "slot never opened" (empty Optional)
    //     from "slot closed" (Optional reset to NullOpt) — both are empty, which
    //     is correct: a closed FD is indistinguishable from one never opened.
    //   - RefPtr provides the ownership and thread-safe refcount.
    //   - Vector grows on demand; slots are never shrunk to keep FD numbers stable.
    //   - AllocateFd scans for the first empty slot (O(n), acceptable for kMaxFds=1024).
    //
    Vector<Optional<RefPtr<fs::FileDescription>>> m_fd_table;

    sigaction m_signal_actions[NSIG]{};

    // Process tree state.
    Process*              m_parent{nullptr};
    IntrusiveDoublyLinkedList m_children; ///< Children linked via child_node.
    ProcessState          m_state{ProcessState::Running};
    int                   m_exit_status{0};
    // Note: PID allocation handled by bitmap pool in Process.cpp — no static here.
};

} // namespace ceryx::proc
