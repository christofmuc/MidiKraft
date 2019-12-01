#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

cmake_minimum_required(VERSION 3.14)

project(Midikraft-Base)

# Find JUCE, and add the components we need
IF(NOT DEFINED JUCE_LIBRARIES)
	SET(JUCE_DIR_HINT "${CMAKE_CURRENT_LIST_DIR}/third_party")
	find_package(JUCE REQUIRED 
		COMPONENTS 
			juce_core		
			juce_events
			juce_audio_basics
			juce_audio_devices
			juce_data_structures
	)
ENDIF()

# Define the sources for the static library
set(Sources
	include/DiscoverableDevice.h
	include/EditBufferHandler.h
	include/MasterkeyboardCapability.h
	include/MidiChannel.h
	include/MidiController.h src/MidiController.cpp
	include/NamedDeviceCapability.h
	include/SimpleDiscoverableDevice.h src/SimpleDiscoverableDevice.cpp
	include/SoundExpanderCapability.h
	include/StepSequencer.h
	include/TypedNamedValue.h		
	README.md
	LICENSE.md
)

# Setup library
add_library(libmidikraft-base ${Sources})
target_include_directories(libmidikraft-base PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include PRIVATE ${JUCE_INCLUDES})

# Pedantic about warnings
if (MSVC)
    # warning level 4 and all warnings as errors
    target_compile_options(libmidikraft-base PRIVATE /W4 /WX)
else()
    # lots of warnings and all warnings as errors
    #target_compile_options(libmidikraft-base PRIVATE -Wall -Wextra -pedantic -Werror)
endif()
