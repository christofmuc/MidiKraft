#
#  Copyright (c) 2020 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

set(LIBRARIAN_RESOURCE_FILES
    resources/automatic_categories.jsonc
    resources/mapping_categories.jsonc
)

set(LIBRARIAN_GENERATED_HEADER "${CMAKE_CURRENT_BINARY_DIR}/BinaryResources.h")

add_custom_command(
    OUTPUT ${LIBRARIAN_GENERATED_HEADER}
    COMMAND
        ${CMAKE_COMMAND}
        -Dresource_dir=${CMAKE_CURRENT_SOURCE_DIR}/resources
        -Doutput=${LIBRARIAN_GENERATED_HEADER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/createResources.cmake
    DEPENDS
        ${LIBRARIAN_RESOURCE_FILES}
        ${CMAKE_CURRENT_SOURCE_DIR}/createResources.cmake
    COMMENT "Compiling binary resources"
    VERBATIM
)

add_library(midikraft-librarian STATIC)

target_sources(
    midikraft-librarian
    PRIVATE
        AutomaticCategory.cpp
        AutomaticCategory.h
        Category.cpp
        Category.h
        JsonSchema.cpp
        JsonSchema.h
        JsonSerialization.cpp
        JsonSerialization.h
        Librarian.cpp
        Librarian.h
        PatchHolder.cpp
        PatchHolder.h
        PatchInterchangeFormat.cpp
        PatchInterchangeFormat.h
        PatchList.cpp
        PatchList.h
        SynthBank.cpp
        SynthBank.h
        SynthHolder.cpp
        SynthHolder.h
        ${LIBRARIAN_RESOURCE_FILES}
        ${LIBRARIAN_GENERATED_HEADER}
)

set_source_files_properties(${LIBRARIAN_GENERATED_HEADER} PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)
set_source_files_properties(${LIBRARIAN_RESOURCE_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

target_include_directories(
    midikraft-librarian
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(
    midikraft-librarian
    PUBLIC
        juce-utils
        midikraft-base
        nlohmann_json::nlohmann_json
    PRIVATE
        fmt::fmt
        spdlog::spdlog
)

midikraft_enable_warnings(midikraft-librarian)
