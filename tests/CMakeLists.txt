add_executable(midikraft-base-tests base/test_midikraft_base_smoke.cpp)

target_link_libraries(
    midikraft-base-tests
    PRIVATE
        midikraft-base
        doctest::doctest
)

midikraft_enable_warnings(midikraft-base-tests)

add_test(NAME midikraft-base-tests COMMAND midikraft-base-tests)
