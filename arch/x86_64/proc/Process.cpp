#include <ceryx/proc/Process.hpp>
#include <ceryx/mm/UserAddressSpace.hpp>
#include <FoundationKitMemory/Heap/GlobalAllocator.hpp>

namespace ceryx::proc {

static u64 s_next_pid = 1;

Expected<Process*, FoundationKitMemory::MemoryError> Process::Create() noexcept {
    auto uas_res = mm::UserAddressSpace::Create();
    if (!uas_res.HasValue()) return Unexpected(uas_res.Error());
    
    Process* p = new Process(s_next_pid++, uas_res.Value());
    return p;
}

void Process::Destroy() noexcept {
    m_address_space->Destroy();
    delete this;
}

} // namespace ceryx::proc
