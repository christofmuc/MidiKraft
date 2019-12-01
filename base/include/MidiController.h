#pragma once

#include "JuceHeader.h"

#include "EditBufferHandler.h"
#include "SimpleDiscoverableDevice.h"

#include <map>

class MidiController;
class DiscoverableDevice;
class SimpleLogger;

class SafeMidiOutput {
public:
	SafeMidiOutput(MidiController *controller, MidiOutput *midiOutput);

	void sendMessageNow(const MidiMessage& message);
	void sendBlockOfMessagesNow(const MidiBuffer& buffer);

private:
	MidiOutput * midiOut_;
	MidiController *controller_;
};

class MidiController : public MidiInputCallback, public EditBufferHandler
{
public:
	MidiController();
	~MidiController();

	static MidiController *instance();

	void setMidiLogFunction(std::function<void(const MidiMessage& message, const String& source, bool)>);
	void logMidiMessage(const MidiMessage& message, const String& source, bool isOut);

	bool enableMidiOutput(std::string const &newOutput);
	SafeMidiOutput *getMidiOutput(std::string const &name);
	void enableMidiInput(std::string const &newInput);
	void disableMidiInput(std::string const &input);

	// Implementation of Callback
	virtual void handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message);

	// Implementation of the editBufferHandler
	virtual void setNextEditBufferHandler(HandlerHandle const &handle, std::function<void(MidiMessage const &)> handler) override;
	virtual bool removeEditBufferHandler(HandlerHandle const &handle) override;
	
	void addMessageHandler(HandlerHandle const &handle, std::function<void(MidiInput *source, MidiMessage const &message)> handler);
	bool removeMessageHandler(HandlerHandle const &handle);

private:
	static MidiController *instance_;
	AudioDeviceManager deviceManager;
	std::map<EditBufferHandler::HandlerHandle, std::function<void(MidiMessage const &)>> handlerForNextMessage_;
	std::map<EditBufferHandler::HandlerHandle, std::function<void(MidiInput *, MidiMessage const &)>> messageHandlers_;
	std::map< std::string, std::unique_ptr<MidiOutput>> outputsOpen_;
	std::map< std::string, SafeMidiOutput *> safeOutputs_;
	std::map< std::string, MidiInputCallback *>  callbacks_;
	std::function<void(const MidiMessage& message, const String& source, bool)> midiLogFunction_;
};

