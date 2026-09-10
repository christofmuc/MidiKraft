#pragma once

#include <string>
#include <utility>
#include <vector>

namespace midikraft {

	class DataFile;
	using PatchTextViews = std::vector<std::pair<std::string, std::string>>;

	// Optional, named, preformatted representations of a patch. Names must be unique
	// and stable across patches; an empty list means no extra views are available.
	class PatchTextCapability {
	public:
		virtual ~PatchTextCapability() = default;
		virtual PatchTextViews getClearText(DataFile const& patch) const = 0;
	};

}
