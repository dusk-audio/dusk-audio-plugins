# DuskVerb AVX2 vs scalar parity test.
#
# Compiles both kernel variants from FDNReverbSIMDKernels.h into a single
# translation unit (forced -mavx2 -mfma so the AVX2 path is available) and
# asserts the two implementations agree to within a few ULP on random
# inputs. Catches drift between the hand-written intrinsics and the scalar
# reference whenever someone touches either kernel.
#
# Enable with -DBUILD_DUSKVERB_SIMD_PARITY=ON at configure time.
# Runs from `ctest` or directly: ./build/tests/duskverb_simd_parity/duskverb_simd_parity

add_executable(duskverb_simd_parity
    test_parity.cpp
)

target_include_directories(duskverb_simd_parity
    PRIVATE
        ${CMAKE_SOURCE_DIR}/plugins/DuskVerb/src/dsp
)

target_compile_features(duskverb_simd_parity PRIVATE cxx_std_17)

# Force the AVX2 + FMA codepaths to be available in this TU regardless of
# the host's default tuning flags. Non-x86 hosts won't have the option;
# guard so the test still builds (it will print "skipped" at runtime).
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mavx2" DUSKVERB_HAS_MAVX2_FLAG)
check_cxx_compiler_flag("-mfma"  DUSKVERB_HAS_MFMA_FLAG)
if(DUSKVERB_HAS_MAVX2_FLAG AND DUSKVERB_HAS_MFMA_FLAG)
    target_compile_options(duskverb_simd_parity PRIVATE -mavx2 -mfma)
endif()

set_target_properties(duskverb_simd_parity PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/duskverb_simd_parity"
)
