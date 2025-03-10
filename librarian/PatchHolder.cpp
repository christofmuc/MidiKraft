/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PatchHolder.h"

#include "Capability.h"

#include "AutomaticCategory.h"
#include "StoredPatchNameCapability.h"
#include "HasBanksCapability.h"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

namespace midikraft {

	const char
		*kFileSource = "filesource",
		*kSynthSource = "synthsource",
		*kBulkSource = "bulksource",
		*kFileInBulk = "fileInBulk",
		*kFileName = "filename",
		*kFullPath = "fullpath",
		*kTimeStamp = "timestamp",
		*kBankNumber = "banknumber",
		*kProgramNo = "program";

	PatchHolder::PatchHolder(std::shared_ptr<Synth> activeSynth, std::shared_ptr<SourceInfo> sourceInfo, std::shared_ptr<DataFile> patch, 
		std::shared_ptr<AutomaticCategory> detector /* = nullptr */)
		: patch_(patch)
            , synth_(activeSynth)
            , isFavorite_(Favorite())
            , isHidden_(false)
            , bankNumber_(MidiBankNumber::invalid())
            , patchNumber_(MidiProgramNumber::invalidProgram())
            , sourceInfo_(sourceInfo)
    {
		if (patch) {
			name_ = activeSynth->nameForPatch(patch);
			if (detector) {
				categories_ = detector->determineAutomaticCategories(*this);
			}
		}
	}

	PatchHolder::PatchHolder() : isFavorite_(Favorite()), isHidden_(false), bankNumber_(MidiBankNumber::invalid()), patchNumber_(MidiProgramNumber::invalidProgram())
	{
	}

	std::shared_ptr<DataFile> PatchHolder::patch() const
	{
		return patch_;
	}

	midikraft::Synth * PatchHolder::synth() const
	{
		return !synth_.expired() ? synth_.lock().get() : nullptr;
	}

	std::shared_ptr<midikraft::Synth> PatchHolder::smartSynth() const
	{
		return synth_.expired() ? nullptr : synth_.lock();
	}

	int PatchHolder::getType() const
	{
		return patch_->dataTypeID();
	}

	void PatchHolder::setName(std::string const &newName)
	{
		auto storedInPatch = midikraft::Capability::hasCapability<StoredPatchNameCapability>(patch());
		if (storedInPatch && storedInPatch->changeNameStoredInPatch(newName)) {
			// If the Patch can do it, poke the name into the patch, and then use the result (limited to the characters the synth can do) for the patch holder as well
			name_ = storedInPatch->name();
		}
		else {
			// The name is only stored in the PatchHolder, and thus the database, anyway, so we just accept the string
			name_ = newName;
		}
	}

	std::string PatchHolder::name() const
	{
		return name_;
	}

	void PatchHolder::setSourceId(std::string const &source_id)
	{
		sourceId_ = source_id;
	}

	std::string PatchHolder::sourceId() const
	{
		return sourceId_;
	}

	void PatchHolder::setPatchNumber(MidiProgramNumber number)
	{
		patchNumber_ = number;
	}

	MidiProgramNumber PatchHolder::patchNumber() const
	{
		return patchNumber_;
	}

	void PatchHolder::setBank(MidiBankNumber bank)
	{
		bankNumber_ = bank;
	}

	MidiBankNumber PatchHolder::bankNumber() const
	{
		return bankNumber_;
	}

	bool PatchHolder::isFavorite() const
	{
		return isFavorite_.is() == Favorite::TFavorite::YES;
	}

	Favorite PatchHolder::howFavorite() const
	{
		return isFavorite_;
	}

	void PatchHolder::setFavorite(Favorite fav)
	{
		isFavorite_ = fav;
	}

	void PatchHolder::setSourceInfo(std::shared_ptr<SourceInfo> newSourceInfo)
	{
		sourceInfo_ = newSourceInfo;
	}

	bool PatchHolder::isHidden() const
	{
		return isHidden_;
	}

	void PatchHolder::setHidden(bool isHidden)
	{
		isHidden_ = isHidden;
	}

	bool PatchHolder::hasCategory(Category const &category) const
	{
		return categories_.find(category) != categories_.end();
	}

	void PatchHolder::setCategory(Category const &category, bool hasIt)
	{
		if (!hasIt) {
			if (hasCategory(category)) {
				categories_.erase(category);
			}
		}
		else {
			categories_.insert(category);
		}
	}

	void PatchHolder::setCategories(std::set<Category> const &cats)
	{
		categories_ = cats;
	}

	void PatchHolder::clearCategories()
	{
		categories_.clear();
	}

	std::set<Category> PatchHolder::categories() const
	{
		return categories_;
	}

	std::set<midikraft::Category> PatchHolder::userDecisionSet() const
	{
		return userDecisions_;
	}

	std::shared_ptr<SourceInfo> PatchHolder::sourceInfo() const
	{
		return sourceInfo_;
	}

	std::string PatchHolder::comment() const
	{
		return comment_;
	}

	void PatchHolder::setComment(std::string const& newComment)
	{
		comment_ = newComment;
	}

	std::string PatchHolder::author() const
	{
		return author_;
	}

	void PatchHolder::setAuthor(std::string const& newAuthor)
	{
		author_ = newAuthor;
	}

	std::string PatchHolder::info() const
	{
		return info_;
	}

	void PatchHolder::setInfo(std::string const& info)
	{
		info_ = info;
	}

	bool PatchHolder::autoCategorizeAgain(std::shared_ptr<AutomaticCategory> detector)
	{
		auto previous = categories();
		auto newCategories = detector->determineAutomaticCategories(*this);
		if (previous != newCategories) {
			for (auto n : newCategories) {
				if (userDecisions_.find(n) == userDecisions_.end()) {
					// For this category no user decision has been recorded, so we can safely set it!
					categories_.insert(n);
				}
			}
			for (auto o : previous) {
				if (newCategories.find(o) == newCategories.end()) {
					// This category has been removed by the auto categorizer, let's check if there is no user decision on it!
					if (userDecisions_.find(o) == userDecisions_.end()) {
						categories_.erase(o);
					}
				}
			}
			return previous != categories_;
		}
		else {
			return false;
		}
	}

	std::string PatchHolder::md5() const
	{
		if (!synth_.expired() && patch_)
			return synth_.lock()->calculateFingerprint(patch_);
		else
			return "empty";
	}

	std::string PatchHolder::createDragInfoString() const
	{
		// The drag info should be... "PATCH", synth, type, and md5
		nlohmann::json dragInfo = {
			{ "drag_type", "PATCH"},
			{ "synth", synth_.lock()->getName()},
			{ "data_type", patch_ ? patch_->dataTypeID() : 0},
			{ "patch_name", name()},
			{ "md5", md5() }
		};
		return dragInfo.dump(-1, ' ', true, nlohmann::detail::error_handler_t::replace); // Force ASCII, else we get UTF8 exceptions when using some old synths data. Like the MKS50...
	}

	nlohmann::json PatchHolder::dragInfoFromString(std::string s) {
		try {
			return nlohmann::json::parse(s);
		}
		catch (nlohmann::json::parse_error& e) {
			spdlog::error("Error parsing drop target: {}", e.what());
			return {};
		}
	}

	bool PatchHolder::dragItemIsPatch(nlohmann::json const& infos)
	{
		return infos.contains("drag_type") && (infos["drag_type"] == "PATCH" || infos["drag_type"] == "PATCH_IN_LIST");
	}

	bool PatchHolder::dragItemIsList(nlohmann::json const& infos)
	{
		return infos.contains("drag_type") && infos["drag_type"] == "LIST";
	}

	void PatchHolder::setUserDecision(Category const& clicked)
	{
		userDecisions_.insert(clicked);
	}

	void PatchHolder::setUserDecisions(std::set<Category> const &cats)
	{
		userDecisions_ = cats;
	}

	Favorite::Favorite() : favorite_(TFavorite::DONTKNOW)
	{
	}

	Favorite::Favorite(bool isFavorite) : favorite_(isFavorite ? TFavorite::YES : TFavorite::NO)
	{
	}

	Favorite::Favorite(int howFavorite)
	{
		switch (howFavorite) {
		case -1:
			favorite_ = TFavorite::DONTKNOW;
			break;
		case 0:
			favorite_ = TFavorite::NO;
			break;
		case 1:
			favorite_ = TFavorite::YES;
			break;
		default:
			jassert(false);
			favorite_ = TFavorite::DONTKNOW;
		}
	}

	Favorite::TFavorite Favorite::is() const
	{
		return favorite_;
	}

	std::string SourceInfo::toString() const
	{
		return jsonRep_;
	}

	std::shared_ptr<SourceInfo> SourceInfo::fromString(std::shared_ptr<Synth> synth, std::string const &str)
	{
		try {
			auto doc = nlohmann::json::parse(str);
			if (doc.is_object()) {
				if (doc.contains(kFileSource)) {
                    return FromFileSource::fromString(synth, str);
				}
				else if (doc.contains(kSynthSource)) {
					return FromSynthSource::fromString(str);
				}
				else if (doc.contains(kBulkSource)) {
					return FromBulkImportSource::fromString(synth, str);
				}
			}
			spdlog::error("Json string does not contain correct source info type: {}", str);
		}
		catch (nlohmann::json::exception const& e) {
			spdlog::error("JSON error parsing source information of patch: {}", e.what());
		}
		return nullptr;
	}

	bool SourceInfo::isEditBufferImport(std::shared_ptr<SourceInfo> sourceInfo)
	{
		auto synthSource = std::dynamic_pointer_cast<FromSynthSource>(sourceInfo);
		return (synthSource && !synthSource->bankNumber().isValid());
	}

	FromSynthSource::FromSynthSource(Time timestamp, MidiBankNumber bankNo) : timestamp_(timestamp), bankNo_(bankNo)
	{
		nlohmann::json doc;
		std::string timestring = timestamp.toISO8601(true).toStdString();
		doc[kSynthSource] = true;
		doc[kTimeStamp] = timestring;
		if (bankNo.isValid()) {
			doc[kBankNumber] = bankNo.toZeroBased();
		}
		jsonRep_ = doc.dump();
	}

	FromSynthSource::FromSynthSource(Time timestamp) : FromSynthSource(timestamp, MidiBankNumber::invalid())
	{
	}

	std::string FromSynthSource::md5(Synth *synth) const
	{
		String displayString(toDisplayString(synth, false));
		return MD5(displayString.toUTF8()).toHexString().toStdString();
	}

	std::string FromSynthSource::toDisplayString(Synth *synth, bool shortVersion) const
	{
		ignoreUnused(shortVersion);
		std::string bank = "";
		if (bankNo_.isValid()) {
			auto descriptors = Capability::hasCapability<HasBankDescriptorsCapability>(synth);
			if (descriptors) {
				auto banks = descriptors->bankDescriptors();
				if (bankNo_.toZeroBased() < static_cast<int>(banks.size())) {
					bank = " " + banks[(size_t) bankNo_.toZeroBased()].name;
				}
				else {
					bank = fmt::format(" bank {}", bankNo_.toOneBased());
				}
			}
			else {
				auto bankCapa = Capability::hasCapability<HasBanksCapability>(synth);
				if (bankCapa) {
					bank = " " + bankCapa->friendlyBankName(bankNo_);
				}
				else {
					bank = fmt::format(" bank {}", bankNo_.toOneBased());;
				}
			}
		}
		else {
			bank = " edit buffer";
		}
		if (timestamp_.toMilliseconds() != 0) {
			// https://docs.juce.com/master/classTime.html#afe9d0c7308b6e75fbb5e5d7b76262825
			return fmt::format("Imported from synth{} on {}", bank, timestamp_.formatted("%x at %X").toStdString());
		}
		else {
			// Legacy import, no timestamp was recorded.
			return fmt::format("Imported from synth{}", bank);
		}
	}

	std::shared_ptr<FromSynthSource> FromSynthSource::fromString(std::string const &jsonString)
	{
		auto doc = nlohmann::json::parse(jsonString);
		if (doc.is_object()) {
			if (doc.contains(kSynthSource)) {
				Time timestamp;
				if (doc.contains(kTimeStamp)) {
					std::string timestring = doc[kTimeStamp];
					timestamp = Time::fromISO8601(timestring);
				}
				MidiBankNumber bankNo = MidiBankNumber::invalid();
				if (doc.contains(kBankNumber)) {
					//TODO - a bank size of -1 seems to ask for trouble
					//jassertfalse;
					bankNo = MidiBankNumber::fromZeroBase(doc[kBankNumber].get<int>(), -1);
				}
				return std::make_shared<FromSynthSource>(timestamp, bankNo);
			}
		}
		return nullptr;
	}

	MidiBankNumber FromSynthSource::bankNumber() const
	{
		return bankNo_;
	}

	FromFileSource::FromFileSource(std::string const &filename, std::string const &fullpath, MidiProgramNumber program) : filename_(filename), fullpath_(fullpath), program_(program)
	{
		nlohmann::json doc;
		doc[kFileSource] = true;
		doc[kFileName] = filename;
		doc[kFullPath] = fullpath;
		if (program.bank().isValid()) {
			doc[kBankNumber] = program.bank().toZeroBased();
			doc[kProgramNo] = program.toZeroBasedWithBank();
		}
		else
		{
			doc[kProgramNo] = program.toZeroBasedDiscardingBank();
		}
		jsonRep_ = doc.dump();

	}

	std::string FromFileSource::md5(Synth *synth) const
	{
		String displayString(toDisplayString(synth, true));
		return MD5(displayString.toUTF8()).toHexString().toStdString();
	}

	std::string FromFileSource::toDisplayString(Synth *, bool shortVersion) const
	{
		ignoreUnused(shortVersion);
		return fmt::format("Imported from file {}", filename_);
	}

	std::shared_ptr<FromFileSource> FromFileSource::fromString(std::shared_ptr<Synth> synth, std::string const &jsonString)
	{
		auto obj = nlohmann::json::parse(jsonString);
		if (obj.contains(kFileSource)) {
			std::string filename = obj[kFileName].get<std::string>();
			std::string fullpath = obj[kFullPath].get<std::string>();
			MidiProgramNumber program = MidiProgramNumber::invalidProgram();
			if (obj.contains(kBankNumber)) {
				int bankNo = obj[kBankNumber].get<int>();
				// Need to determine the size of the bank
				MidiBankNumber bank = Synth::bankNumberFromInt(synth, bankNo);
				program = MidiProgramNumber::fromZeroBaseWithBank(bank, bankNo);
			}
			else {
				program = MidiProgramNumber::fromZeroBase(obj[kProgramNo].get<int>());
			}
			return std::make_shared<FromFileSource>(filename, fullpath, program);
		}
		return nullptr;
	}

	FromBulkImportSource::FromBulkImportSource(Time timestamp, std::shared_ptr<SourceInfo> individualInfo) : timestamp_(timestamp), individualInfo_(individualInfo)
	{
		nlohmann::json doc;
		std::string timestring = timestamp.toISO8601(true).toStdString();
		doc[kBulkSource] = true;
		doc[kTimeStamp] = timestring;
		if (individualInfo) {
			std::string subinfo = individualInfo->toString();
			doc[kFileInBulk] = subinfo;
		} 
		jsonRep_ = doc.dump();
	}

	std::string FromBulkImportSource::md5(Synth *synth) const
	{
		ignoreUnused(synth);
		String uuid(fmt::format("Bulk import {}", timestamp_.formatted("%x at %X").toStdString()));
		return MD5(uuid.toUTF8()).toHexString().toStdString();
	}

	std::string FromBulkImportSource::toDisplayString(Synth *synth, bool shortVersion) const
	{
		if (timestamp_.toMilliseconds() != 0) {
			if (shortVersion || !individualInfo_) {
				// https://docs.juce.com/master/classTime.html#afe9d0c7308b6e75fbb5e5d7b76262825
				return fmt::format("Bulk import ({})", timestamp_.formatted("%x at %X").toStdString());
			}
			else {
				// https://docs.juce.com/master/classTime.html#afe9d0c7308b6e75fbb5e5d7b76262825
				return fmt::format("Bulk import {} ({})", timestamp_.formatted("%x at %X").toStdString(), individualInfo_->toDisplayString(synth, true));
			}
		}
		return "Bulk file import";
	}

	std::shared_ptr<FromBulkImportSource> FromBulkImportSource::fromString(std::shared_ptr<Synth> synth, std::string const &jsonString)
	{
		auto obj = nlohmann::json::parse(jsonString);
		if (obj.contains(kBulkSource)) {
			Time timestamp;
			if (obj.contains(kTimeStamp)) {
				std::string timestring = obj[kTimeStamp];
				timestamp = Time::fromISO8601(timestring);
			}
			std::shared_ptr<SourceInfo> individualInfo;
			if (obj.contains(kFileInBulk)) {
				auto &subinfoJson = obj[kFileInBulk];
				if (subinfoJson.is_string()) {
					individualInfo = SourceInfo::fromString(synth, subinfoJson);
				}
				else {
					std::string subinfo = subinfoJson.dump();
					individualInfo = SourceInfo::fromString(synth, subinfo);
				}
			}
			return std::make_shared<FromBulkImportSource>(timestamp, individualInfo);
		}
		return nullptr;
	}

	std::shared_ptr<SourceInfo> FromBulkImportSource::individualInfo() const
	{
		return individualInfo_;
	}

}
