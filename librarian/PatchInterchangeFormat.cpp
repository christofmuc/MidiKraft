/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PatchInterchangeFormat.h"

#include "SynthBank.h"

#include "Logger.h"
#include "Sysex.h"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "JsonSerialization.h"

#include <fstream>
#include <ostream>

namespace {

const char *kSynth = "Synth";
const char *kName = "Name";
const char *kSysex = "Sysex";
const char *kFavorite = "Favorite";
const char *kPlace = "Place";
const char *kBank = "Bank";
const char *kCategories = "Categories";
const char *kNonCategories = "NonCategories";
const char *kSourceInfo = "SourceInfo";
const char *kComment= "Comment";
const char *kLibrary = "Library";
const char *kHeader = "Header";
const char *kFileFormat = "FileFormat";
const char *kPIF = "PatchInterchangeFormat";
const char *kVersion = "Version";

}

namespace midikraft {

	bool findCategory(std::shared_ptr<AutomaticCategory> detector, const char *categoryName, midikraft::Category &outCategory) {
		// Hard code migration from the Rev2SequencerTool categoryNames to KnobKraft Orm
		//TODO this can use the mapping defined for the Virus now?
		//TODO - this could become arbitrarily complex with free tags?
		if (!strcmp(categoryName, "Bells")) categoryName = "Bell";
		if (!strcmp(categoryName, "FX")) categoryName = "SFX";

		// Check if this is a valid category
		for (auto acat : detector->loadedRules()) {
			if (acat.category().category() == categoryName) {
				// Found, great!
				outCategory = acat.category();
				return true;
			}
		}
		return false;
	}

	/*
	* Load routine for the new PatchInterchangeFormat.
	*
	* The idea is to create a human readable (JSON) format that allows to archive and transport sysex patches and their metadata.
	*
	* The sysex binary data is in a base64 encoded field, the rest of the metadata is normal JSON, and should be largely self-documenting.
	*
	* Example of metadata: Given name to patch, origin (synth or file import etc.), is favorite, categories etc.
	*
	* File version history:
	*
	*   0  - This file format has no header information and is just an array of Patches. It was exported by the Rev2SequencerTool, the KnobKraft Orm predecessor, to export data stored in the AWS DynamoDB
	*   1  - First version with header containing name of file format and version number, else it is identical to version 0 containing the patches in the field "Library" (to mark it is not a bank!)
	*/

	std::vector<midikraft::PatchHolder> PatchInterchangeFormat::load(std::map<std::string, std::shared_ptr<Synth>> activeSynths, std::string const &filename, std::shared_ptr<AutomaticCategory> detector)
	{
		std::vector<midikraft::PatchHolder> result;

		// Check if file exists
		File pif(filename);
		auto fileSource = std::make_shared<FromFileSource>(pif.getFileName().toStdString(), pif.getFullPathName().toStdString(), MidiProgramNumber::invalidProgram());
		if (pif.existsAsFile()) {
			FileInputStream in(pif);
			String content = in.readEntireStreamAsString();

			// Try to parse it!
			try {
				auto jsonDoc = nlohmann::json::parse(content.toStdString());

				int version = 0;
				if (jsonDoc.is_object()) {
					if (!jsonDoc.contains(kHeader)) {
						spdlog::error("This is not a PatchInterchangeFormat JSON file - no header defined. Aborting.");
						return {};
					}
					nlohmann::json header;
					if (jsonDoc[kHeader].is_object()) {
						// Proper format, use the header object, not the manually hacked header which was needed to get the files back into version 1.11.0
						// Eventually, the header = jsonDoc special case should be removed again.
						header = jsonDoc[kHeader];
					}
					if (!header.contains(kFileFormat) || !header[kFileFormat].is_string()) {
						spdlog::error("File header block has no string member to define FileFormat. Aborting.");
						return {};
					}
					if (header[kFileFormat] != kPIF) {
						spdlog::error("File header defines different FileFormat than PatchInterchangeFormat. Aborting.");
						return {};
					}
					if (!header.contains(kVersion) || !header[kVersion].is_number_integer()) {
						spdlog::error("File header has no integer-values member defining file Version. Aborting.");
						return {};
					}
					// Header all good, let's read the Version of the format
					version = header[kVersion];
				}

				nlohmann::json patchArray;
				if (version == 0) {
					// Original version had no header, whole file was an array of patches
					if (!jsonDoc.is_array()) {
					}
					patchArray = jsonDoc;
				}
				else {
					// From version 1 on, Patches are stored in a Member Field called "Library"
					if (jsonDoc.contains(kLibrary) && jsonDoc[kLibrary].is_array()) {
						patchArray = jsonDoc[kLibrary];
					}
				}

				if (patchArray.is_array()) {
					for (auto item = patchArray.cbegin(); item != patchArray.cend(); item++) {
						if (!item->contains(kSynth)) {
							spdlog::warn("Skipping patch which has no 'Synth' field");
							continue;
						}
						std::string synthname = (*item)[kSynth];
						if (activeSynths.find(synthname) == activeSynths.end()) {
							spdlog::warn("Skipping patch which is for synth {} and not for any present in the list given", synthname);
							continue;
						}
						auto activeSynth = activeSynths[synthname];
						if (!item->contains(kName)) {
							spdlog::warn("Skipping patch which has no 'Name' field");
							continue;
						}
						std::string patchName = (*item)[kName]; //TODO this is not robust, as it might have a non-string type
						if (!item->contains(kSysex)) {
							spdlog::warn("Skipping patch {} which has no 'Sysex' field", patchName);
							continue;
						}

						// Optional fields!
						Favorite fav;
						if (item->contains(kFavorite)) {
							if ((*item)[kFavorite].is_number_integer()) {
								fav = Favorite((*item)[kFavorite] != 0);
							}
							else if ((*item)[kFavorite].is_null()) {
								fav = Favorite(-1);
							}
							else {
								std::string favoriteStr = (*item)[kFavorite];
								try {
									bool favorite = std::stoi(favoriteStr) != 0;
									fav = Favorite(favorite);
								}
								catch (std::invalid_argument&) {
									spdlog::warn("Ignoring favorite information for patch {} because {} does not convert to an integer", patchName, favoriteStr);
								}
							}
						}

						MidiBankNumber bank = MidiBankNumber::invalid();
						if (item->contains(kBank)) {
							if ((*item)[kBank].is_number_integer()) {
								int bankInt = (*item)[kBank];
								bank = MidiBankNumber::fromZeroBase(bankInt, SynthBank::numberOfPatchesInBank(activeSynth, bankInt));
							}
							else {
								std::string bankStr = (*item)[kBank];
								try {
									int bankInt = std::stoi(bankStr);
									bank = MidiBankNumber::fromZeroBase(bankInt, SynthBank::numberOfPatchesInBank(activeSynth, bankInt));
								}
								catch (std::invalid_argument&) {
									spdlog::warn("Ignoring MIDI bank information for patch {} because {} does not convert to an integer", patchName, bankStr);
								}
							}
						}

						MidiProgramNumber place = MidiProgramNumber::invalidProgram();
						if (item->contains(kPlace)) {
							if ((*item)[kPlace].is_number_integer()) {
								if (bank.isValid()) {
									place = MidiProgramNumber::fromZeroBaseWithBank(bank, (*item)[kPlace]);
								}
								else {
									place = MidiProgramNumber::fromZeroBase((*item)[kPlace]);
								}
							}
							else {
								std::string placeStr = (*item)[kPlace];
								try {
									if (bank.isValid()) {
										place = MidiProgramNumber::fromZeroBaseWithBank(bank, std::stoi(placeStr));
									}
									else {
										place = MidiProgramNumber::fromZeroBase(std::stoi(placeStr));
									}
								}
								catch (std::invalid_argument&) {
									spdlog::warn("Ignoring MIDI place information for patch {} because {} does not convert to an integer", patchName, placeStr);
								}
							}
						}

						std::vector<Category> categories;
						if (item->contains(kCategories) && (*item)[kCategories].is_array()) {
							auto cats = (*item)[kCategories];
							for (auto cat = cats.cbegin(); cat != cats.cend(); cat++) {
								midikraft::Category category(nullptr);
								if (findCategory(detector, cat->get<std::string>().c_str(), category)) {
									categories.push_back(category);
								}
								else {
									spdlog::warn("Ignoring category {} of patch {} because it is not part of our standard categories!", cat->get<std::string>(), patchName);
								}
							}
						}

						std::vector<Category> nonCategories;
						if (item->contains(kNonCategories) && item->at(kNonCategories).is_array()) {
							auto cats = (*item)[kNonCategories];
							for (auto cat = cats.cbegin(); cat != cats.cend(); cat++) {
								midikraft::Category category(nullptr);
								if (findCategory(detector, cat->get<std::string>().c_str(), category)) {
									nonCategories.push_back(category);
								}
								else {
									spdlog::warn("Ignoring non-category {} of patch {} because it is not part of our standard categories!", cat->get<std::string>(), patchName);
								}
							}
						}

						std::shared_ptr<midikraft::SourceInfo> importInfo;
						if (item->contains(kSourceInfo)) {
							if ((*item)[kSourceInfo].is_string())
								importInfo = SourceInfo::fromString(activeSynth, (*item)[kSourceInfo]);
							else
								importInfo = SourceInfo::fromString(activeSynth, (*item)[kSourceInfo].dump());
						}

						std::string comment;
						if (item->contains(kComment)) {
							comment = (*item)[kComment];
						}

						// All mandatory fields found, we can parse the data!
						MemoryBlock sysexData;
						MemoryOutputStream writeToBlock(sysexData, false);
						String base64encoded = (*item)[kSysex].get<std::string>();
						if (Base64::convertFromBase64(writeToBlock, base64encoded)) {
							writeToBlock.flush();
							auto messages = Sysex::memoryBlockToMessages(sysexData);
							auto patches = activeSynth->loadSysex(messages);
							//jassert(patches.size() == 1);
							if (patches.size() == 1) {
								PatchHolder holder(activeSynth, fileSource, patches[0], detector);
								holder.setFavorite(fav);
								holder.setBank(bank);
								holder.setPatchNumber(place);
								holder.setName(patchName);
								for (const auto& cat : categories) {
									holder.setCategory(cat, true);
									holder.setUserDecision(cat); // All Categories loaded via PatchInterchangeFormat are considered user decisions
								}
								for (const auto& noncat : nonCategories) {
									holder.setUserDecision(noncat); // A Category mentioned here says it might not be present, but that is a user decision!
								}
								if (importInfo) {
									holder.setSourceInfo(importInfo);
								}
								holder.setComment(comment);
								result.push_back(holder);
							}
						}
						else {
							spdlog::warn("Skipping patch with invalid base64 encoded data!");
						}
					}
				}
				else {
				spdlog::warn("No Library patches defined in PatchInterchangeFormat, no patches loaded");
				}
			}
			catch (nlohmann::json::exception const& e) {
				spdlog::error("JSON error loading PIF file {}, import aborted: {}", filename, e.what());
			}
		}
		return result;
	}

	void PatchInterchangeFormat::save(std::vector<PatchHolder> const &patches, std::string const &toFilename)
	{
		File outputFile(toFilename);
		if (outputFile.existsAsFile()) {
			outputFile.deleteFile();
		}

		nlohmann::json doc;

		nlohmann::json header;
		header[kFileFormat] = kPIF;
		header[kVersion] = 1;
		doc[kHeader] = header;

		auto library = nlohmann::json::array();
		for (auto patch : patches) {
			nlohmann::json patchJson;
			patchJson[kSynth] = patch.synth()->getName();
			patchJson[kName] = patch.name();
			switch (patch.howFavorite().is()) {
			case Favorite::TFavorite::DONTKNOW:
				patchJson[kFavorite] = nlohmann::json();
				break;
			case Favorite::TFavorite::YES:
				patchJson[kFavorite] = 1;
				break;
			case Favorite::TFavorite::NO:
				patchJson[kFavorite] = 0;
				break;
			default:
				spdlog::error("Missing code to write Favoriate value of {} to PIF, program error!", static_cast<int>(patch.howFavorite().is()));
				patchJson[kFavorite] = nlohmann::json();
			}
			
			if (patch.bankNumber().isValid()) {
				patchJson[kBank] = patch.bankNumber().toZeroBased();
			}
			patchJson[kPlace] = patch.patchNumber().toZeroBasedDiscardingBank();
 			auto categoriesSet = patch.categories();
			auto userDecisions = patch.userDecisionSet();
			auto userDefinedCategories = category_intersection(categoriesSet, userDecisions);
			if (!userDefinedCategories.empty()) {
				// Here is a list of categories to write
				auto categoryList = nlohmann::json::array();
				for (auto cat : userDefinedCategories) {
					categoryList.emplace_back(cat.category());
				}
				patchJson[kCategories] = categoryList;
			}
			auto userDefinedNonCategories = category_difference(userDecisions, categoriesSet);
			if (!userDefinedNonCategories.empty()) {
				// Here is a list of non-categories to write
				auto nonCategoryList = nlohmann::json::array();
				for (auto cat : userDefinedNonCategories) {
					nonCategoryList.emplace_back(cat.category());
				}
				patchJson[kNonCategories] = nonCategoryList;
			}

			if (patch.sourceInfo()) {
				patchJson[kSourceInfo] = nlohmann::json::parse(patch.sourceInfo()->toString());
			}

			if (!patch.comment().empty()) {
				patchJson[kComment] = patch.comment();
			}

			// Now the fun part, pack the sysex for transport
			auto sysexMessages = patch.synth()->dataFileToSysex(patch.patch(), nullptr);
			std::vector<uint8> data;
			// Just concatenate all messages generated into one uint8 array
			for (auto m : sysexMessages) {
				std::copy(m.getRawData(), m.getRawData() + m.getRawDataSize(), std::back_inserter(data));
			}
			std::string base64encoded = JsonSerialization::dataToString(data);
			patchJson[kSysex] = base64encoded;

			library.emplace_back(patchJson);
		}
		doc[kLibrary] = library;

		std::ofstream o(toFilename);
		o << std::setw(4) << doc << std::endl;
}

}
