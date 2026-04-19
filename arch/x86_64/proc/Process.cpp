#include <ceryx/proc/Process.hpp>
#include <ceryx/mm/UserAddressSpace.hpp>
#include <ceryx/fs/Vfs.hpp>
#include <ceryx/proc/ElfLoader.hpp>
#include <ceryx/proc/Thread.hpp>
#include <ceryx/proc/Scheduler.hpp>
#include <FoundationKitMemory/Heap/GlobalAllocator.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitCxxStl/Structure/AtomicBitmap.hpp>
#include <ceryx/Errno.h>

namespace ceryx::proc {

// ── PID allocator ──────────────────────────────────────────────────────────────
// Bitmap-based recycling pool. PID 0 = swapper (never issued).
// PID 1 = init (issued once, never recycled). Max live PID = kMaxPid - 1.

namespace {

constexpr u32 kMaxPid = 32768;
static FoundationKitCxxStl::Structure::AtomicBitmap<kMaxPid> s_pid_bitmap;

/// Mark `pid` as in-use. Returns false if already allocated.
bool PidMark(u32 pid) noexcept {
    if (pid >= kMaxPid) return false;
    if (s_pid_bitmap.Test(pid)) return false;
    s_pid_bitmap.Set(pid);
    return true;
}

/// Release `pid` back to the pool.
void PidFree(u32 pid) noexcept {
    // PID 0 and PID 1 (init) are never recycled.
    if (pid < 2 || pid >= kMaxPid) return;
    s_pid_bitmap.Reset(pid);
}

/// Allocate the next free PID using the lock-free bitmap.
Expected<u32, int> PidAlloc() noexcept {
    usize bit = s_pid_bitmap.FindFirstUnsetAndSet();
    if (bit >= kMaxPid) {
        return Unexpected<int>(-EAGAIN);
    }
    return static_cast<u32>(bit);
}

} // anonymous namespace

// ── Create / Spawn ─────────────────────────────────────────────────────────────

Expected<Process*, FoundationKitMemory::MemoryError>
Process::Create(RefPtr<fs::Vnode> root, RefPtr<fs::Vnode> cwd) noexcept {
    auto uas_res = mm::UserAddressSpace::Create();
    if (!uas_res.HasValue()) return Unexpected(uas_res.Error());

    // Reserve PID 0 (swapper) if this is the very first process creation.
    static bool boot_init = []() {
        s_pid_bitmap.Set(0); // Reserve swapper
        return true;
    }();
    (void)boot_init;

    auto pid_res = PidAlloc();
    u32 pid = pid_res.HasValue() ? pid_res.Value() : 0;

    Process* p = new Process(pid, uas_res.Value(), root, cwd);
    return p;
}

Expected<Process*, int> Process::Spawn(RefPtr<fs::Vnode> root, StringView path) noexcept {
    auto vnode_res = fs::Vfs::PathToVnode(root, path);
    if (!vnode_res) return Unexpected<int>(vnode_res.Error()); 
    RefPtr<fs::Vnode> vnode = vnode_res.Value();

    auto proc_res = Create(root, root);
    if (!proc_res) return Unexpected<int>(-ENOMEM);
    Process* p = proc_res.Value();

    // Map the PIE executable at 0x400000 (bias). 
    // Since init.ld base is 0, this results in the same layout but with kernel-controlled placement.
    auto load_res = ElfLoader::Load(*p, *vnode, 0x400000);
    if (!load_res) {
        p->Destroy();
        return Unexpected<int>(load_res.Error());
    }

    auto result = load_res.Value();
    p->SetHeap(result.heap_base, result.heap_base);

    Thread* thread = new Thread(p, false);
    thread->InitializeUserStack(result.entry_point, result.stack_top);
    thread->SetName("user_proc");

    Scheduler::AddThread(thread, 0);
    return p;
}

// ── Fork ──────────────────────────────────────────────────────────────────────

Expected<Process*, int> Process::Fork(cpu::InterruptFrame* regs) noexcept {
    // 1. Clone address space with FoundationKitMemory CoW semantics.
    auto uas_res = m_address_space->Fork();
    if (!uas_res.HasValue()) return Unexpected<int>(-ENOMEM);

    // 2. Allocate a recycled PID for the child.
    auto pid_res = PidAlloc();
    if (!pid_res.HasValue()) {
        uas_res.Value()->Destroy();
        return Unexpected<int>(pid_res.Error());
    }

    // 3. Allocate child Process object.
    Process* child = new Process(pid_res.Value(), uas_res.Value(), m_root, m_cwd);
    if (!child) {
        PidFree(pid_res.Value());
        uas_res.Value()->Destroy();
        return Unexpected<int>(-ENOMEM);
    }

    // 4. Clone FD table: each FileDescription gets an extra reference.
    m_fd_table.ForEach([&](usize fd, fs::FileDescription& file) {
        file.AddRef();
        (void)child->m_fd_table.Store(static_cast<u32>(fd), &file);
    });
    child->m_next_fd = m_next_fd;

    // 5. Clone signal disposition table.
    for (int i = 0; i < NSIG; ++i) {
        child->m_signal_actions[i] = m_signal_actions[i];
    }

    // 6. Inherit heap bookkeeping (brk pointer).
    child->m_heap_start = m_heap_start;
    child->m_heap_end   = m_heap_end;

    // 7. Wire process tree.
    child->m_parent = this;
    m_children.PushBack(&child->child_node);

    // 8. Create child thread.
    Thread* child_thread = new Thread(child, false);
    if (!child_thread) {
        m_children.Remove(&child->child_node);
        child->Reap();
        return Unexpected<int>(-ENOMEM);
    }
    child_thread->SetName("fork_child");
    child_thread->InitializeForkStack(*regs);

    Scheduler::AddThread(child_thread, 0);

    FK_LOG_INFO("ceryx::proc: forked PID {} → child PID {}", m_pid, child->m_pid);
    return child;
}

// ── Exit / Reap / Destroy ─────────────────────────────────────────────────────

void Process::Exit(int status) noexcept {
    // 1. Close all open file descriptors.
    m_fd_table.ForEach([&](usize /*fd*/, fs::FileDescription& file) {
        file.Release();
    });
    m_fd_table.Clear();

    // 2. Transition to Zombie.
    m_state       = ProcessState::Zombie;
    m_exit_status = status;

    // 3. Wake parent blocked in waitpid (uses `this` as the channel).
    if (m_parent) {
        Scheduler::Wake(this, /*wake_all=*/false);
    }
}

void Process::Reap() noexcept {
    // Return the PID to the recycling pool.
    PidFree(static_cast<u32>(m_pid));

    // Free the address space and the Process object itself.
    m_address_space->Destroy();
    delete this;
}

void Process::Destroy() noexcept {
    // Immediate teardown: error/early-exit paths. PID freed if non-init.
    PidFree(static_cast<u32>(m_pid));
    m_fd_table.ForEach([&](usize /*fd*/, fs::FileDescription& file) {
        file.Release();
    });
    m_fd_table.Clear();
    m_address_space->Destroy();
    delete this;
}

// ── Process tree helpers ──────────────────────────────────────────────────────

Process* Process::FindChild(u64 pid) noexcept {
    // pid == 0 means "any child".
    auto* node = m_children.Begin();
    while (node != m_children.End()) {
        auto* child = ContainerOf<Process, &Process::child_node>(node);
        if (pid == 0 || child->m_pid == pid) return child;
        node = node->next;
    }
    return nullptr;
}

void Process::RemoveChild(Process* child) noexcept {
    m_children.Remove(&child->child_node);
}

// ── File descriptor management ────────────────────────────────────────────────

Expected<u32, int> Process::AllocateFd(RefPtr<fs::FileDescription> file) noexcept {
    if (!file) return Unexpected<int>(-EBADF);

    u32 fd = m_next_fd++;

    file->AddRef();
    if (!m_fd_table.Store(fd, file.Get())) {
        file->Release();
        return Unexpected<int>(-ENOMEM);
    }

    return fd;
}

RefPtr<fs::FileDescription> Process::GetFd(u32 fd) noexcept {
    fs::FileDescription* desc = m_fd_table.Load(fd);
    if (!desc) return nullptr;

    desc->AddRef();
    return RefPtr<fs::FileDescription>(desc);
}

void Process::FreeFd(u32 fd) noexcept {
    fs::FileDescription* desc = m_fd_table.Load(fd);
    if (desc) {
        m_fd_table.Erase(fd);
        desc->Release();
    }
}

} // namespace ceryx::proc
