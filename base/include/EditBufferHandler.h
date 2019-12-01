#pragma once

#include "../JuceLibraryCode/JuceHeader.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/nil_generator.hpp>

class EditBufferHandler {
public:
	typedef boost::uuids::uuid HandlerHandle;
	static HandlerHandle makeOne() { return boost::uuids::random_generator()(); }
	static HandlerHandle makeNone() { return boost::uuids::nil_generator()(); }

	//TODO - I think these should have an optional expiration date/timeout with a timeout handler, like when the expected response doesn't happen
	virtual void setNextEditBufferHandler(HandlerHandle const &uuid, std::function<void(juce::MidiMessage const &)> handler) = 0;
	virtual bool removeEditBufferHandler(HandlerHandle const &handle) = 0;
};

