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

add_library(dx21_engine STATIC
    Source/Engine/Dx21ChipEnvelope.cpp
    Source/Engine/Dx21Envelope.cpp
    Source/Engine/Dx21Engine.cpp
    Source/Engine/Dx21PitchEnvelope.cpp
    Source/Engine/Dx21Tables.cpp
    Source/Engine/Dx21Types.cpp
    Source/Engine/Dx21Sysex.cpp
    Source/Engine/Dx21Voice.cpp
    Source/Engine/Dx21VoiceLibrary.cpp
)

target_include_directories(dx21_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

add_executable(dx21_console_render
    Source/Console/Main.cpp
)

target_link_libraries(dx21_console_render PRIVATE dx21_engine)

add_executable(dx21_engine_tests
    Source/Tests/EngineTests.cpp
)

target_link_libraries(dx21_engine_tests PRIVATE dx21_engine)
target_compile_definitions(dx21_engine_tests
    PRIVATE
        DX21_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

add_test(NAME dx21_engine_tests COMMAND dx21_engine_tests)

set(DX21_JUCE_DIR "" CACHE PATH "Path to a JUCE checkout. If empty, external/JUCE is used when present.")
option(DX21_BUILD_STANDALONE "Build the JUCE standalone app when JUCE is available." ON)
option(DX21_BUILD_PLUGIN "Build the JUCE VST3 plugin when JUCE is available." ON)
option(DX21_BUILD_AU "Build the Audio Unit plugin format on macOS." ON)

set(DX21_RESOLVED_JUCE_DIR "")
if (NOT DX21_BUILD_STANDALONE AND NOT DX21_BUILD_PLUGIN)
    set(DX21_RESOLVED_JUCE_DIR "")
elseif (DX21_JUCE_DIR AND EXISTS "${DX21_JUCE_DIR}/CMakeLists.txt")
    set(DX21_RESOLVED_JUCE_DIR "${DX21_JUCE_DIR}")
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    set(DX21_RESOLVED_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE")
endif()

if (DX21_RESOLVED_JUCE_DIR)
    add_subdirectory("${DX21_RESOLVED_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

    if (DX21_BUILD_STANDALONE)
    juce_add_gui_app(OpalineFM_Standalone
        PRODUCT_NAME "Opaline FM"
        COMPANY_NAME "Hidecade"
        BUNDLE_ID "jp.hidecade.opalinefm"
    )

    target_sources(OpalineFM_Standalone
        PRIVATE
            Source/App/Main.cpp
            Source/App/Dx21AppState.h
            Source/App/Dx21StateSerialization.h
            Source/App/MainComponent.cpp
            Source/App/MainComponent.h
    )

    target_compile_definitions(OpalineFM_Standalone
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_ASIO=1
            JUCE_ASIO_USE_EXTERNAL_SDK=0
            DX21_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    )

    target_link_libraries(OpalineFM_Standalone
        PRIVATE
            dx21_engine
            juce::juce_audio_utils
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
    endif()

    if (DX21_BUILD_PLUGIN)
    set(DX21_PLUGIN_FORMATS VST3 Standalone)
    if (APPLE AND DX21_BUILD_AU)
        list(APPEND DX21_PLUGIN_FORMATS AU)
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
        FORMATS ${DX21_PLUGIN_FORMATS}
        PRODUCT_NAME "Opaline FM"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )

    target_sources(OpalineFM_Plugin
        PRIVATE
            Source/App/Dx21AppState.h
            Source/App/Dx21StateSerialization.h
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
            DX21_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    )

    if (MSVC)
        target_compile_options(OpalineFM_Plugin PRIVATE /FS)
    endif()

    target_link_libraries(OpalineFM_Plugin
        PRIVATE
            dx21_engine
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
    message(STATUS "JUCE not found. JUCE targets disabled. Set DX21_JUCE_DIR or place JUCE at external/JUCE.")
endif()
