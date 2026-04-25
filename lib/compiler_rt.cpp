// ============================================================================
// compiler_rt.cpp — freestanding compiler-rt stubs for Ceryx
//
// Clang/GCC emit calls to these symbols when performing arithmetic on types
// wider than the native machine word (128-bit integers on x86_64).  Because
// the kernel is built with -nostdlib, compiler-rt is not linked, so we must
// supply the implementations ourselves.
//
// Naming convention (Itanium ABI / LLVM compiler-rt):
//   __udivti3  — unsigned 128-bit division, returns quotient
//   __umodti3  — unsigned 128-bit modulo,   returns remainder
//   __divti3   — signed   128-bit division, returns quotient
//   __modti3   — signed   128-bit modulo,   returns remainder
//   __multi3   — signed   128-bit multiply  (only needed on 32-bit targets;
//                on x86_64 the compiler uses the MUL instruction directly,
//                but we include it for completeness)
//
// Algorithm — __udivti3 / __umodti3:
//
//   The standard approach for 128÷64 on x86_64 is the two-step "divq" trick:
//
//     Given  N = (N_hi << 64) | N_lo  and  D (64-bit denominator):
//
//     1. If N_hi == 0: trivial — one 64-bit DIV instruction.
//     2. If N_hi >= D: overflow (quotient > 64 bits) — use the full
//        128-bit long-division loop.
//     3. Otherwise: use the x86_64 "div" instruction which accepts a
//        128-bit dividend split across RDX:RAX and a 64-bit divisor,
//        producing a 64-bit quotient in RAX and remainder in RDX.
//        This is the common case for TimeKeeper::_OslFallback().
//
//   For the general 128÷128 case (D is also 128-bit) we fall back to a
//   portable bit-by-bit long-division loop.  This is slower but correct
//   and is only hit when the divisor itself exceeds 64 bits.
//
// References:
//   LLVM compiler-rt/lib/builtins/udivmodti4.c
//   Hacker's Delight, Chapter 9 — Unsigned Long Division
// ============================================================================

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

using namespace FoundationKitCxxStl;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

/// @brief Split a u128 into its high and low 64-bit halves.
struct U128Parts {
    u64 lo;
    u64 hi;
};

[[nodiscard]] static constexpr U128Parts Split(u128 v) noexcept {
    return { static_cast<u64>(v), static_cast<u64>(v >> 64) };
}

[[nodiscard]] static constexpr u128 Join(u64 hi, u64 lo) noexcept {
    return (static_cast<u128>(hi) << 64) | lo;
}

/// @brief Unsigned 128-bit division and modulo in one pass.
///
/// @param n   Dividend (128-bit).
/// @param d   Divisor  (128-bit).
/// @param rem Output remainder (may be nullptr).
/// @return    Quotient.
///
/// Fast paths (in order of likelihood for kernel use):
///   1. d_hi == 0 && n_hi == 0  → single 64-bit DIV
///   2. d_hi == 0 && n_hi < d_lo → x86_64 128÷64 DIV instruction (RDX:RAX)
///   3. d_hi == 0 && n_hi >= d_lo → long division (quotient > 64 bits)
///   4. d_hi != 0                 → bit-by-bit long division
[[nodiscard]] static u128 UDivMod128(u128 n, u128 d, u128* rem) noexcept {
    FK_BUG_ON(d == 0, "compiler_rt: __udivti3 / __umodti3: division by zero");

    const auto [n_lo, n_hi] = Split(n);
    const auto [d_lo, d_hi] = Split(d);

    // ── Fast path 1: both fit in 64 bits ─────────────────────────────────────
    if (n_hi == 0 && d_hi == 0) {
        if (rem) *rem = n_lo % d_lo;
        return n_lo / d_lo;
    }

    // ── Fast path 2: divisor fits in 64 bits, quotient fits in 64 bits ───────
    // Condition: d_hi == 0 && n_hi < d_lo
    // The x86_64 DIV instruction computes RDX:RAX / r/m64 → quotient in RAX,
    // remainder in RDX.  It faults if the quotient overflows 64 bits, which is
    // why we guard with n_hi < d_lo.
    if (d_hi == 0 && n_hi < d_lo) {
        u64 q, r;
        __asm__(
            "divq %4"
            : "=a"(q), "=d"(r)
            : "a"(n_lo), "d"(n_hi), "r"(d_lo)
        );
        if (rem) *rem = r;
        return q;
    }

    // ── Fast path 3: divisor fits in 64 bits, quotient may exceed 64 bits ────
    // n_hi >= d_lo (with d_hi == 0).  Decompose into two 128÷64 divisions:
    //   q_hi = n_hi / d_lo  (with remainder r_hi)
    //   q_lo = (r_hi << 64 | n_lo) / d_lo
    if (d_hi == 0) {
        u64 q_hi, r_hi, q_lo, r_lo;

        // First division: n_hi / d_lo
        __asm__(
            "divq %4"
            : "=a"(q_hi), "=d"(r_hi)
            : "a"(n_hi), "d"(u64{0}), "r"(d_lo)
        );

        // Second division: (r_hi:n_lo) / d_lo
        __asm__(
            "divq %4"
            : "=a"(q_lo), "=d"(r_lo)
            : "a"(n_lo), "d"(r_hi), "r"(d_lo)
        );

        if (rem) *rem = r_lo;
        return Join(q_hi, q_lo);
    }

    // ── General case: 128÷128 bit-by-bit long division ───────────────────────
    // This path is only hit when d_hi != 0, i.e. the divisor itself is > 64 bits.
    // For the TimeKeeper use-case this never happens (freq is always a u64).
    // We use the standard shift-and-subtract algorithm.
    if (n < d) {
        if (rem) *rem = n;
        return 0;
    }

    // Count leading zeros of d to normalise.
    // __builtin_clzll is available in freestanding mode on GCC/Clang.
    const u32 shift = static_cast<u32>(__builtin_clzll(d_hi));

    u128 q = 0;
    u128 r = n;
    u128 d_shifted = d << shift;

    // Unrolled: iterate from the highest bit of the quotient down to 0.
    // Maximum 128 iterations, but shift is at most 63 here (d_hi != 0).
    for (u32 i = 0; i <= shift; ++i) {
        q <<= 1;
        if (r >= d_shifted) {
            r -= d_shifted;
            q |= 1;
        }
        d_shifted >>= 1;
    }

    if (rem) *rem = r;
    return q;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public ABI symbols — must have C linkage and no name mangling
// ---------------------------------------------------------------------------

extern "C" {

/// @brief Unsigned 128-bit division: returns n / d.
__attribute__((used))
u128 __udivti3(u128 n, u128 d) noexcept {
    return UDivMod128(n, d, nullptr);
}

/// @brief Unsigned 128-bit modulo: returns n % d.
__attribute__((used))
u128 __umodti3(u128 n, u128 d) noexcept {
    u128 rem;
    UDivMod128(n, d, &rem);
    return rem;
}

/// @brief Signed 128-bit division: returns n / d.
__attribute__((used))
i128 __divti3(i128 n, i128 d) noexcept {
    FK_BUG_ON(d == 0, "compiler_rt: __divti3: division by zero");

    // Determine sign of result, then delegate to unsigned division.
    const bool neg = (n < 0) != (d < 0);
    const u128 un = (n < 0) ? static_cast<u128>(-n) : static_cast<u128>(n);
    const u128 ud = (d < 0) ? static_cast<u128>(-d) : static_cast<u128>(d);
    const u128 uq = UDivMod128(un, ud, nullptr);
    return neg ? -static_cast<i128>(uq) : static_cast<i128>(uq);
}

/// @brief Signed 128-bit modulo: returns n % d.
__attribute__((used))
i128 __modti3(i128 n, i128 d) noexcept {
    FK_BUG_ON(d == 0, "compiler_rt: __modti3: division by zero");

    // Sign of remainder matches sign of dividend (C standard).
    const bool neg = n < 0;
    const u128 un = (n < 0) ? static_cast<u128>(-n) : static_cast<u128>(n);
    const u128 ud = (d < 0) ? static_cast<u128>(-d) : static_cast<u128>(d);
    u128 rem;
    UDivMod128(un, ud, &rem);
    return neg ? -static_cast<i128>(rem) : static_cast<i128>(rem);
}

/// @brief Unsigned 128-bit division+modulo combined (used by some backends).
/// @param rem_out  Pointer to receive the remainder.
/// @return         Quotient.
__attribute__((used))
u128 __udivmodti4(u128 n, u128 d, u128* rem_out) noexcept {
    return UDivMod128(n, d, rem_out);
}

/// @brief 128-bit multiply — on x86_64 the compiler uses MUL directly,
///        but some code paths (e.g. __int128 in generic C) call this.
///
/// Implemented as: result = (a_lo * b_lo) + ((a_lo * b_hi + a_hi * b_lo) << 64)
/// This avoids any recursive __multi3 call because each sub-multiplication
/// is a plain 64×64→64 operation that the compiler lowers to a single IMUL.
__attribute__((used))
i128 __multi3(i128 a, i128 b) noexcept {
    const u64 a_lo = static_cast<u64>(a);
    const u64 a_hi = static_cast<u64>(static_cast<u128>(a) >> 64);
    const u64 b_lo = static_cast<u64>(b);
    const u64 b_hi = static_cast<u64>(static_cast<u128>(b) >> 64);

    // Full 128-bit product:
    //   result_lo = a_lo * b_lo          (lower 64 bits)
    //   result_hi = a_lo * b_hi          (cross terms, upper 64 bits)
    //             + a_hi * b_lo
    //             + carry from result_lo  (ignored — truncated to 128 bits)
    u64 result_lo, carry;
    __asm__("mulq %3" : "=a"(result_lo), "=d"(carry) : "a"(a_lo), "r"(b_lo));
    const u64 result_hi = carry + a_lo * b_hi + a_hi * b_lo;
    return static_cast<i128>(Join(result_hi, result_lo));
}

} // extern "C"
