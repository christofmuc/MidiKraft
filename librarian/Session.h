/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <string>

#include "PatchHolder.h"

namespace midikraft {

	struct Session {
		std::string name_;

		Session(std::string const &name) : name_(name) {
		}
	};

	struct SessionPatch {
		Session session_;
		std::string synthName_;
		std::string patchName_;
		PatchHolder patchHolder_;

		SessionPatch(Session session, std::string const &synthName, std::string const &patchName, PatchHolder patchholder) :
			session_(session), synthName_(synthName), patchName_(patchName), patchHolder_(patchholder) {
		}
	};

}
