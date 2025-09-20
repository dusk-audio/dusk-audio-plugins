# Default JUCE configuration for all plugins
# This file sets common definitions for all JUCE-based plugins in the project

# Function to apply common JUCE definitions to a plugin target
function(apply_juce_defaults TARGET_NAME)
    # Apply to main target
    target_compile_definitions(${TARGET_NAME}
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=1
            JUCE_DISPLAY_SPLASH_SCREEN=0
            JUCE_REPORT_APP_USAGE=0
            JUCE_STRICT_REFCOUNTEDPOINTER=1
    )

    # Apply to VST3 target if it exists
    if(TARGET ${TARGET_NAME}_VST3)
        target_compile_definitions(${TARGET_NAME}_VST3
            PRIVATE
                JUCE_VST3_CAN_REPLACE_VST2=1
        )
    endif()

    # Apply to LV2 target if it exists
    if(TARGET ${TARGET_NAME}_LV2)
        target_compile_definitions(${TARGET_NAME}_LV2
            PRIVATE
                JUCE_VST3_CAN_REPLACE_VST2=1
        )
    endif()
endfunction()