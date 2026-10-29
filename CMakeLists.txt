cmake_minimum_required(VERSION 3.22)

project(ShapeMod VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SHAPEMOD_BUILD_TESTS "Build the headless engine test" OFF)

# JUCE: use a local checkout if JUCE_DIR is given, otherwise download JUCE 8.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.16-security
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(SHAPEMOD_SOURCES
    Source/Shape.cpp
    Source/ShapeEditor.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

set(SHAPEMOD_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0)

# Builds one variant of the plugin.
#   ShapeMod FX         - goes on an audio track's effect chain, hosts an effect
#   ShapeMod Instrument - goes in the instrument slot, hosts a synth and passes MIDI to it
function(shapemod_add_variant target product_name plugin_code is_synth)
    if(is_synth)
        set(categories Instrument Synth)
    else()
        set(categories Fx)
    endif()

    juce_add_plugin(${target}
        COMPANY_NAME "dgussmann"
        COMPANY_WEBSITE "https://github.com/dgussmann/Claude-VST"
        BUNDLE_ID "com.dgussmann.${target}"
        PLUGIN_MANUFACTURER_CODE Dgsm
        PLUGIN_CODE ${plugin_code}
        FORMATS VST3
        PRODUCT_NAME "${product_name}"
        IS_SYNTH ${is_synth}
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES ${categories})

    target_sources(${target} PRIVATE ${SHAPEMOD_SOURCES})
    target_compile_definitions(${target} PUBLIC ${SHAPEMOD_DEFINITIONS} SHAPEMOD_IS_INSTRUMENT=$<BOOL:${is_synth}>)
    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

shapemod_add_variant(ShapeModFX         "ShapeMod FX"         Smfx FALSE)
shapemod_add_variant(ShapeModInstrument "ShapeMod Instrument" Smin TRUE)

if(SHAPEMOD_BUILD_TESTS)
    juce_add_console_app(ShapeModTest PRODUCT_NAME "ShapeModTest")
    target_sources(ShapeModTest PRIVATE Tests/EngineTest.cpp ${SHAPEMOD_SOURCES})
    target_compile_definitions(ShapeModTest PRIVATE ${SHAPEMOD_DEFINITIONS} SHAPEMOD_IS_INSTRUMENT=0)
    target_link_libraries(ShapeModTest
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
