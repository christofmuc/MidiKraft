/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PatchInterchangeFormat.h"

#include "RapidjsonHelper.h"

#include "Logger.h"
#include "Sysex.h"

#include <boost/format.hpp>

const char *kSynth = "Synth";
const char *kName = "Name";
const char *kSysex = "Sysex";
const char *kFarvorite = "Favorite";
const char *kPlace = "Place";
const char *kCategories = "Categories";
const char *kNonCategories = "NonCategories";
const char *kSourceInfo = "SourceInfo";

namespace midikraft {

	bool findCategory(std::shared_ptr<AutomaticCategory> detector, const char *categoryName, midikraft::Category &outCategory) {
		// Hard code migration from the Rev2SequencerTool categoryNames to KnobKraft Orm
		//TODO this can use the mapping defined for the Virus now?
		//TODO - this could become arbitrarily complex with free tags?
		if (!strcmp(categoryName, "Bells")) categoryName = "Bell";
		if (!strcmp(categoryName, "FX")) categoryName = "SFX";

		// Check if this is a valid category
		for (auto acat : detector->predefinedCategoryVector()) {
			if (acat.category == categoryName) {
				// Found, great!
				outCategory = acat;
				return true;			
			}
		}
		return false;
	}

	std::vector<midikraft::PatchHolder> PatchInterchangeFormat::load(std::shared_ptr<Synth> activeSynth, std::string const &filename, std::shared_ptr<AutomaticCategory> detector)
	{
		std::vector<midikraft::PatchHolder> result;

		// Check if file exists
		File pif(filename);
		auto fileSource = std::make_shared<FromFileSource>(pif.getFileName().toStdString(), pif.getFullPathName().toStdString(), MidiProgramNumber::fromZeroBase(0));
		if (pif.existsAsFile()) {
			FileInputStream in(pif);
			String content = in.readEntireStreamAsString();

			// Try to parse it!
			rapidjson::Document jsonDoc;
			jsonDoc.Parse(content.toStdString().c_str());

			if (jsonDoc.IsArray()) {
				auto patchArray = jsonDoc.GetArray();
				for (auto item = jsonDoc.Begin(); item != jsonDoc.End(); item++) {
					if (!item->HasMember(kSynth)) {
						SimpleLogger::instance()->postMessage("Skipping patch which has no 'Synth' field");
						continue;
					}
					if (activeSynth->getName() != (*item)[kSynth].GetString()) {
						SimpleLogger::instance()->postMessage((boost::format("Skipping patch which is for synth %s and not for %s") % (*item)["Synth"].GetString() % activeSynth->getName()).str());
						continue;
					}
					if (!item->HasMember(kName)) {
						SimpleLogger::instance()->postMessage("Skipping patch which has no 'Name' field");
						continue;
					}
					std::string patchName = (*item)[kName].GetString(); //TODO this is not robust, as it might have a non-string type
					if (!item->HasMember(kSysex)) {
						SimpleLogger::instance()->postMessage((boost::format("Skipping patch %s which has no 'Sysex' field") % patchName).str());
						continue;
					}

					// Optional fields!
					Favorite fav;
					if (item->HasMember(kFarvorite)) {
						std::string favoriteStr = (*item)[kFarvorite].GetString();
						try {
							bool favorite = std::stoi(favoriteStr) != 0;
							fav = Favorite(favorite);
						}
						catch (std::invalid_argument &) {
							SimpleLogger::instance()->postMessage((boost::format("Ignoring favorite information for patch %s because %s does not convert to an integer") % patchName % favoriteStr).str());
						}
					}

					MidiProgramNumber place = MidiProgramNumber::fromZeroBase(0);
					if (item->HasMember(kPlace)) {
						std::string placeStr = (*item)[kPlace].GetString();
						try {
							place = MidiProgramNumber::fromZeroBase(std::stoi(placeStr));
						}
						catch (std::invalid_argument &) {
							SimpleLogger::instance()->postMessage((boost::format("Ignoring MIDI place information for patch %s because %s does not convert to an integer") % patchName % placeStr).str());
						}
					}

					std::vector<Category> categories;
					if (item->HasMember(kCategories)) {
						auto cats = (*item)[kCategories].GetArray();
						for (auto cat = cats.Begin(); cat != cats.End(); cat++) {
							midikraft::Category category("", Colours::aliceblue);
							if (findCategory(detector, cat->GetString(), category)) {
								categories.push_back(category);
							}
							else {
								SimpleLogger::instance()->postMessage((boost::format("Ignoring category %s of patch %s because it is not part of our standard categories!") % cat->GetString() % patchName).str());
							}
						}
					}

					std::vector<Category> nonCategories;
					if (item->HasMember(kNonCategories)) {
						auto cats = (*item)[kNonCategories].GetArray();
						for (auto cat = cats.Begin(); cat != cats.End(); cat++) {
							midikraft::Category category("", Colours::aliceblue);
							if (findCategory(detector, cat->GetString(), category)) {
								nonCategories.push_back(category);
							}
							else {
								SimpleLogger::instance()->postMessage((boost::format("Ignoring non-category %s of patch %s because it is not part of our standard categories!") % cat->GetString() % patchName).str());
							}
						}
					}

					std::shared_ptr<midikraft::SourceInfo> importInfo;
					if (item->HasMember(kSourceInfo)) {
						importInfo = SourceInfo::fromString(renderToJson((*item)[kSourceInfo]));
					}

					// All mandatory fields found, we can parse the data!
					MemoryBlock sysexData;
					MemoryOutputStream writeToBlock(sysexData, false);
					String base64encoded = (*item)[kSysex].GetString();
					if (Base64::convertFromBase64(writeToBlock, base64encoded)) {
						writeToBlock.flush();
						auto messages = Sysex::memoryBlockToMessages(sysexData);
						auto patches = activeSynth->loadSysex(messages);
						//jassert(patches.size() == 1);
						if (patches.size() == 1) {
							//TODO The file format did not specify MIDI banks 
							PatchHolder holder(activeSynth, fileSource, patches[0], MidiBankNumber::fromZeroBase(0), place, detector);
							holder.setFavorite(fav);
							holder.setName(patchName);
							for (const auto& cat : categories) {
								holder.setCategory(cat, true);
								holder.setUserDecision(cat); // All Categories loaded via PatchInterchangeFormat are considered user decisions
							}
							for (const auto &noncat : nonCategories) {
								holder.setUserDecision(noncat); // A Category mentioned here says it might not be present, but that is a user decision!
							}
							if (importInfo) {
								holder.setSourceInfo(importInfo);
							}
							result.push_back(holder);
						}
					}
					else {
						SimpleLogger::instance()->postMessage("Skipping patch with invalid base64 encoded data!");
					}
				}
			}
		}
		return result;
	}

}
