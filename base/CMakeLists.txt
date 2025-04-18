#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

cmake_minimum_required(VERSION 3.14)

project(MidiKraft-Base)

# Define the sources for the static library
set(Sources
	include/Additive.h src/Additive.cpp
	include/AdditiveCapability.h
	include/AutoDetection.h src/AutoDetection.cpp
	include/BankDumpCapability.h
	include/BidirectionalSyncCapability.h
	include/Capability.h
	include/CreateInitPatchDataCapability.h
	include/DataFileLoadCapability.h
	include/DataFileSendCapability.h
	include/DetailedParametersCapability.h
	include/DiscoverableDevice.h src/DiscoverableDevice.cpp 
	include/DrawbarOrgan.h src/DrawbarOrgan.cpp 
	include/DynamicPlugin.h
	include/EditBufferCapability.h
	include/FindSynthOnMidiNetwork.h src/FindSynthOnMidiNetwork.cpp
	include/JuceHeader.h
	include/GlobalSettingsCapability.h	
	include/HandshakeLoadingCapability.h
	include/HasBanksCapability.h
	include/HybridWaveCapability.h
	include/LayerCapability.h
	include/LayeredPatchCapability.h
	include/LoopDetection.h src/LoopDetection.cpp
	include/MasterkeyboardCapability.h	
	include/MidiClockCapability.h
	include/MidiController.h src/MidiController.cpp
	include/MidiLocationCapability.h
	include/MidiRequest.h src/MidiRequest.cpp
	include/MTSFile.h src/MTSFile.cpp
	include/NamedDeviceCapability.h	
	include/Patch.h src/Patch.cpp
	include/ProgramDumpCapability.h
	include/ReadonlySoundExpander.h src/ReadonlySoundExpander.cpp
	include/SendsProgramChangeCapability.h
	include/SimpleDiscoverableDevice.h src/SimpleDiscoverableDevice.cpp
	include/SoundExpanderCapability.h
	include/StepSequencer.h
	include/StoredPatchNameCapability.h
	include/StoredPatchNumberCapability.h
	include/StoredTagCapability.h
	include/StreamLoadCapability.h
	include/Synth.h src/Synth.cpp
	include/SynthParameterDefinition.h
	include/SysexDataSerializationCapability.h
	include/Tag.h src/Tag.cpp
	include/TimedMidiSender.h src/TimedMidiSender.cpp 
)

# Setup library
add_library(midikraft-base ${Sources})
target_include_directories(midikraft-base PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(midikraft-base PUBLIC juce-utils spdlog::spdlog)

# Pedantic about warnings
if (MSVC)
    # warning level 4 and all warnings as errors
    target_compile_options(midikraft-base PRIVATE /W4 /WX)
else()
    # lots of warnings and all warnings as errors
    target_compile_options(midikraft-base PRIVATE -Wall -Wextra -pedantic -Wno-deprecated-declarations) #-Werror )
endif()
