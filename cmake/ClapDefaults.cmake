# CLAP plugin format helper.
#
# Wraps clap_juce_extensions_plugin() so per-plugin CMakeLists only need to
# pass the target name, slug, and CLAP feature tags.
#
# Usage (after juce_add_plugin):
#   apply_clap_extension(DuskVerb duskverb audio-effect reverb stereo)
#
# Produces a <TARGET>_CLAP target. CLAP_ID is fixed to com.dusk-audio.<slug>.
# No-op if BUILD_CLAP is OFF (e.g. when the submodule is missing).

function(apply_clap_extension TARGET_NAME CLAP_SLUG)
    if(NOT BUILD_CLAP)
        return()
    endif()

    set(FEATURES ${ARGN})
    if(NOT FEATURES)
        message(FATAL_ERROR
            "apply_clap_extension(${TARGET_NAME} ${CLAP_SLUG}): at least one CLAP feature tag is required")
    endif()

    clap_juce_extensions_plugin(
        TARGET ${TARGET_NAME}
        CLAP_ID "com.dusk-audio.${CLAP_SLUG}"
        CLAP_FEATURES ${FEATURES}
        CLAP_MANUAL_URL "https://dusk-audio.github.io/plugins/${CLAP_SLUG}"
        CLAP_SUPPORT_URL "https://dusk-audio.github.io"
    )
endfunction()
