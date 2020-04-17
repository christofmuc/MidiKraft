#pragma once

#include "JuceHeader.h"

#include "Synth.h"

namespace midikraft {

	class LegacyLoaderCapability {
	public:
		virtual std::string additionalFileExtensions() = 0;
		virtual bool supportsExtension(std::string const &filename) = 0;
		virtual TPatchVector load(std::string const &filename, std::vector<uint8> const &fileContent) = 0;
	};
}

