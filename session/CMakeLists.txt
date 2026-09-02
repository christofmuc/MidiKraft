cmake_minimum_required(VERSION 3.14)

set(Sources
	include/ConfiguredSynthInstance.h
	include/SessionTypes.h
	include/SessionCodecs.h
	src/ConfiguredSynthInstance.cpp
	src/SessionCodecs.cpp
)

add_library(midikraft-session STATIC ${Sources})
add_library(midikraft::session ALIAS midikraft-session)

target_include_directories(midikraft-session PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(midikraft-session PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(midikraft-session PUBLIC cxx_std_20)

if(MSVC)
	target_compile_options(midikraft-session PRIVATE /W4 /WX)
else()
	target_compile_options(midikraft-session PRIVATE -Wall -Wextra -pedantic)
endif()

if(MIDIKRAFT_BUILD_SESSION_TESTS)
	add_executable(midikraft-session-tests tests/ConfiguredSynthInstanceTests.cpp tests/SessionCodecsTests.cpp)
	target_link_libraries(midikraft-session-tests PRIVATE midikraft-session nlohmann_json::nlohmann_json)
	target_compile_definitions(midikraft-session-tests PRIVATE
		MIDIKRAFT_SESSION_FIXTURES_DIR="${CMAKE_CURRENT_LIST_DIR}/tests/fixtures")
	add_test(NAME midikraft-session-codecs COMMAND midikraft-session-tests)
endif()
