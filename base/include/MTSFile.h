#pragma once

#include "JuceHeader.h"

#include "Patch.h"

namespace midikraft {

	class MTSFile : public DataFile {
	public:
		MTSFile(Synth::PatchData const &data) : DataFile(data) {}

		std::string patchName() const override
		{
			return "not implemented yet";
		}


		void setName(std::string const &name) override
		{
			ignoreUnused(name);
			throw new std::runtime_error("not implemented yet");
		}

	};

}


