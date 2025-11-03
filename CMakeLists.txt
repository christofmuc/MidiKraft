cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0135)
    # Preserve archive timestamps so FetchContent skips unnecessary rebuilds (silences CMake 3.25+ warning)
    cmake_policy(SET CMP0135 NEW)
endif()

project(MidiKraft VERSION 0.1 LANGUAGES C CXX)

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
option(MIDIKRAFT_FETCH_JUCE "Automatically fetch JUCE if not provided by the parent project" ON)
option(MIDIKRAFT_FETCH_JUCE_UTILS "Automatically fetch juce-utils if not provided by the parent project" ON)
set(MIDIKRAFT_JUCE_EXTRA_LINK_LIBRARIES "" CACHE STRING "Additional platform libraries required for juce-static (e.g. pulseaudio, X11)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

set(MIDIKRAFT_JUCE_MODULES
    juce_audio_basics
    juce_audio_devices
    juce_audio_formats
    juce_audio_processors
    juce_audio_utils
    juce_core
    juce_javascript
    juce_cryptography
    juce_data_structures
    juce_dsp
    juce_events
    juce_graphics
    juce_gui_basics
    juce_gui_extra
    juce_opengl
    juce_video
)

function(midikraft_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(MIDIKRAFT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        target_compile_definitions(${target} PRIVATE _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
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
        URL https://github.com/gabime/spdlog/archive/refs/tags/v1.16.0.tar.gz
    )
    FetchContent_MakeAvailable(spdlog)
endif()

if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json CONFIG QUIET)
endif()
if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

if(NOT TARGET juce::juce_core)
    if(MIDIKRAFT_FETCH_JUCE)
        set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
        set(JUCE_ENABLE_MODULE_SOURCE_GROUPS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            juce
            URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.10.zip
        )
        FetchContent_MakeAvailable(juce)
    else()
        message(FATAL_ERROR "JUCE modules not found. Provide targets juce::juce_core, etc., or enable MIDIKRAFT_FETCH_JUCE.")
    endif()
endif()

set(_midikraft_juce_module_targets "")
foreach(module IN LISTS MIDIKRAFT_JUCE_MODULES)
    if(TARGET juce::${module})
        list(APPEND _midikraft_juce_module_targets juce::${module})
    elseif(DEFINED juce_SOURCE_DIR)
        juce_add_module(${juce_SOURCE_DIR}/modules/${module})
        if(TARGET juce::${module})
            list(APPEND _midikraft_juce_module_targets juce::${module})
        endif()
    endif()
endforeach()

if(NOT DEFINED JUCE_INCLUDES AND TARGET juce::juce_core)
    get_target_property(_juce_core_includes juce::juce_core INTERFACE_INCLUDE_DIRECTORIES)
    if(_juce_core_includes)
        list(GET _juce_core_includes 0 _juce_first_include)
        set(JUCE_INCLUDES ${_juce_first_include} CACHE PATH "JUCE module include directory")
    endif()
endif()

if(NOT DEFINED JUCE_INCLUDES AND DEFINED juce_SOURCE_DIR)
    set(JUCE_INCLUDES ${juce_SOURCE_DIR}/modules CACHE PATH "JUCE module include directory")
endif()

if(NOT TARGET juce-static AND _midikraft_juce_module_targets)
    add_library(juce-static STATIC)
    target_link_libraries(
        juce-static
        PRIVATE
            ${_midikraft_juce_module_targets}
            ${MIDIKRAFT_JUCE_EXTRA_LINK_LIBRARIES}
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
    )
    target_compile_definitions(
        juce-static
        PUBLIC
            JUCE_STANDALONE_APPLICATION=1
            JUCE_MODAL_LOOPS_PERMITTED=1
            JUCE_USE_NATIVE_FILECHOOSERS=1
            JUCE_PLUGINHOST_VST=0
            JUCE_PLUGINHOST_AU=0
            DONT_SET_USING_JUCE_NAMESPACE=1
            JUCE_REPORT_APP_USAGE=0
            JUCE_CHECK_MEMORY_LEAKS=0
            JUCE_QUICKTIME=0
            JUCE_USE_DIRECTWRITE=1
            JUCE_CATCH_UNHANDLED_EXCEPTIONS=0
            JUCE_COREGRAPHICS_DRAW_ASYNC=1
            JUCE_WIN_PER_MONITOR_DPI_AWARE=1
            JUCE_USE_FLAC=1
            JUCE_WEB_BROWSER=0
            JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
            JUCE_MODULE_AVAILABLE_juce_audio_basics=1
            JUCE_MODULE_AVAILABLE_juce_audio_devices=1
            JUCE_MODULE_AVAILABLE_juce_audio_formats=1
            JUCE_MODULE_AVAILABLE_juce_audio_processors=1
            JUCE_MODULE_AVAILABLE_juce_audio_utils=1
            JUCE_MODULE_AVAILABLE_juce_core=1
            JUCE_MODULE_AVAILABLE_juce_javascript=1
            JUCE_MODULE_AVAILABLE_juce_cryptography=1
            JUCE_MODULE_AVAILABLE_juce_data_structures=1
            JUCE_MODULE_AVAILABLE_juce_dsp=1
            JUCE_MODULE_AVAILABLE_juce_events=1
            JUCE_MODULE_AVAILABLE_juce_graphics=1
            JUCE_MODULE_AVAILABLE_juce_gui_basics=1
            JUCE_MODULE_AVAILABLE_juce_gui_extra=1
            JUCE_MODULE_AVAILABLE_juce_opengl=1
            JUCE_MODULE_AVAILABLE_juce_video=1
    )
    if(DEFINED JUCE_INCLUDES)
        target_include_directories(
            juce-static
            PUBLIC
                ${JUCE_INCLUDES}
        )
    endif()
    set_target_properties(
        juce-static
        PROPERTIES
            POSITION_INDEPENDENT_CODE TRUE
            VISIBILITY_INLINES_HIDDEN TRUE
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
    )
endif()

if(NOT TARGET juce-utils)
    if(MIDIKRAFT_FETCH_JUCE_UTILS)
        FetchContent_Declare(
            juce_utils
            GIT_REPOSITORY https://github.com/christofmuc/juce-utils.git
            GIT_TAG master
        )
        FetchContent_MakeAvailable(juce_utils)
    else()
        message(FATAL_ERROR "juce-utils target not found. Provide it or enable MIDIKRAFT_FETCH_JUCE_UTILS.")
    endif()
endif()

if(TARGET juce-utils)
    target_compile_definitions(juce-utils PUBLIC _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
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
