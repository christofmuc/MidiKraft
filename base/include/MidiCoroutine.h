/*
   Copyright (c) 2023 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include <spdlog/spdlog.h>
#include <coroutine>

namespace midikraft {

	struct MidiMessageWithDevice {
		juce::MidiDeviceInfo device;
		std::vector<juce::MidiMessage> message;
	};

	template <typename TPromise>
	class MidiControllerInput {
	public:
		MidiControllerInput() {
			// Setup the old school callback for our MIDIMessage handler
			callbackHandle_ = MidiController::instance()->makeOneHandle();
			MidiController::instance()->addMessageHandler(callbackHandle_, [this](MidiInput* source, MidiMessage const& message) {
				ignoreUnused(message);
				// This code is executed by the Audio Thread, that handles incoming MIDI messages as well
				ScopedLock lock(taskLock_);
				auto coro = tasks_.begin();
				while (coro != tasks_.end()) {
					if (coro->done()) {
						tasks_.erase(coro);
					}
					else {
						coro->promise().messageIn_ = { source->getDeviceInfo(), {  } };
						coro->promise().signalResume(); // Don't take the Audio thread into the coroutine, but rather release the waiting thread
						coro++;
					}
				}
			});
		}

		~MidiControllerInput() {
			MidiController::instance()->removeMessageHandler(callbackHandle_);
			callbackHandle_ = MidiController::instance()->makeNoneHandle();
		}

		auto suspend() {
			struct awaiter {
				MidiControllerInput& _input;
				std::coroutine_handle<TPromise> handle;

				explicit awaiter(MidiControllerInput& input) : _input(input) {
				}

				bool await_ready() noexcept {
					return false;
				}

				void await_suspend(std::coroutine_handle<TPromise> coro) noexcept {
					{
						ScopedLock lock(_input.taskLock_);
						_input.tasks_.insert(coro);
					}
					handle = coro;
				}

				MidiMessageWithDevice await_resume() {
					if (handle)
						return std::exchange(handle.promise().messageIn_, {});
					else
						return {};
				}

			};

			return awaiter(*this);
		}

	private:
		std::set<std::coroutine_handle<TPromise>> tasks_{};
		MidiController::HandlerHandle callbackHandle_;
		CriticalSection taskLock_;
	};

	template <typename ReturnType>
	class MidiCoroutine {
	public:
		struct promise_type {
			// This creates the Coroutine frame
			MidiCoroutine get_return_object() { 
				return MidiCoroutine{ std::coroutine_handle<promise_type>::from_promise(*this) };
			}

			std::suspend_never initial_suspend() {
				// Do immediately start until the first yield or await
				return {};
			}

			void return_value(ReturnType returnValue) {
				returnValue_ = returnValue;
			}

			void unhandled_exception() {
				jassertfalse;
				spdlog::error("Caught unhandled exception in MidiCoroutine!");
			}

			std::suspend_always final_suspend() noexcept {
				// Last thing before we are shutdown
				return {};
			}

			// We can yield MidiMessages, and they will be sent.
			std::suspend_always yield_value(MidiMessageWithDevice messages) {
				messagesToSend_ = messages;
				return { };
			}

			// We can await on an MidiControllerInput, and we will be suspended if our queue is empty
			auto await_transform(MidiControllerInput<promise_type>& input) noexcept {
				return input.suspend();
			}

			void signalResume() {
				resumeLatch_.notify_one();
			}

			std::condition_variable resumeLatch_;
			MidiMessageWithDevice messagesToSend_;
			MidiMessageWithDevice messageIn_;
			ReturnType returnValue_;
		};

		std::coroutine_handle<promise_type> handle;

		explicit MidiCoroutine(std::coroutine_handle<promise_type> handle_) : handle(handle_) {}
		MidiCoroutine(MidiCoroutine&& rhs) noexcept : handle(std::exchange(rhs.handle, nullptr)) {} // Move only

		~MidiCoroutine() {
			if (handle) {
				handle.destroy();
			}
		}

		MidiMessageWithDevice getNextMessage() {
			return std::move(handle.promise().messagesToSend_);
		}

		ReturnType getResult() {
			return handle.promise().returnValue_;
		}

	};


	template <typename ReturnType>
	ReturnType runMidiCoroutine(MidiCoroutine<ReturnType>& driveThis) {
		std::mutex mtx;
		std::unique_lock<std::mutex> lck(mtx);
		while (!driveThis.handle.done()) {
			auto messagesToSend = driveThis.getNextMessage();
			if (messagesToSend.message.empty()) {
				if (driveThis.handle.promise().resumeLatch_.wait_for(lck, std::chrono::milliseconds(10)) == std::cv_status::timeout) {
					spdlog::debug("Resumed empty message retrieved");
					if (!driveThis.handle.done()) {
						driveThis.handle.resume();
					}
				}
				else {
					if (!driveThis.handle.done()) {
						driveThis.handle.resume();
					}
				}
			}
			else {
				MidiController::instance()->getMidiOutput(messagesToSend.device)->sendBlockOfMessagesFullSpeed(messagesToSend.message);
				spdlog::debug("Resumed after send message");
				if (!driveThis.handle.done()) {
					driveThis.handle.resume();
				}
			}
		}
		return driveThis.getResult();
	}



}