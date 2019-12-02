#pragma once

#include <stdexcept>

class MidiBankNumber {
public:
	static MidiBankNumber fromOneBase(int bankNo) { return MidiBankNumber(bankNo - 1); }
	static MidiBankNumber fromZeroBase(int bankNo) { return MidiBankNumber(bankNo); };
	static MidiBankNumber invalid() { return MidiBankNumber(-1); };

	int isValid() const { return bankNo_ != -1; }
	int toZeroBased() const {
		if (!isValid()) {
			throw new std::runtime_error("Illegal State");
		}
		return bankNo_;
	}

	int toOneBased() const {
		if (!isValid()) {
			throw new std::runtime_error("Illegal State");
		}
		return bankNo_ + 1;
	}

private:
	MidiBankNumber(int zeroBasedNumber) : bankNo_(zeroBasedNumber) {}

	int bankNo_;

};
