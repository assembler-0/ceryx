#pragma once

#include <FoundationKitCxxStl/Structure/RingBuffer.hpp>
#include <FoundationKitCxxStl/Sync/Mutex.hpp>
#include <FoundationKitCxxStl/Sync/ConditionVariable.hpp>
#include <FoundationKitCxxStl/Base/RefCounted.hpp>

namespace ceryx::fs {

using namespace FoundationKitCxxStl;
using namespace FoundationKitCxxStl::Sync;
using namespace FoundationKitCxxStl::Structure;

/// @brief Core pipe object shared between read and write ends.
class Pipe : public RefCounted<Pipe> {
public:
    static constexpr usize kDefaultCapacity = 64 * 1024; // 64KB

    explicit Pipe(usize capacity = kDefaultCapacity) noexcept
        : m_buffer(capacity) {}

    Expected<usize, int> Read(void* buffer, usize size) noexcept {
        UniqueLock guard(m_lock);
        
        while (m_buffer.Empty()) {
            if (m_writers == 0) return 0; // EOF: No one left to write
            m_can_read.Wait(guard);
        }

        u8* dest = static_cast<u8*>(buffer);
        usize read_bytes = 0;
        while (read_bytes < size) {
            auto val = m_buffer.Pop();
            if (!val) break;
            dest[read_bytes++] = *val;
        }

        if (read_bytes > 0) {
            m_can_write.NotifyAll();
        }

        return read_bytes;
    }

    Expected<usize, int> Write(const void* buffer, usize size) noexcept {
        UniqueLock guard(m_lock);

        if (m_readers == 0) {
            // TODO: In a real UNIX, this would send SIGPIPE.
            return -EPIPE;
        }

        const u8* src = static_cast<const u8*>(buffer);
        usize written_bytes = 0;

        while (written_bytes < size) {
            while (m_buffer.Size() >= m_buffer.Capacity()) {
                if (m_readers == 0) return -EPIPE;
                m_can_write.Wait(guard);
            }

            while (written_bytes < size && m_buffer.Push(src[written_bytes])) {
                written_bytes++;
            }

            if (written_bytes > 0) {
                m_can_read.NotifyAll();
            }
        }

        return written_bytes;
    }

    void Destroy() const noexcept { delete this; }

    void IncrementReaders() noexcept { m_readers++; }
    void DecrementReaders() noexcept { 
        m_readers--; 
        if (m_readers == 0) m_can_write.NotifyAll(); 
    }

    void IncrementWriters() noexcept { m_writers++; }
    void DecrementWriters() noexcept { 
        m_writers--; 
        if (m_writers == 0) m_can_read.NotifyAll(); 
    }

private:
    DynamicRingBuffer<u8> m_buffer;
    Mutex m_lock;
    ConditionVariable m_can_read;
    ConditionVariable m_can_write;

    usize m_readers = 0;
    usize m_writers = 0;
};

} // namespace ceryx::fs
