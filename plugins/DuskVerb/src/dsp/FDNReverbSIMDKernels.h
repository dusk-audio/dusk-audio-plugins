#pragma once

// FDN feedback kernels with optional AVX2 acceleration.
//
// Two implementations of each kernel are exposed under separate names:
//   * <kernel>Scalar — pure C++, always compiled, the reference path.
//   * <kernel>AVX2   — only present when the TU is compiled with
//                       -mavx2 -mfma (i.e. __AVX2__ && __FMA__ defined).
//
// The compile-time dispatch wrappers `<kernel>` pick AVX2 when available and
// fall back to the scalar otherwise. This is the path FDNReverb's audio
// thread calls.
//
// Having both versions independently named lets a parity test compile the
// AVX2 path and the scalar path in the same translation unit and assert
// they agree to within a tight ULP bound. See
// tests/duskverb_simd_parity/test_parity.cpp.

#include <cmath>

#if defined(__AVX2__) && defined(__FMA__)
    #include <immintrin.h>
    #define DUSK_FDN_HAS_AVX2 1
#else
    #define DUSK_FDN_HAS_AVX2 0
#endif

namespace duskverb {
namespace detail {

inline constexpr bool kHasAVX2 = (DUSK_FDN_HAS_AVX2 != 0);

// ---------------------------------------------------------------------------
// 16-point in-place Walsh-Hadamard transform — scalar reference.
// O(N log N) butterfly, normalization folded into the final stage so the
// output of H_16 v has the same energy as v (orthonormal matrix).
// ---------------------------------------------------------------------------
inline void hadamardInPlace16Scalar (float* data)
{
    constexpr int n = 16;
    constexpr int kLog2N = 4;

    for (int stage = 0; stage < kLog2N - 1; ++stage)
    {
        int len = 1 << stage;
        for (int i = 0; i < n; i += 2 * len)
        {
            for (int j = 0; j < len; ++j)
            {
                float a = data[i + j];
                float b = data[i + j + len];
                data[i + j]       = a + b;
                data[i + j + len] = a - b;
            }
        }
    }

    constexpr float kNorm = 0.25f;            // 1/√16
    constexpr int lastLen = 1 << (kLog2N - 1); // 8
    for (int i = 0; i < n; i += 2 * lastLen)
    {
        for (int j = 0; j < lastLen; ++j)
        {
            float a = data[i + j];
            float b = data[i + j + lastLen];
            data[i + j]            = (a + b) * kNorm;
            data[i + j + lastLen]  = (a - b) * kNorm;
        }
    }
}

// ---------------------------------------------------------------------------
// 16×16 matrix-vector multiply — scalar reference.
// ---------------------------------------------------------------------------
inline void perturbMatVec16Scalar (const float (&M)[16][16],
                                   const float* in,
                                   float*       out)
{
    for (int row = 0; row < 16; ++row)
    {
        float sum = 0.0f;
        for (int col = 0; col < 16; ++col)
            sum += M[row][col] * in[col];
        out[row] = sum;
    }
}

#if DUSK_FDN_HAS_AVX2
// ---------------------------------------------------------------------------
// 16-point in-place Walsh-Hadamard transform — AVX2 + FMA.
// Math: H_16 v = (H_2 ⊗ H_2 ⊗ H_2 ⊗ H_2) v — four butterfly stages.
// Each stage maps `out = data * sign_mask + swapped_data` so a single
// _mm256_fmadd_ps per __m256 covers a whole pass.
//   Stage 0: butterfly adjacent scalars   (i ↔ i ^ 1, within 128-lanes)
//   Stage 1: butterfly 2-float halves     (i ↔ i ^ 2, within 128-lanes)
//   Stage 2: butterfly 4-float halves     (i ↔ i ^ 4, cross 128-lane permute)
//   Stage 3: butterfly 8-float halves     (lo ↔ hi 256, folds 1/√16 norm)
// ---------------------------------------------------------------------------
inline void hadamardInPlace16AVX2 (float* data)
{
    __m256 lo = _mm256_loadu_ps (data);
    __m256 hi = _mm256_loadu_ps (data + 8);

    {
        const __m256 loSwap = _mm256_shuffle_ps (lo, lo, _MM_SHUFFLE (2, 3, 0, 1));
        const __m256 hiSwap = _mm256_shuffle_ps (hi, hi, _MM_SHUFFLE (2, 3, 0, 1));
        const __m256 sign   = _mm256_setr_ps (1.f,-1.f, 1.f,-1.f, 1.f,-1.f, 1.f,-1.f);
        lo = _mm256_fmadd_ps (lo, sign, loSwap);
        hi = _mm256_fmadd_ps (hi, sign, hiSwap);
    }

    {
        const __m256 loSwap = _mm256_shuffle_ps (lo, lo, _MM_SHUFFLE (1, 0, 3, 2));
        const __m256 hiSwap = _mm256_shuffle_ps (hi, hi, _MM_SHUFFLE (1, 0, 3, 2));
        const __m256 sign   = _mm256_setr_ps (1.f, 1.f,-1.f,-1.f, 1.f, 1.f,-1.f,-1.f);
        lo = _mm256_fmadd_ps (lo, sign, loSwap);
        hi = _mm256_fmadd_ps (hi, sign, hiSwap);
    }

    {
        const __m256 loSwap = _mm256_permute2f128_ps (lo, lo, 0x01);
        const __m256 hiSwap = _mm256_permute2f128_ps (hi, hi, 0x01);
        const __m256 sign   = _mm256_setr_ps (1.f, 1.f, 1.f, 1.f,-1.f,-1.f,-1.f,-1.f);
        lo = _mm256_fmadd_ps (lo, sign, loSwap);
        hi = _mm256_fmadd_ps (hi, sign, hiSwap);
    }

    {
        const __m256 kNorm = _mm256_set1_ps (0.25f);
        const __m256 sum   = _mm256_mul_ps (_mm256_add_ps (lo, hi), kNorm);
        const __m256 diff  = _mm256_mul_ps (_mm256_sub_ps (lo, hi), kNorm);
        _mm256_storeu_ps (data,     sum);
        _mm256_storeu_ps (data + 8, diff);
    }
}

// ---------------------------------------------------------------------------
// 16×16 matrix-vector multiply — AVX2 + FMA.
// Each row dot product: two FMAs across 8-float halves, then horizontal sum.
// FMA changes accumulation order vs the scalar 16-step sum, so the result
// is bitwise-different but within a few ULP — verified by the parity test
// at `tests/duskverb_simd_parity/`.
// ---------------------------------------------------------------------------
inline void perturbMatVec16AVX2 (const float (&M)[16][16],
                                 const float* in,
                                 float*       out)
{
    const __m256 xLo = _mm256_loadu_ps (in);
    const __m256 xHi = _mm256_loadu_ps (in + 8);
    for (int row = 0; row < 16; ++row)
    {
        const __m256 mLo = _mm256_loadu_ps (&M[row][0]);
        const __m256 mHi = _mm256_loadu_ps (&M[row][8]);
        const __m256 prod = _mm256_fmadd_ps (mHi, xHi,
                                _mm256_mul_ps (mLo, xLo));
        const __m128 lo128 = _mm256_castps256_ps128 (prod);
        const __m128 hi128 = _mm256_extractf128_ps  (prod, 1);
        __m128 s = _mm_add_ps (lo128, hi128);
        s = _mm_hadd_ps (s, s);
        s = _mm_hadd_ps (s, s);
        out[row] = _mm_cvtss_f32 (s);
    }
}
#endif // DUSK_FDN_HAS_AVX2

// ---------------------------------------------------------------------------
// Compile-time dispatch wrappers — production code calls these.
// ---------------------------------------------------------------------------
inline void hadamardInPlace16 (float* data)
{
#if DUSK_FDN_HAS_AVX2
    hadamardInPlace16AVX2 (data);
#else
    hadamardInPlace16Scalar (data);
#endif
}

inline void perturbMatVec16 (const float (&M)[16][16],
                             const float* in,
                             float*       out)
{
#if DUSK_FDN_HAS_AVX2
    perturbMatVec16AVX2 (M, in, out);
#else
    perturbMatVec16Scalar (M, in, out);
#endif
}

} // namespace detail
} // namespace duskverb
