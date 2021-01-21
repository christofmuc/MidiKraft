#
#  Copyright (c) 2020 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

cmake_minimum_required(VERSION 3.14)

project(MidiKraft-database)

# Define the sources for the static library
set(Sources
	PatchDatabase.cpp PatchDatabase.h
	README.md
	LICENSE.md
)

set(SQLITE_CPP_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../third_party/SQLiteCpp/include")

# Setup library
add_library(midikraft-database ${Sources})
target_include_directories(midikraft-database PUBLIC ${CMAKE_CURRENT_LIST_DIR} PRIVATE ${JUCE_INCLUDES} ${boost_SOURCE_DIR} ${MANUALLY_RAPID_JSON} ${SQLITE_CPP_INCLUDE})
target_link_libraries(midikraft-database juce-utils midikraft-base midikraft-librarian ${APPLE_BOOST})

# Pedantic about warnings
if (MSVC)
    # warning level 4 and all warnings as errors
    #target_compile_options(midikraft-database PRIVATE /W4 /WX)
else()
    # lots of warnings and all warnings as errors
    #target_compile_options(midikraft-librarian PRIVATE -Wall -Wextra -pedantic -Werror)
endif()
