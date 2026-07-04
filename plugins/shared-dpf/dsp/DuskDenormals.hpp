// DuskDenormals.hpp — RAII flush-to-zero / denormals-are-zero guard.
//
// Framework-free replacement for juce::ScopedNoDenormals. Handles both SSE
// (x86) and ARM64 (Apple Silicon and others, via FPCR). Construct one at the
// top of every processBlock; it restores the previous FPU control state on
// destruction. Lifted verbatim from plugins/tape-echo/core/TapeEchoDSP.cpp.

#pragma once

#include <cstdint>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
  #include <xmmintrin.h>
  #define DUSK_HAS_SSE 1
#endif

namespace duskaudio
{

struct ScopedFlushDenormals
{
#if DUSK_HAS_SSE
    unsigned int oldCsr;
    ScopedFlushDenormals() noexcept : oldCsr(_mm_getcsr())
    {
        _mm_setcsr(oldCsr | 0x8040u); // FTZ | DAZ
    }
    ~ScopedFlushDenormals() noexcept { _mm_setcsr(oldCsr); }
#elif defined(__aarch64__)
    // ARM64 (Apple Silicon and others): set the FZ bit (24) in FPCR.
    uint64_t oldFpcr;
    ScopedFlushDenormals() noexcept
    {
        asm volatile("mrs %0, fpcr" : "=r"(oldFpcr));
        asm volatile("msr fpcr, %0" ::"r"(oldFpcr | (1ULL << 24)));
    }
    ~ScopedFlushDenormals() noexcept
    {
        asm volatile("msr fpcr, %0" ::"r"(oldFpcr));
    }
#else
    ScopedFlushDenormals() noexcept = default;
#endif
};

} // namespace duskaudio
