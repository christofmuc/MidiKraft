#
#  Copyright (c) 2020 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

set(SQLITE_CPP_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../../third_party/SQLiteCpp/include")

add_library(midikraft-database STATIC)

target_sources(
    midikraft-database
    PRIVATE
        CategoryBitfield.cpp
        CategoryBitfield.h
        PatchDatabase.cpp
        PatchDatabase.h
        PatchFilter.cpp
        PatchFilter.h
)

target_include_directories(
    midikraft-database
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
    PRIVATE
        ${JUCE_INCLUDES}
        ${SQLITE_CPP_INCLUDE}
)

target_link_libraries(
    midikraft-database
    PUBLIC
        juce-utils
        midikraft-librarian
    PRIVATE
        spdlog::spdlog
)

midikraft_enable_warnings(midikraft-database)
