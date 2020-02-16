#
#  Copyright (c) 2020 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

cmake_minimum_required(VERSION 3.14)

project(MidiKraft-librarian)

# Find JUCE, and add the components we need
IF(NOT DEFINED JUCE_LIBRARIES)
	SET(JUCE_DIR_HINT "${CMAKE_CURRENT_LIST_DIR}/../third_party")
	find_package(JUCE REQUIRED 
		COMPONENTS 
			juce_core		
			juce_events
			juce_audio_basics
			juce_audio_devices
			juce_data_structures
			juce_graphics # For the Colour class
			juce_gui_basics # ThreadWithProgressWindow
			juce_cryptography # only for MD5
	)
ENDIF()

# Define the sources for the static library
set(Sources
	AutomaticCategory.cpp AutomaticCategory.h
	Category.cpp Category.h
	Librarian.cpp Librarian.h
	PatchHolder.cpp PatchHolder.h
	RapidJsonHelper.cpp RapidJsonHelper.h
	Session.h
	SynthHolder.cpp SynthHolder.h
	README.md
	LICENSE.md
	${PATCH_FILES}
)

# Setup library
add_library(midikraft-librarian ${Sources})
target_include_directories(midikraft-librarian PUBLIC ${CMAKE_CURRENT_LIST_DIR} PRIVATE ${JUCE_INCLUDES} ${boost_SOURCE_DIR} ${MANUALLY_RAPID_JSON})
target_link_libraries(midikraft-librarian juce-utils midikraft-base)

# Pedantic about warnings
if (MSVC)
    # warning level 4 and all warnings as errors
    target_compile_options(midikraft-librarian PRIVATE /W4 /WX)
else()
    # lots of warnings and all warnings as errors
    #target_compile_options(midikraft-librarian PRIVATE -Wall -Wextra -pedantic -Werror)
endif()
