/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"
#include <map>

namespace midikraft {

	// Controller-owned input lifetime bookkeeping. Native start/stop callbacks run
	// under the same lock as the state transition, including first acquire/last release.
	class MidiInputOwnership {
	public:
		template <typename Start> bool enable(String const &id, Start start) {
			ScopedLock lock(lock_);
			if (!activate(id, start))
				return false;
			entries_.at(id).persistent = true;
			return true;
		}

		template <typename Stop> void disable(String const &id, Stop stop) {
			ScopedLock lock(lock_);
			auto found = entries_.find(id);
			if (found == entries_.end())
				return;
			found->second.persistent = false;
			stopIfUnused(found, stop);
		}

		template <typename Start> bool acquire(String const &id, Start start) {
			ScopedLock lock(lock_);
			if (!activate(id, start))
				return false;
			++entries_.at(id).leases;
			return true;
		}

		template <typename Stop> void release(String const &id, Stop stop) {
			ScopedLock lock(lock_);
			auto found = entries_.find(id);
			if (found == entries_.end() || found->second.leases == 0)
				return;
			--found->second.leases;
			stopIfUnused(found, stop);
		}

		bool isEnabled(String const &id) const {
			ScopedLock lock(lock_);
			auto found = entries_.find(id);
			return found != entries_.end() && found->second.enabled;
		}

		// The native device has disappeared. Keep outstanding leases balanced so an
		// older scope cannot stop a newly acquired connection when the port returns.
		void disconnected(String const &id) {
			ScopedLock lock(lock_);
			auto found = entries_.find(id);
			if (found == entries_.end())
				return;
			found->second.enabled = false;
			found->second.persistent = false;
			if (found->second.leases == 0)
				entries_.erase(found);
		}

	private:
		friend class MidiController;
		struct Entry {
			size_t leases = 0;
			bool persistent = false;
			bool enabled = false;
		};
		using Entries = std::map<String, Entry>;
		mutable CriticalSection lock_;
		Entries entries_;

		template <typename Start> bool activate(String const &id, Start start) {
			if (id.isEmpty())
				return false;
			auto &entry = entries_[id];
			if (entry.enabled)
				return true;
			if (!start()) {
				if (entry.leases == 0)
					entries_.erase(id);
				return false;
			}
			entry.enabled = true;
			return true;
		}

		template <typename Stop> void stopIfUnused(Entries::iterator found, Stop stop) {
			if (found->second.leases != 0 || found->second.persistent)
				return;
			if (found->second.enabled)
				stop();
			entries_.erase(found);
		}
	};
} // namespace midikraft
