cmake_minimum_required(VERSION 3.22)

project(DX21Native VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(dx21_engine STATIC
    Source/Engine/Dx21Envelope.cpp
    Source/Engine/Dx21Engine.cpp
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

set(DX21_RESOLVED_JUCE_DIR "")
if (DX21_JUCE_DIR AND EXISTS "${DX21_JUCE_DIR}/CMakeLists.txt")
    set(DX21_RESOLVED_JUCE_DIR "${DX21_JUCE_DIR}")
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    set(DX21_RESOLVED_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE")
endif()

if (DX21_RESOLVED_JUCE_DIR)
    add_subdirectory("${DX21_RESOLVED_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

    juce_add_gui_app(DX21Native_Standalone
        PRODUCT_NAME "DX21 Native"
        COMPANY_NAME "Hidecade"
        BUNDLE_ID "jp.hidecade.dx21native"
    )

    target_sources(DX21Native_Standalone
        PRIVATE
            Source/App/Main.cpp
            Source/App/Dx21AppState.h
            Source/App/Dx21StateSerialization.h
            Source/App/MainComponent.cpp
            Source/App/MainComponent.h
    )

    target_compile_definitions(DX21Native_Standalone
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_ASIO=1
            JUCE_ASIO_USE_EXTERNAL_SDK=0
            DX21_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    )

    target_link_libraries(DX21Native_Standalone
        PRIVATE
            dx21_engine
            juce::juce_audio_utils
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
else()
    message(STATUS "JUCE not found. Standalone target disabled. Set DX21_JUCE_DIR or place JUCE at external/JUCE.")
endif()
