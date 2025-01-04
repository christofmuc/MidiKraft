/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <vector>

namespace midikraft {

	class LayeredPatchCapability {
	public:
		enum LayerMode {
			SEPARATE,
			STACK,
			SPLIT
		};

		virtual LayerMode layerMode() const = 0;
		virtual int numberOfLayers() const = 0;
		virtual std::vector<std::string> layerTitles() const = 0;
		virtual std::string layerName(int layerNo) const = 0;
		virtual void setLayerName(int layerNo, std::string const &layerName) = 0;
	};

}
