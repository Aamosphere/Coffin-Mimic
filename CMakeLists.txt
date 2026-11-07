cmake_minimum_required(VERSION 3.22)
project(Mimic VERSION 0.2.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")

option(MIMIC_BUILD_TESTS "Build the offline test host" OFF)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../JUCE JUCE)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../JUCE/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../JUCE JUCE)
else()
    message(FATAL_ERROR "JUCE-kansiota ei loydy (../JUCE tai ../../JUCE)")
endif()

# MIDI input (prey notes, the Pitcher, manual fill trigger) makes the AU a
# MIDI-controlled effect.
juce_add_plugin(Mimic
    COMPANY_NAME "Aamosphere"
    PLUGIN_MANUFACTURER_CODE Aamo
    PLUGIN_CODE Mimc
    FORMATS AU VST3
    PRODUCT_NAME "Mimic"
    BUNDLE_ID com.aamosphere.mimic
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

set(MIMIC_DSP_SOURCES
    Source/Engine.cpp
    Source/Reverb.cpp
    Source/PluginProcessor.cpp
    Source/Presets.cpp)

target_sources(Mimic PRIVATE
    ${MIMIC_DSP_SOURCES}
    Source/PluginEditor.cpp)

# Special Elite (Apache 2.0) and Bangers (SIL OFL 1.1), licences beside them.
juce_add_binary_data(MimicFonts SOURCES
    Resources/Fonts/SpecialElite-Regular.ttf
    Resources/Fonts/Bangers-Regular.ttf)

juce_generate_juce_header(Mimic)

target_compile_definitions(Mimic PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Mimic
    PRIVATE MimicFonts
            juce::juce_audio_utils
            juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

if(MIMIC_BUILD_TESTS)
    juce_add_console_app(MimicTest PRODUCT_NAME "MimicTest")
    juce_generate_juce_header(MimicTest)
    target_sources(MimicTest PRIVATE Tests/TestHost.cpp ${MIMIC_DSP_SOURCES})
    target_compile_definitions(MimicTest PRIVATE
        MIMIC_HEADLESS=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(MimicTest PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)

    juce_add_console_app(MimicSnapshot PRODUCT_NAME "MimicSnapshot")
    juce_generate_juce_header(MimicSnapshot)
    target_sources(MimicSnapshot PRIVATE Tests/Snapshot.cpp ${MIMIC_DSP_SOURCES} Source/PluginEditor.cpp)
    target_compile_definitions(MimicSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(MimicSnapshot PRIVATE
        MimicFonts
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
