cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0135)
    # Preserve archive timestamps so FetchContent skips unnecessary rebuilds (silences CMake 3.25+ warning)
    cmake_policy(SET CMP0135 NEW)
endif()

project(MidiKraft VERSION 0.1 LANGUAGES CXX)

option(MIDIKRAFT_BUILD_LIBRARIAN "Build the optional librarian module" OFF)
include(CMakeDependentOption)
cmake_dependent_option(
    MIDIKRAFT_BUILD_DATABASE
    "Build the optional database (PatchDatabase) module. Requires Librarian."
    OFF
    "MIDIKRAFT_BUILD_LIBRARIAN"
    OFF
)
option(MIDIKRAFT_BUILD_TESTS "Enable building the doctest-based unit tests" OFF)
option(MIDIKRAFT_WARNINGS_AS_ERRORS "Treat all compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

function(midikraft_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(MIDIKRAFT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        set(local_warnings -Wall -Wextra -Wpedantic -Wno-deprecated-declarations)
        if(MIDIKRAFT_WARNINGS_AS_ERRORS)
            list(APPEND local_warnings -Werror)
        endif()
        target_compile_options(${target} PRIVATE ${local_warnings})
    endif()
endfunction()

if(NOT TARGET fmt::fmt)
    FetchContent_Declare(
        fmt
        URL https://github.com/fmtlib/fmt/releases/download/9.1.0/fmt-9.1.0.zip
    )
    FetchContent_MakeAvailable(fmt)
endif()

if(NOT TARGET spdlog::spdlog)
    find_package(spdlog CONFIG QUIET)
endif()
if(NOT TARGET spdlog::spdlog)
    FetchContent_Declare(
        spdlog
        URL https://github.com/gabime/spdlog/archive/refs/tags/v1.13.0.tar.gz
    )
    FetchContent_MakeAvailable(spdlog)
endif()

if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json CONFIG QUIET)
endif()
if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_subdirectory(base)

if(MIDIKRAFT_BUILD_LIBRARIAN)
    add_subdirectory(librarian)
endif()

if(MIDIKRAFT_BUILD_DATABASE)
    add_subdirectory(database)
endif()

if(MIDIKRAFT_BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(
        doctest
        URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.zip
    )
    FetchContent_MakeAvailable(doctest)
    list(APPEND CMAKE_MODULE_PATH "${doctest_SOURCE_DIR}/scripts/cmake")
    add_subdirectory(tests)
endif()

