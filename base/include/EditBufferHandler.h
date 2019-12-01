/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

class EditBufferHandler {
public:
	typedef juce::Uuid HandlerHandle;
	static HandlerHandle makeOne() { return juce::Uuid(); }
	static HandlerHandle makeNone() { return juce::Uuid::null(); }

	//TODO - I think these should have an optional expiration date/timeout with a timeout handler, like when the expected response doesn't happen
	virtual void setNextEditBufferHandler(HandlerHandle const &uuid, std::function<void(juce::MidiMessage const &)> handler) = 0;
	virtual bool removeEditBufferHandler(HandlerHandle const &handle) = 0;
};

