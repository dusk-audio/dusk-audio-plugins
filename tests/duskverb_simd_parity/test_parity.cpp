// AVX2 vs scalar parity test for the FDN feedback kernels.
//
// Compiles both `*Scalar` and `*AVX2` versions from FDNReverbSIMDKernels.h
// and asserts they agree to within a few ULP on a large pool of random
// inputs. Catches regressions in the hand-written intrinsics (FMA sign
// flips, lane permutation bugs, missing zero-init) before they reach a
// release build.
//
// Tolerance: 1e-4 absolute. FMA changes accumulation order vs the scalar
// 16-multiply-add sum, so the two paths are NOT bit-identical — but the
// drift is bounded by a handful of ULP for inputs in [-1, +1]. The test
// signals are intentionally O(1) magnitude (typical FDN feedback samples).
//
// Build: enabled when CMake configures with -DBUILD_DUSKVERB_SIMD_PARITY=ON.
// The target is compiled with -mavx2 -mfma to ensure both kernels are
// available in the same translation unit. On hosts that lack AVX2 the test
// is built but reports "skipped" and exits 0.

#include "FDNReverbSIMDKernels.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace {

constexpr int   kIterations = 4096;
constexpr float kTolerance  = 1e-4f;

bool floatsClose (float a, float b, float tol)
{
    const float diff = std::fabs (a - b);
    return diff <= tol;
}

int testHadamard16 (std::mt19937& rng)
{
#if !DUSK_FDN_HAS_AVX2
    (void) rng;
    std::printf ("hadamardInPlace16: AVX2 not available — skipped\n");
    return 0;
#else
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    int failures = 0;
    float maxAbsDiff = 0.0f;

    for (int it = 0; it < kIterations; ++it)
    {
        float scalar[16], simd[16];
        for (int i = 0; i < 16; ++i)
        {
            const float v = dist (rng);
            scalar[i] = v;
            simd  [i] = v;
        }

        duskverb::detail::hadamardInPlace16Scalar (scalar);
        duskverb::detail::hadamardInPlace16AVX2   (simd);

        for (int i = 0; i < 16; ++i)
        {
            const float d = std::fabs (scalar[i] - simd[i]);
            if (d > maxAbsDiff) maxAbsDiff = d;
            if (! floatsClose (scalar[i], simd[i], kTolerance))
            {
                if (failures < 4)
                    std::fprintf (stderr,
                                  "  hadamard mismatch iter %d lane %d: scalar=%g simd=%g diff=%g\n",
                                  it, i, scalar[i], simd[i], d);
                ++failures;
            }
        }
    }

    std::printf ("hadamardInPlace16: %d iters, maxAbsDiff=%g, failures=%d\n",
                 kIterations, maxAbsDiff, failures);
    return failures == 0 ? 0 : 1;
#endif
}

int testPerturbMatVec16 (std::mt19937& rng)
{
#if !DUSK_FDN_HAS_AVX2
    (void) rng;
    std::printf ("perturbMatVec16: AVX2 not available — skipped\n");
    return 0;
#else
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    int failures = 0;
    float maxAbsDiff = 0.0f;

    for (int it = 0; it < kIterations; ++it)
    {
        float M[16][16];
        float in[16], outScalar[16], outSimd[16];

        for (int r = 0; r < 16; ++r)
            for (int c = 0; c < 16; ++c)
                M[r][c] = dist (rng);
        for (int i = 0; i < 16; ++i) in[i] = dist (rng);

        duskverb::detail::perturbMatVec16Scalar (M, in, outScalar);
        duskverb::detail::perturbMatVec16AVX2   (M, in, outSimd);

        for (int i = 0; i < 16; ++i)
        {
            const float d = std::fabs (outScalar[i] - outSimd[i]);
            if (d > maxAbsDiff) maxAbsDiff = d;
            if (! floatsClose (outScalar[i], outSimd[i], kTolerance))
            {
                if (failures < 4)
                    std::fprintf (stderr,
                                  "  perturb mismatch iter %d row %d: scalar=%g simd=%g diff=%g\n",
                                  it, i, outScalar[i], outSimd[i], d);
                ++failures;
            }
        }
    }

    std::printf ("perturbMatVec16: %d iters, maxAbsDiff=%g, failures=%d\n",
                 kIterations, maxAbsDiff, failures);
    return failures == 0 ? 0 : 1;
#endif
}

} // namespace

// Runtime CPU feature probe. The test target is compiled with -mavx2 -mfma
// so the AVX2 codepaths are present in the binary, but if the host CPU
// doesn't support those instructions we must NOT execute them or the
// process raises SIGILL. Returns true only when both AVX2 and FMA are
// available at runtime. Compile-time short-circuits on non-x86 hosts.
bool runtimeHasAvx2Fma()
{
#if defined(__GNUC__) && (defined(__x86_64__) || defined(_M_X64) || defined(__i386__))
    __builtin_cpu_init();
    return __builtin_cpu_supports ("avx2") && __builtin_cpu_supports ("fma");
#else
    return false;
#endif
}

int main()
{
    std::printf ("DuskVerb FDN SIMD parity test (DUSK_FDN_HAS_AVX2=%d)\n",
                 DUSK_FDN_HAS_AVX2);

    if (! runtimeHasAvx2Fma())
    {
        std::printf ("Host CPU lacks AVX2/FMA — test skipped (PASS by convention).\n");
        return 0;
    }

    std::mt19937 rng (0xC0FFEEu);

    int rc = 0;
    rc |= testHadamard16       (rng);
    rc |= testPerturbMatVec16  (rng);

    if (rc == 0)
        std::printf ("PASS\n");
    else
        std::fprintf (stderr, "FAIL\n");
    return rc;
}
