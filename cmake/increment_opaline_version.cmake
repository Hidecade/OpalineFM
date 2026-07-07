if (NOT DEFINED VERSION_FILE)
    message(FATAL_ERROR "VERSION_FILE is required")
endif()

if (NOT EXISTS "${VERSION_FILE}")
    message(FATAL_ERROR "Version file not found: ${VERSION_FILE}")
endif()

file(READ "${VERSION_FILE}" contents)
string(REGEX MATCH [[set\(OPALINE_VERSION_MAJOR ([0-9]+)\)]] major_match "${contents}")
set(major "${CMAKE_MATCH_1}")
string(REGEX MATCH [[set\(OPALINE_VERSION_MINOR ([0-9]+)\)]] minor_match "${contents}")
set(minor "${CMAKE_MATCH_1}")
string(REGEX MATCH [[set\(OPALINE_VERSION_PATCH ([0-9]+)\)]] patch_match "${contents}")
set(patch "${CMAKE_MATCH_1}")

if (major STREQUAL "" OR minor STREQUAL "" OR patch STREQUAL "")
    message(FATAL_ERROR "Could not parse Opaline version file")
endif()

set(current_version "${major}.${minor}.${patch}")
if (DEFINED EXPECTED_VERSION AND NOT current_version STREQUAL EXPECTED_VERSION)
    message(STATUS "Opaline FM version already advanced from ${EXPECTED_VERSION} to ${current_version}; skipping bump")
    return()
endif()

math(EXPR patch "${patch} + 1")
set(version "${major}.${minor}.${patch}")

file(WRITE "${VERSION_FILE}"
"set(OPALINE_VERSION_MAJOR ${major})\n"
"set(OPALINE_VERSION_MINOR ${minor})\n"
"set(OPALINE_VERSION_PATCH ${patch})\n"
"set(OPALINE_VERSION \"\${OPALINE_VERSION_MAJOR}.\${OPALINE_VERSION_MINOR}.\${OPALINE_VERSION_PATCH}\")\n")

if (DEFINED HEADER_FILE)
    get_filename_component(header_dir "${HEADER_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${header_dir}")
    file(WRITE "${HEADER_FILE}"
"#pragma once\n\n"
"#define OPALINE_VERSION_MAJOR ${major}\n"
"#define OPALINE_VERSION_MINOR ${minor}\n"
"#define OPALINE_VERSION_PATCH ${patch}\n"
"#define OPALINE_VERSION_STRING \"${version}\"\n")
endif()

message(STATUS "Opaline FM version bumped to ${version}")