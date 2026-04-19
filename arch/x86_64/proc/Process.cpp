#include <ceryx/proc/Process.hpp>
#include <ceryx/mm/UserAddressSpace.hpp>
#include <FoundationKitMemory/Heap/GlobalAllocator.hpp>
#include <ceryx/Errno.h>

namespace ceryx::proc {

static u64 s_next_pid = 1;

Expected<Process*, FoundationKitMemory::MemoryError> Process::Create(RefPtr<fs::Vnode> root, RefPtr<fs::Vnode> cwd) noexcept {
    auto uas_res = mm::UserAddressSpace::Create();
    if (!uas_res.HasValue()) return Unexpected(uas_res.Error());
    
    Process* p = new Process(s_next_pid++, uas_res.Value(), root, cwd);
    return p;
}

void Process::Destroy() noexcept {
    m_fd_table.ForEach([&](usize /*fd*/, fs::FileDescription& file) {
        file.Release();
    });
    m_fd_table.Clear();
    m_address_space->Destroy();
    delete this;
}

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
