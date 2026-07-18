cmake_minimum_required(VERSION 3.22)

include(cmake/OpalineVersion.cmake)

project(OpalineFM VERSION ${OPALINE_VERSION} LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OPALINE_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${OPALINE_GENERATED_DIR}")
configure_file(Source/App/OpalineVersion.h.in "${OPALINE_GENERATED_DIR}/OpalineVersion.h" @ONLY)

function(opaline_add_version_bump target_name)
    if (NOT OPALINE_AUTO_INCREMENT_VERSION)
        return()
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DVERSION_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpalineVersion.cmake
            -DEXPECTED_VERSION=${PROJECT_VERSION}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/increment_opaline_version.cmake
        COMMENT "Incrementing Opaline FM patch version for next build"
        VERBATIM
    )
endfunction()

option(OPALINE_AUTO_INCREMENT_VERSION "Increment the patch version after application and plugin builds." ON)

if (MSVC OR CMAKE_GENERATOR MATCHES "Visual Studio")
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(opaline_engine STATIC
    Source/Engine/ChipVoiceImport.cpp
    Source/Engine/OpalineChipEnvelope.cpp
    Source/Engine/OpalineEnvelope.cpp
    Source/Engine/OpalineEngine.cpp
    Source/Engine/OpalinePitchEnvelope.cpp
    Source/Engine/RealtimeAudioRecorder.cpp
    Source/Engine/OpalineTables.cpp
    Source/Engine/OpalineTypes.cpp
    Source/Engine/OpalineSysex.cpp
    Source/Engine/OpalineVoice.cpp
    Source/Engine/OpalineVoiceLibrary.cpp
)

target_include_directories(opaline_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${OPALINE_GENERATED_DIR}
)

add_executable(opaline_console_render
    Source/Console/Main.cpp
)

target_link_libraries(opaline_console_render PRIVATE opaline_engine)

add_executable(opaline_tune_factory_bank
    Source/Tools/TuneFactoryBank.cpp
)

target_link_libraries(opaline_tune_factory_bank PRIVATE opaline_engine)

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
set(OPALINE_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/opalinefm-icon.png")

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

    juce_add_binary_data(OpalineFM_Assets
        NAMESPACE OpalineBinaryData
        HEADER_NAME OpalineBinaryData.h
        SOURCES
            assets/factory.opalinelibrary.xml
            assets/factory.syx
    )

    if (OPALINE_BUILD_STANDALONE)
    juce_add_gui_app(OpalineFM_Standalone
        PRODUCT_NAME "Opaline FM"
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "Hidecade"
        BUNDLE_ID "jp.hidecade.opalinefm"
        ICON_BIG "${OPALINE_APP_ICON}"
        ICON_SMALL "${OPALINE_APP_ICON}"
    )

    target_sources(OpalineFM_Standalone
        PRIVATE
            Source/App/Main.cpp
            Source/App/OpalineAppState.h
            Source/App/OpalineStateSerialization.h
            Source/App/OpalineVoiceLibraryXml.cpp
            Source/App/OpalineVoiceLibraryXml.h
            Source/App/MainComponent.cpp
            Source/App/MainComponent.h
    )

    target_compile_definitions(OpalineFM_Standalone
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_ASIO=1
            JUCE_ASIO_USE_EXTERNAL_SDK=0
    )

    target_link_libraries(OpalineFM_Standalone
        PRIVATE
            opaline_engine
            OpalineFM_Assets
            juce::juce_audio_utils
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
        opaline_add_version_bump(OpalineFM_Standalone)
    endif()

    if (OPALINE_BUILD_PLUGIN)
    set(OPALINE_PLUGIN_FORMATS VST3 Standalone)
    if (APPLE AND OPALINE_BUILD_AU)
        list(APPEND OPALINE_PLUGIN_FORMATS AU)
    endif()

    juce_add_plugin(OpalineFM_Plugin
        COMPANY_NAME "Hidecade"
        VERSION ${PROJECT_VERSION}
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE Hica
        PLUGIN_CODE OpFM
        FORMATS ${OPALINE_PLUGIN_FORMATS}
        PRODUCT_NAME "Opaline FM"
        ICON_BIG "${OPALINE_APP_ICON}"
        ICON_SMALL "${OPALINE_APP_ICON}"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
    )

    target_sources(OpalineFM_Plugin
        PRIVATE
            Source/App/OpalineAppState.h
            Source/App/OpalineStateSerialization.h
            Source/App/OpalineVoiceLibraryXml.cpp
            Source/App/OpalineVoiceLibraryXml.h
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
    )

    if (MSVC)
        target_compile_options(OpalineFM_Plugin PRIVATE /FS)
    endif()

    target_link_libraries(OpalineFM_Plugin
        PRIVATE
            opaline_engine
            OpalineFM_Assets
            juce::juce_audio_devices
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
    opaline_add_version_bump(OpalineFM_Plugin)
    endif()
else()
    message(STATUS "JUCE not found. JUCE targets disabled. Set OPALINE_JUCE_DIR or place JUCE at external/JUCE.")
endif()
