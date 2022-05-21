/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Patch.h"
#include "MidiBankNumber.h"
#include "AutomaticCategory.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
#include "nlohmann/json.hpp"
#pragma GCC diagnostic pop

#include <set>

namespace midikraft {

	class Favorite {
	public:
		enum class TFavorite { DONTKNOW = -1, NO = 0, YES = 1 };
		Favorite(); // Creates an "unknown favorite"
		explicit Favorite(bool isFavorite); // Creates a favorite with a user decision
		explicit Favorite(int howFavorite); // For loading from the database

		TFavorite is() const;

	private:
		TFavorite favorite_;
	};

	class SourceInfo {
	public:
        virtual ~SourceInfo() = default;
		virtual std::string toString() const;
		virtual std::string md5(Synth *synth) const = 0;
		virtual std::string toDisplayString(Synth *synth, bool shortVersion) const = 0;
		static std::shared_ptr<SourceInfo> fromString(std::string const &str);

		static bool isEditBufferImport(std::shared_ptr<SourceInfo> sourceInfo);

	protected:
		std::string jsonRep_;
	};

	class FromSynthSource : public SourceInfo {
	public:
		explicit FromSynthSource(Time timestamp); // Use this for edit buffer
		FromSynthSource(Time timestamp, MidiBankNumber bankNo); // Use this when the program place is known
        virtual ~FromSynthSource() override = default;
		virtual std::string md5(Synth *synth) const override;
		virtual std::string toDisplayString(Synth *synth, bool shortVersion) const override;
		static std::shared_ptr<FromSynthSource> fromString(std::string const &jsonString);

		MidiBankNumber bankNumber() const;

	private:
		const Time timestamp_;
		const MidiBankNumber bankNo_;
	};

	class FromFileSource : public SourceInfo {
	public:
		FromFileSource(std::string const &filename, std::string const &fullpath, MidiProgramNumber program);
		virtual std::string md5(Synth *synth) const override;
		virtual std::string toDisplayString(Synth *synth, bool shortVersion) const override;
		static std::shared_ptr<FromFileSource> fromString(std::string const &jsonString);

	private:
		const std::string filename_;
	};

	class FromBulkImportSource : public SourceInfo {
	public:
		FromBulkImportSource(Time timestamp, std::shared_ptr<SourceInfo> individualInfo);
		virtual std::string md5(Synth *synth) const override;
		virtual std::string toDisplayString(Synth *synth, bool shortVersion) const override;
		static std::shared_ptr<FromBulkImportSource> fromString(std::string const &jsonString);
		std::shared_ptr<SourceInfo> individualInfo() const;

	private:
		const Time timestamp_;
		std::shared_ptr<SourceInfo> individualInfo_;
	};

	class PatchHolder {
	public:		
		PatchHolder();
		PatchHolder(std::shared_ptr<Synth> activeSynth, std::shared_ptr<SourceInfo> sourceInfo, std::shared_ptr<DataFile> patch,
			MidiBankNumber bank, MidiProgramNumber place, 
			std::shared_ptr<AutomaticCategory> detector = nullptr);

		std::shared_ptr<DataFile> patch() const;
		Synth *synth() const;
		std::shared_ptr<Synth> smartSynth() const; // This is for refactoring

		int getType() const;

		void setName(std::string const &newName);
		std::string name() const;

		void setSourceId(std::string const &source_id);
		std::string sourceId() const;

		void setPatchNumber(MidiProgramNumber number);
		MidiProgramNumber patchNumber() const;

		void setBank(MidiBankNumber bank);
		MidiBankNumber bankNumber() const;

		bool isFavorite() const;
		Favorite howFavorite() const;
		void setFavorite(Favorite fav);
		void setSourceInfo(std::shared_ptr<SourceInfo> newSourceInfo);

		bool isHidden() const;
		void setHidden(bool isHidden);

		bool hasCategory(Category const &category) const;
		void setCategory(Category const &category, bool hasIt);
		void setCategories(std::set<Category> const &cats);
		void clearCategories();
		std::set<Category> categories() const;
		std::set<Category> userDecisionSet() const;
		void setUserDecision(Category const &clicked);
		void setUserDecisions(std::set<Category> const &cats);

		std::shared_ptr<SourceInfo> sourceInfo() const;

		bool autoCategorizeAgain(std::shared_ptr<AutomaticCategory> detector); // Returns true if categories have changed!
		
		std::string md5() const;
		std::string createDragInfoString() const;
		static nlohmann::json dragInfoFromString(std::string s);

	private:
		std::shared_ptr<DataFile> patch_;
		std::shared_ptr<Synth> synth_;
		std::string name_;
		std::string sourceId_;
		int type_;
		Favorite isFavorite_;
		bool isHidden_;
		std::set<Category> categories_;
		std::set<Category> userDecisions_;
		MidiBankNumber bankNumber_;
		MidiProgramNumber patchNumber_;
		std::shared_ptr<SourceInfo> sourceInfo_;
	};

}
