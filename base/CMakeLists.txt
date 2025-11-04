#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

add_library(midikraft-base STATIC)

target_sources(
    midikraft-base
    PRIVATE
        include/Additive.h
        include/AdditiveCapability.h
        include/AutoDetection.h
        include/BankDumpCapability.h
        include/BidirectionalSyncCapability.h
        include/Capability.h
        include/CreateInitPatchDataCapability.h
        include/DataFileLoadCapability.h
        include/DataFileSendCapability.h
        include/DetailedParametersCapability.h
        include/DiscoverableDevice.h
        include/DrawbarOrgan.h
        include/DynamicPlugin.h
        include/EditBufferCapability.h
        include/FindSynthOnMidiNetwork.h
        include/GlobalSettingsCapability.h
        include/HandshakeLoadingCapability.h
        include/HasBanksCapability.h
        include/HybridWaveCapability.h
        include/JuceHeader.h
        include/LayerCapability.h
        include/LayeredPatchCapability.h
        include/LoopDetection.h
        include/MasterkeyboardCapability.h
        include/MidiClockCapability.h
        include/MidiController.h
        include/MidiLocationCapability.h
        include/MidiRequest.h
        include/MTSFile.h
        include/NamedDeviceCapability.h
        include/Patch.h
        include/ProgramDumpCapability.h
        include/ReadonlySoundExpander.h
        include/SendsProgramChangeCapability.h
        include/SimpleDiscoverableDevice.h
        include/SoundExpanderCapability.h
        include/StepSequencer.h
        include/StoredPatchNameCapability.h
        include/StoredPatchNumberCapability.h
        include/StoredTagCapability.h
        include/StreamLoadCapability.h
        include/Synth.h
        include/SynthParameterDefinition.h
        include/SysexDataSerializationCapability.h
        include/Tag.h
        include/TimedMidiSender.h
        src/Additive.cpp
        src/AutoDetection.cpp
        src/DiscoverableDevice.cpp
        src/DrawbarOrgan.cpp
        src/FindSynthOnMidiNetwork.cpp
        src/LoopDetection.cpp
        src/MTSFile.cpp
        src/MidiController.cpp
        src/MidiRequest.cpp
        src/Patch.cpp
        src/ReadonlySoundExpander.cpp
        src/SimpleDiscoverableDevice.cpp
        src/Synth.cpp
        src/Tag.cpp
        src/TimedMidiSender.cpp
)

target_include_directories(
    midikraft-base
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(
    midikraft-base
    PUBLIC
        juce-utils
        spdlog::spdlog
        fmt::fmt
)

target_compile_features(midikraft-base PUBLIC cxx_std_17)
midikraft_enable_warnings(midikraft-base)
