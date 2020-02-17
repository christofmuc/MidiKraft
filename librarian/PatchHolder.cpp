/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PatchHolder.h"

#include "AutomaticCategory.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"

#include <boost/format.hpp>

#include "RapidjsonHelper.h"

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


	PatchHolder::PatchHolder(Synth *activeSynth, std::shared_ptr<SourceInfo> sourceInfo, std::shared_ptr<Patch> patch, bool autoDetectCategories /* = false */)
		: sourceInfo_(sourceInfo), patch_(patch), isFavorite_(Favorite())
	{
		if (patch && autoDetectCategories) {
			categories_ = AutoCategory::determineAutomaticCategories(*patch);
		}
		md5_ = calcMd5(activeSynth, patch.get());
	}

	PatchHolder::PatchHolder() : isFavorite_(Favorite())
	{
	}

	std::shared_ptr<Patch> PatchHolder::patch() const
	{
		return patch_;
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

	void PatchHolder::setCategory(std::string const &categoryName, bool hasIt)
	{
		for (auto cat : AutoCategory::predefinedCategories()) {
			if (cat.category().category == categoryName) {
				setCategory(cat.category(), hasIt);
			}
		}
	}

	void PatchHolder::clearCategories()
	{
		categories_.clear();
	}

	std::set<Category> PatchHolder::categories() const
	{
		return categories_;
	}

	int64 PatchHolder::categoriesAsBitfield() const {
		return Category::categorySetAsBitfield(categories_);
	}

	void PatchHolder::setCategoriesFromBitfield(int64 bitfield)
	{
		categories_.clear();
		for (int i = 0; i < 64; i++) {
			if (bitfield & (1LL << i)) {
				// This bit is set, find the category that has this bitindex
				for (auto c : AutoCategory::predefinedCategoryVector()) {
					if (c.bitIndex == (i + 1)) {
						categories_.insert(c);
						break;
					}
				}
			}
		}
	}

	std::shared_ptr<SourceInfo> PatchHolder::sourceInfo() const
	{
		return sourceInfo_;
	}

	bool PatchHolder::autoCategorizeAgain()
	{
		auto previous = categories();
		categories_ = AutoCategory::determineAutomaticCategories(*patch_);
		return previous != categories_;
	}

	std::string PatchHolder::calcMd5(Synth *activeSynth, Patch *patch)
	{
		auto filteredData = activeSynth->filterVoiceRelevantData(patch->data());
		MD5 md5(&filteredData[0], filteredData.size());
		return md5.toHexString().toStdString();
	}

	std::string PatchHolder::md5() const
	{
		return md5_;
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

	std::shared_ptr<SourceInfo> SourceInfo::fromString(std::string const &str)
	{
		rapidjson::Document doc;
		doc.Parse(str.c_str());
		if (doc.IsObject()) {
			auto obj = doc.GetObject();
			if (obj.HasMember(kFileSource)) {
				return FromFileSource::fromString(str);
			}
			else if (obj.HasMember(kSynthSource)) {
				return FromSynthSource::fromString(str);
			}
			else if (obj.HasMember(kBulkSource)) {
				return FromBulkImportSource::fromString(str);
			}
		}
		return nullptr;
	}

	FromSynthSource::FromSynthSource(Time timestamp, MidiBankNumber bankNo) : timestamp_(timestamp), bankNo_(bankNo)
	{
		rapidjson::Document doc;
		doc.SetObject();
		std::string timestring = timestamp.toISO8601(true).toStdString();
		doc.AddMember(rapidjson::StringRef(kSynthSource), true, doc.GetAllocator());
		doc.AddMember(rapidjson::StringRef(kTimeStamp), rapidjson::Value(timestring.c_str(), (rapidjson::SizeType) timestring.size()), doc.GetAllocator());
		if (bankNo.isValid()) {
			doc.AddMember(rapidjson::StringRef(kBankNumber), bankNo.toZeroBased(), doc.GetAllocator());
		}
		jsonRep_ = renderToJson(doc);
	}

	std::string FromSynthSource::toDisplayString(Synth *synth) const
	{
		std::string bank = "";
		if (bankNo_.isValid()) {
			bank = (boost::format(" bank %s") % synth->friendlyBankName(bankNo_)).str();;
		}
		if (timestamp_.toMilliseconds() != 0) {
			// https://docs.juce.com/master/classTime.html#afe9d0c7308b6e75fbb5e5d7b76262825
			return (boost::format("Imported from synth%s at %s") % bank % timestamp_.formatted("%x at %X").toStdString()).str();
		}
		else {
			// Legacy import, no timestamp was recorded.
			return (boost::format("Imported from synth%s") % bank).str();
		}
	}

	std::shared_ptr<FromSynthSource> FromSynthSource::fromString(std::string const &jsonString)
	{
		rapidjson::Document doc;
		doc.Parse(jsonString.c_str());
		if (doc.IsObject()) {
			auto obj = doc.GetObject();
			if (obj.HasMember(kSynthSource)) {
				Time timestamp;
				if (obj.HasMember(kTimeStamp)) {
					std::string timestring = obj.FindMember(kTimeStamp).operator*().value.GetString();
					timestamp = Time::fromISO8601(timestring);
				}
				MidiBankNumber bankNo = MidiBankNumber::invalid();
				if (obj.HasMember(kBankNumber)) {
					bankNo = MidiBankNumber::fromZeroBase(obj.FindMember(kBankNumber).operator*().value.GetInt());
				}
				return std::make_shared<FromSynthSource>(timestamp, bankNo);
			}
		}
		return nullptr;
	}

	FromFileSource::FromFileSource(std::string const &filename, std::string const &fullpath, MidiProgramNumber program) : filename_(filename)
	{
		rapidjson::Document doc;
		doc.SetObject();
		doc.AddMember(rapidjson::StringRef(kFileSource), true, doc.GetAllocator());
		doc.AddMember(rapidjson::StringRef(kFileName), rapidjson::Value(filename.c_str(), (rapidjson::SizeType)  filename.size()), doc.GetAllocator());
		doc.AddMember(rapidjson::StringRef(kFullPath), rapidjson::Value(fullpath.c_str(), (rapidjson::SizeType) fullpath.size()), doc.GetAllocator());
		doc.AddMember(rapidjson::StringRef(kProgramNo), program.toZeroBased(), doc.GetAllocator());
		jsonRep_ = renderToJson(doc);

	}

	std::string FromFileSource::toDisplayString(Synth *) const
	{
		return (boost::format("Imported from file %s") % filename_).str();
	}

	std::shared_ptr<FromFileSource> FromFileSource::fromString(std::string const &jsonString)
	{
		rapidjson::Document doc;
		doc.Parse(jsonString.c_str());
		if (doc.IsObject()) {
			auto obj = doc.GetObject();
			if (obj.HasMember(kFileSource)) {
				std::string filename = obj.FindMember(kFileName).operator*().value.GetString();
				std::string fullpath = obj.FindMember(kFullPath).operator*().value.GetString();
				MidiProgramNumber program = MidiProgramNumber::fromZeroBase(obj.FindMember(kProgramNo).operator*().value.GetInt());
				return std::make_shared<FromFileSource>(filename, fullpath, program);
			}
		}
		return nullptr;
	}

	FromBulkImportSource::FromBulkImportSource(Time timestamp, std::shared_ptr<SourceInfo> individualInfo) : timestamp_(timestamp), individualInfo_(individualInfo)
	{
		rapidjson::Document doc;
		doc.SetObject();
		std::string timestring = timestamp.toISO8601(true).toStdString();
		doc.AddMember(rapidjson::StringRef(kBulkSource), true, doc.GetAllocator());
		doc.AddMember(rapidjson::StringRef(kTimeStamp), rapidjson::Value(timestring.c_str(), (rapidjson::SizeType) timestring.size()), doc.GetAllocator());
		std::string subinfo = individualInfo->toString();
		doc.AddMember(rapidjson::StringRef(kFileInBulk), rapidjson::Value(subinfo.c_str(), (rapidjson::SizeType) subinfo.size()), doc.GetAllocator());
		jsonRep_ = renderToJson(doc);
	}

	std::string FromBulkImportSource::toDisplayString(Synth *synth) const
	{
		ignoreUnused(synth);
		if (timestamp_.toMilliseconds() != 0) {
			// https://docs.juce.com/master/classTime.html#afe9d0c7308b6e75fbb5e5d7b76262825
			return (boost::format("Bulk file import %s") % timestamp_.formatted("%x at %X").toStdString()).str();
		}
		return "Bulk file import";
	}

	std::shared_ptr<FromBulkImportSource> FromBulkImportSource::fromString(std::string const &jsonString)
	{
		rapidjson::Document doc;
		doc.Parse(jsonString.c_str());
		if (doc.IsObject()) {
			auto obj = doc.GetObject();
			if (obj.HasMember(kBulkSource)) {
				Time timestamp;
				if (obj.HasMember(kTimeStamp)) {
					std::string timestring = obj.FindMember(kTimeStamp).operator*().value.GetString();
					timestamp = Time::fromISO8601(timestring);
				}
				std::shared_ptr<FromFileSource> individualInfo;
				if (obj.HasMember(kFileInBulk)) {
					individualInfo = FromFileSource::fromString(obj.FindMember(kFileInBulk).operator*().value.GetString());
				}
				return std::make_shared<FromBulkImportSource>(timestamp, individualInfo);
			}
		}
		return nullptr;
	}

	std::shared_ptr<SourceInfo> FromBulkImportSource::individualInfo() const
	{
		return individualInfo_;
	}

}
