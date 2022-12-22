cmake_minimum_required(VERSION 3.14)

project(MidiKraft)

option(MIDIKRAFT_BUILD_LIBRARIAN "Select whether to add the optional librarian module" OFF)
option(MIDIKRAFT_BUILD_DATABASE "Select whether to add the optional database (PatchDatabase) module. Requires Librarian" OFF)

if (TARGET fmt)
else()
include(FetchContent)
FetchContent_Declare(fmt URL https://github.com/fmtlib/fmt/releases/download/9.1.0/fmt-9.1.0.zip)
FetchContent_MakeAvailable(fmt)
endif()

add_subdirectory(base)

if (MIDIKRAFT_BUILD_LIBRARIAN)
add_subdirectory(librarian)
endif()

if(MIDIKRAFT_BUILD_DATABASE)
if(MIDIKRAFT_BUILD_LIBRARIAN)
add_subdirectory(database)
else()
message(FATAL_ERROR "Can only add MidiKraft database when also Librarian is selected. Please specify both -DMIDIKRAFT_BUILD_LIBRARIAN and -DMIDIKRAFT_BUILD_DATABASE")
endif()
endif()

