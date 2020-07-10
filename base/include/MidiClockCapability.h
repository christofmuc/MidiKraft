#pragma once

#include "JuceHeader.h"

namespace midikraft {

	class MidiClockCapability {
	public:
		enum class ClockMode {
			INVALID, OFF, MASTER, SLAVE, SLAVE_NO_START_STOP, SLAVE_THROUGH
		};

		virtual void setMidiClockMode(ClockMode clockMode) = 0;
		virtual ClockMode getMidiClockMode() = 0;
		virtual Value getMidiClockModeValue() = 0;
		virtual std::vector<ClockMode> getSupportedClockModes() = 0;
	};


}

