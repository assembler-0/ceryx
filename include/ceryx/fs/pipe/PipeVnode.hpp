#pragma once

#include <ceryx/fs/Vnode.hpp>
#include <ceryx/fs/pipe/Pipe.hpp>
#include <ceryx/Errno.h>

namespace ceryx::fs {

class PipeVnode : public Vnode {
public:
    static Expected<RefPtr<PipeVnode>, int> Create() noexcept {
        auto* vnode = new PipeVnode();
        return RefPtr<PipeVnode>(vnode);
    }

    static constexpr u32 kPipeRead = 0x1;
    static constexpr u32 kPipeWrite = 0x2;

    RefPtr<Pipe> pipe;

    PipeVnode() noexcept : Vnode(VnodeType::Fifo, &s_ops) {
        pipe = RefPtr<Pipe>(new Pipe());
    }

private:
    static Expected<usize, int> Read(Vnode& vnode, void* buffer, usize size, usize /*offset*/) noexcept {
        auto& pv = static_cast<PipeVnode&>(vnode);
        return pv.pipe->Read(buffer, size);
    }

    static Expected<usize, int> Write(Vnode& vnode, const void* buffer, usize size, usize /*offset*/) noexcept {
        auto& pv = static_cast<PipeVnode&>(vnode);
        return pv.pipe->Write(buffer, size);
    }

    static void Open(Vnode& vnode, u32 flags) noexcept {
        auto& pv = static_cast<PipeVnode&>(vnode);
        if (flags & kPipeRead) pv.pipe->IncrementReaders();
        if (flags & kPipeWrite) pv.pipe->IncrementWriters();
    }

    static void Close(Vnode& vnode, u32 flags) noexcept {
        auto& pv = static_cast<PipeVnode&>(vnode);
        if (flags & kPipeRead) pv.pipe->DecrementReaders();
        if (flags & kPipeWrite) pv.pipe->DecrementWriters();
    }

    static void Destroy(Vnode& vnode) noexcept {
        auto& pv = static_cast<PipeVnode&>(vnode);
        pv.pipe.Reset();
        delete &pv;
    }

    static constexpr VnodeOps s_ops = {
        .Read    = Read,
        .Write   = Write,
        .Lookup  = nullptr,
        .Create  = nullptr,
        .Mkdir   = nullptr,
        .Mknod   = nullptr,
        .Open    = Open,
        .Close   = Close,
        .Destroy = Destroy,
        .Iterate = nullptr,
    };
};

// Define s_ops in a way that works for this header-only implementation if needed, 
// or move to a .cpp if linking errors occur.
inline constexpr VnodeOps PipeVnode::s_ops;

} // namespace ceryx::fs
