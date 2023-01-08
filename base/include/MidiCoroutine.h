/*
   Copyright (c) 2023 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include <spdlog/spdlog.h>
#ifdef __APPLE__
#include <experimental/coroutine>
#else
#include <coroutine>
#endif

namespace midikraft {

	struct MidiMessageWithDevice {
		juce::MidiDeviceInfo device;
		juce::MidiMessage message;
	};

	struct MidiMessagesWithDevice {
		juce::MidiDeviceInfo device;
		std::vector<juce::MidiMessage> messages;
	};

	template <typename ReturnType>
	class MidiCoroutine {
	public:
		// Awaitable marker class to allow 
		// MidiMessageWithDevice incomingMessage = co_await IncomingMidiMessage{}
		// in any MidiCoroutine
		struct IncomingMidiMessage {};

		// Now the internal mechanics on how to create the promise frame
		struct promise_type {
			// This creates the Coroutine frame
			MidiCoroutine get_return_object() { 
				return MidiCoroutine{ std::coroutine_handle<promise_type>::from_promise(*this) };
			}

			std::suspend_never initial_suspend() {
				// Do immediately start MidiCoroutines until their first await, so return suspend_never
				return {};
			}

			// Capture the result of a co_return statement in the MidiCoroutine
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
			/*std::suspend_always yield_value(MidiMessageWithDevice messages) {
				messagesToSend_ = messages;
				return { };
			}*/

			// We can await on an IncomingMidiMessages, and we will be suspended until MIDI messages are coming in
			auto await_transform(IncomingMidiMessage) noexcept {
				struct awaiter {
					std::coroutine_handle<promise_type> handle_;

					bool await_ready() noexcept {
						return false;
					}

					bool await_suspend(std::coroutine_handle<promise_type> coro) noexcept {
						handle_ = coro;
						if (handle_.promise().incomingIsEmpty()) {
							// No more messages enqueued, need to suspend coroutine and wait for resume
							return true;
						}
						else {
							// Message available, can immediately resume
							return false;
						}
					}

					MidiMessageWithDevice await_resume() {
						if (handle_ && !handle_.promise().incomingIsEmpty()) {
							return handle_.promise().nextIncomingMessage();
						}
						else {
							jassertfalse;
							return {};
						}
					}

				};

				return awaiter();
			}

			/*void signalResume() {
				resumeLatch_.notify_one();
			}*/

			//std::condition_variable resumeLatch_;
			//MidiMessageWithDevice messagesToSend_;
			//MidiMessageWithDevice messageIn_;
			ReturnType returnValue_;

			// This is the object that can be awaited on to receive MIDI input
			//std::optional<MidiControllerInput<ReturnType>&> MidiIn;
			MidiMessageWithDevice nextIncomingMessage() {
				if (incomingIsEmpty()) {
					jassertfalse;
					return {};
				}
				auto result = incomingMessages_.front();
				incomingMessages_.pop();
				return result;
			}

			bool incomingIsEmpty() const {
				return incomingMessages_.empty();
			}

			void enqueueNextMessage(MidiInput* source, MidiMessage const& message) {
				incomingMessages_.push({ source->getDeviceInfo(), message });
			}

		private:
			std::queue<MidiMessageWithDevice> incomingMessages_;
		};

		std::coroutine_handle<promise_type> handle;

		explicit MidiCoroutine(std::coroutine_handle<promise_type> handle_) : handle(handle_) {
			callbackHandle_ = MidiController::instance()->makeOneHandle();
			MidiController::instance()->addMessageHandler(callbackHandle_, [this](MidiInput* source, MidiMessage const& message) {
				// This code is executed by the Audio Thread, that handles incoming MIDI messages as well					
				if (handle) {
					handle.promise().enqueueNextMessage(source, message);
					handle.resume();
				}
			});
		}
		
		MidiCoroutine(MidiCoroutine&& rhs) noexcept : handle(std::exchange(rhs.handle, nullptr)) {
			callbackHandle_ = MidiController::instance()->makeOneHandle();
			MidiController::instance()->addMessageHandler(callbackHandle_, [this](MidiInput* source, MidiMessage const& message) {
				// This code is executed by the Audio Thread, that handles incoming MIDI messages as well					
				if (handle) {
					handle.promise().enqueueNextMessage(source, message);
					handle.resume();
				}
				});
		}

		~MidiCoroutine() {
			MidiController::instance()->removeMessageHandler(callbackHandle_);
			callbackHandle_ = MidiController::instance()->makeNoneHandle();
			if (handle) {
				handle.destroy();
			}
		}

		/*MidiMessageAwaiter await_transform() {

		}*/

		ReturnType getResult() {
			return handle.promise().returnValue_;
		}

	private:
		MidiController::HandlerHandle callbackHandle_;
	};

	template <typename ReturnType>
	ReturnType awaitMidiCoroutine(MidiCoroutine<ReturnType> driveThis) {
		// Use this to block our thread and wait for input in case nothing is to be done
		do {
			driveThis.handle.resume();
			if (!driveThis.handle.done()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		} while (!driveThis.handle.done());
		return driveThis.getResult();
	}

	template <typename ReturnType>
	void runMidiCoroutineWithCallback(MidiCoroutine<ReturnType> driveThis, std::function<void(ReturnType const&)> resultHandler) {
		std::thread([resultHandler, coro = std::move(driveThis)]() mutable {
			// Use this to block our thread and wait for input in case nothing is to be done
			do {
				coro.handle.resume();
				if (!coro.handle.done()) {
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
			} while (!coro.handle.done());
			resultHandler(coro.getResult());
			}).detach();
	}

	//
	// This is the scheduler that occupies the current thread and is feeding MIDI input into coroutines via the 
	// MidiControllerInput class and sends out messages yielded by MidiCoroutines
	/*class MidiCoroutineRunner {
	public:
		MidiCoroutineRunner() : midiInput_(*this) {
		}

		MidiControllerInput midiInput_;
		std::set<std::coroutine_handle<MidiCoroutine<>>> tasks_{};
		CriticalSection taskLock_;

		void enqueueIncomingMessage(MidiInput* source, MidiMessage const& message) {
			ScopedLock lock(taskLock_);
			auto coro = tasks_.begin();
			while (coro != tasks_.end()) {
				if (coro->done()) {
					tasks_.erase(coro);
				}
				else {
					coro->promise().messageIn_ = { source->getDeviceInfo(), {  message } };
					coro->promise().signalResume(); // Don't take the Audio thread into the coroutine, but rather release the waiting thread
					coro++;
				}
			}
		}

		void registerAwait(std::coroutine_handle<MidiCoroutine<>> handle)
		{
			// This will be called by each co_await input 
			// Therefore, we use a task set to not register each coroutine again and again
			ScopedLock lock(taskLock_);
			tasks_.insert(handle);
		}

		ReturnType runMidiCoroutine(MidiCoroutine<ReturnType>& driveThis) {
			// Provide Coroutine with input object
			driveThis.MidiIn = midiInput_;
			// Use this to block our thread and wait for input in case nothing is to be done
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
	}*/



}