#pragma once

class MidiProgramNumber {
public:
	static MidiProgramNumber fromOneBase(int programNumber) { return MidiProgramNumber(programNumber - 1); }
	static MidiProgramNumber fromZeroBase(int programNumber) { return MidiProgramNumber(programNumber); };

	int toZeroBased() const { return programNo_;  }
	int toOneBased() const { return programNo_ + 1; }

private:
	MidiProgramNumber(int zeroBasedNumber) : programNo_(zeroBasedNumber) {}

	int programNo_;
};
