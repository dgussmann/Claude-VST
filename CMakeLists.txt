cmake_minimum_required(VERSION 3.22)
project(Wavesieve VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(WAVESIEVE_BUILD_TESTS "Build the headless test" OFF)

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

set(WAVESIEVE_SOURCES
    Source/WaveTable.cpp
    Source/TableLibrary.cpp
    Source/FilterEngine.cpp
    Source/ShaperEngine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

set(WAVESIEVE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

juce_add_plugin(Wavesieve
    COMPANY_NAME "dgussmann"
    BUNDLE_ID "com.dgussmann.wavesieve"
    PLUGIN_MANUFACTURER_CODE Dgsm
    PLUGIN_CODE Wvsv
    FORMATS VST3
    PRODUCT_NAME "Wavesieve"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Filter Distortion)

target_sources(Wavesieve PRIVATE ${WAVESIEVE_SOURCES})
target_compile_definitions(Wavesieve PUBLIC ${WAVESIEVE_DEFINITIONS})
target_link_libraries(Wavesieve
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if(WAVESIEVE_BUILD_TESTS)
    juce_add_gui_app(WavesieveTest PRODUCT_NAME "WavesieveTest" NEEDS_CURL FALSE)
    target_sources(WavesieveTest PRIVATE Tests/WavesieveTest.cpp ${WAVESIEVE_SOURCES})
    target_compile_definitions(WavesieveTest PRIVATE ${WAVESIEVE_DEFINITIONS} JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(WavesieveTest
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
