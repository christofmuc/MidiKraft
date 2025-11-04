add_executable(midikraft-base-tests base/test_midikraft_base_smoke.cpp)

target_link_libraries(
    midikraft-base-tests
    PRIVATE
        midikraft-base
        doctest::doctest
)

midikraft_enable_warnings(midikraft-base-tests)

add_test(NAME midikraft-base-tests COMMAND midikraft-base-tests)

add_executable(
    midikraft-librarian-tests
    librarian/test_patch_interchange_format.cpp
)

target_link_libraries(
    midikraft-librarian-tests
    PRIVATE
        midikraft-librarian
        doctest::doctest
        nlohmann_json::nlohmann_json
        spdlog::spdlog
)

target_include_directories(
    midikraft-librarian-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/base/include
        ${CMAKE_SOURCE_DIR}/librarian
        ${CMAKE_BINARY_DIR}/librarian
)

if(DEFINED JUCE_INCLUDES)
    target_include_directories(
        midikraft-librarian-tests
        PRIVATE
            ${JUCE_INCLUDES}
    )
endif()

get_target_property(_juce_utils_includes juce-utils INTERFACE_INCLUDE_DIRECTORIES)
if(_juce_utils_includes)
    target_include_directories(
        midikraft-librarian-tests
        PRIVATE
            ${_juce_utils_includes}
    )
endif()

target_compile_definitions(
    midikraft-librarian-tests
    PRIVATE
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
)

midikraft_enable_warnings(midikraft-librarian-tests)

add_test(NAME midikraft-librarian-tests COMMAND midikraft-librarian-tests)
