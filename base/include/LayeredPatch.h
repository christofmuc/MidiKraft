/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

namespace midikraft {

	class LayeredPatch {
	public:
		enum LayerMode {
			SEPARATE,
			STACK,
			SPLIT
		};

		virtual LayerMode layerMode() const = 0;
		virtual int numberOfLayers() const = 0;
		virtual std::string layerName(int layerNo) const = 0;
	};

}
