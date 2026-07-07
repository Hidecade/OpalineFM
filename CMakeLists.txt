cmake_minimum_required(VERSION 3.22)

project(OpalineFM VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC OR CMAKE_GENERATOR MATCHES "Visual Studio")
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(opaline_engine STATIC
    Source/Engine/OpalineChipEnvelope.cpp
    Source/Engine/OpalineEnvelope.cpp
    Source/Engine/OpalineEngine.cpp
    Source/Engine/OpalinePitchEnvelope.cpp
    Source/Engine/OpalineTables.cpp
    Source/Engine/OpalineTypes.cpp
    Source/Engine/OpalineSysex.cpp
    Source/Engine/OpalineVoice.cpp
    Source/Engine/OpalineVoiceLibrary.cpp
)

target_include_directories(opaline_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

add_executable(opaline_console_render
    Source/Console/Main.cpp
)

target_link_libraries(opaline_console_render PRIVATE opaline_engine)

add_executable(opaline_engine_tests
    Source/Tests/EngineTests.cpp
)

target_link_libraries(opaline_engine_tests PRIVATE opaline_engine)
target_compile_definitions(opaline_engine_tests
    PRIVATE
        OPALINE_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

add_test(NAME opaline_engine_tests COMMAND opaline_engine_tests)

set(OPALINE_JUCE_DIR "" CACHE PATH "Path to a JUCE checkout. If empty, external/JUCE is used when present.")
option(OPALINE_BUILD_STANDALONE "Build the JUCE standalone app when JUCE is available." ON)
option(OPALINE_BUILD_PLUGIN "Build the JUCE VST3 plugin when JUCE is available." ON)
option(OPALINE_BUILD_AU "Build the Audio Unit plugin format on macOS." ON)

set(OPALINE_RESOLVED_JUCE_DIR "")
if (NOT OPALINE_BUILD_STANDALONE AND NOT OPALINE_BUILD_PLUGIN)
    set(OPALINE_RESOLVED_JUCE_DIR "")
elseif (OPALINE_JUCE_DIR AND EXISTS "${OPALINE_JUCE_DIR}/CMakeLists.txt")
    set(OPALINE_RESOLVED_JUCE_DIR "${OPALINE_JUCE_DIR}")
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    set(OPALINE_RESOLVED_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE")
endif()

if (OPALINE_RESOLVED_JUCE_DIR)
    add_subdirectory("${OPALINE_RESOLVED_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

    if (OPALINE_BUILD_STANDALONE)
    juce_add_gui_app(OpalineFM_Standalone
        PRODUCT_NAME "Opaline FM"
        COMPANY_NAME "Hidecade"
        BUNDLE_ID "jp.hidecade.opalinefm"
    )

    target_sources(OpalineFM_Standalone
        PRIVATE
            Source/App/Main.cpp
            Source/App/OpalineAppState.h
            Source/App/OpalineStateSerialization.h
            Source/App/MainComponent.cpp
            Source/App/MainComponent.h
    )

    target_compile_definitions(OpalineFM_Standalone
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_ASIO=1
            JUCE_ASIO_USE_EXTERNAL_SDK=0
            OPALINE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    )

    target_link_libraries(OpalineFM_Standalone
        PRIVATE
            opaline_engine
            juce::juce_audio_utils
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
    endif()

    if (OPALINE_BUILD_PLUGIN)
    set(OPALINE_PLUGIN_FORMATS VST3 Standalone)
    if (APPLE AND OPALINE_BUILD_AU)
        list(APPEND OPALINE_PLUGIN_FORMATS AU)
    endif()

    juce_add_plugin(OpalineFM_Plugin
        COMPANY_NAME "Hidecade"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE Hica
        PLUGIN_CODE OpFM
        FORMATS ${OPALINE_PLUGIN_FORMATS}
        PRODUCT_NAME "Opaline FM"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )

    target_sources(OpalineFM_Plugin
        PRIVATE
            Source/App/OpalineAppState.h
            Source/App/OpalineStateSerialization.h
            Source/App/MainComponent.cpp
            Source/App/MainComponent.h
            Source/Plugin/PluginEditor.cpp
            Source/Plugin/PluginEditor.h
            Source/Plugin/PluginProcessor.cpp
            Source/Plugin/PluginProcessor.h
    )

    target_compile_definitions(OpalineFM_Plugin
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_ASIO=1
            JUCE_ASIO_USE_EXTERNAL_SDK=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_STANDALONE_STATE_FILE_SUFFIX=".opalinefmstate"
            OPALINE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    )

    if (MSVC)
        target_compile_options(OpalineFM_Plugin PRIVATE /FS)
    endif()

    target_link_libraries(OpalineFM_Plugin
        PRIVATE
            opaline_engine
            juce::juce_audio_devices
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
    endif()
else()
    message(STATUS "JUCE not found. JUCE targets disabled. Set OPALINE_JUCE_DIR or place JUCE at external/JUCE.")
endif()
