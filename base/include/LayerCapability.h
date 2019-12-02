/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

namespace midikraft {

	class LayerCapability {
	public:
		virtual void switchToLayer(int layerNo) = 0;
	};

}
