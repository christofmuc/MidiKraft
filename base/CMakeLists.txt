#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

cmake_minimum_required(VERSION 3.14)

project(MidiKraft-Base)

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
	)
ENDIF()

# Define the sources for the static library
set(Sources
	include/BankDumpCapability.h
	include/DiscoverableDevice.h
	include/EditBufferCapability.h
	include/MasterkeyboardCapability.h
	include/MidiBankNumber.h
	include/MidiChannel.h
	include/MidiController.h src/MidiController.cpp
	include/NamedDeviceCapability.h
	include/NrpnDefinition.h src/NrpnDefinition.cpp
	include/Patch.h src/Patch.cpp
	include/PatchNumber.h
	include/ProgramDumpCapability.h
	include/SimpleDiscoverableDevice.h src/SimpleDiscoverableDevice.cpp
	include/SoundExpanderCapability.h
	include/StepSequencer.h
	include/Synth.h src/Synth.cpp
	include/SynthParameterDefinition.h
	include/SysexDataSerializationCapability.h
	include/TypedNamedValue.h		
	README.md
	LICENSE.md
)

# Setup library
add_library(midikraft-base ${Sources})
target_include_directories(midikraft-base PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include PRIVATE ${JUCE_INCLUDES})

# Pedantic about warnings
if (MSVC)
    # warning level 4 and all warnings as errors
    target_compile_options(midikraft-base PRIVATE /W4 /WX)
else()
    # lots of warnings and all warnings as errors
    #target_compile_options(midikraft-base PRIVATE -Wall -Wextra -pedantic -Werror)
endif()
