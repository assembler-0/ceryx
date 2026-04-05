#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>

using namespace FoundationKitCxxStl;

namespace ceryx::arch::x86_64::pio {

    static FOUNDATIONKITCXXSTL_ALWAYS_INLINE void outb(u16 port, u8 val) {
        __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
    }

    static FOUNDATIONKITCXXSTL_ALWAYS_INLINE u8 inb(u16 port) {
        u8 ret;
        __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
        return ret;
    }

    static FOUNDATIONKITCXXSTL_ALWAYS_INLINE void outw(u16 port, u16 val) {
        __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port) : "memory");
    }

    static FOUNDATIONKITCXXSTL_ALWAYS_INLINE u16 inw(u16 port) {
        u16 ret;
        __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
        return ret;
    }

    static FOUNDATIONKITCXXSTL_ALWAYS_INLINE void outl(u16 port, u32 val) {
        __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port) : "memory");
    }

    static FOUNDATIONKITCXXSTL_ALWAYS_INLINE u32 inl(u16 port) {
        u32 ret;
        __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
        return ret;
    }

    static FOUNDATIONKITCXXSTL_ALWAYS_INLINE void outsb(u16 port, void* buf, usize len) {
        __asm__ volatile ("cld; rep outsb" : "+S"(buf), "+c"(len) : "d"(port) : "memory");
    }

    static FOUNDATIONKITCXXSTL_ALWAYS_INLINE void insb(u16 port, void* buf, usize len) {
        __asm__ volatile ("cld; rep insb" : "+D"(buf), "+c"(len) : "d"(port) : "memory");
    }

    static FOUNDATIONKITCXXSTL_ALWAYS_INLINE void outsl(u16 port, void* buf, usize len) {
        __asm__ volatile ("cld; rep outsl" : "+S"(buf), "+c"(len) : "d"(port) : "memory");
    }

    static FOUNDATIONKITCXXSTL_ALWAYS_INLINE void insl(u16 port, void* buf, usize len) {
        __asm__ volatile ("cld; rep insl" : "+D"(buf), "+c"(len) : "d"(port) : "memory");
    }

    static FOUNDATIONKITCXXSTL_ALWAYS_INLINE void outsw(u16 port, void* buf, usize len) {
        __asm__ volatile ("cld; rep outsw" : "+S"(buf), "+c"(len) : "d"(port) : "memory");
    }

    static FOUNDATIONKITCXXSTL_ALWAYS_INLINE void insw(u16 port, void* buf, usize len) {
        __asm__ volatile ("cld; rep insw" : "+D"(buf), "+c"(len) : "d"(port) : "memory");
    }

}