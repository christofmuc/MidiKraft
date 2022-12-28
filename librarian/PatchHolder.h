/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Patch.h"
#include "MidiBankNumber.h"
#include "AutomaticCategory.h"
#include "LambdaValueListener.h"

// Turn off warning on unknown pragmas for VC++
#pragma warning(push)
#pragma warning(disable: 4068)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
#include "nlohmann/json.hpp"
#pragma GCC diagnostic pop
#pragma warning(pop)

#include <set>

namespace midikraft {

	class Favorite {
	public:
		enum class TFavorite { DONTKNOW = -1, NO = 0, YES = 1 };
		Favorite(); // Creates an "unknown favorite"
		explicit Favorite(bool isFavorite); // Creates a favorite with a user decision
		explicit Favorite(int howFavorite); // For loading from the database

		TFavorite is() const;
		bool isItForSure() const {
			return favorite_ == Favorite::TFavorite::YES;
		}

		bool operator!=(Favorite const& other) const;

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

		std::string filename() const {
			return filename_;
		}

		std::string fullpath() const {
			return fullpath_;
		}

		MidiProgramNumber programNumber() const {
			return program_;
		}

	private:
		const std::string filename_;
		const std::string fullpath_;
		MidiProgramNumber program_;
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
	private:
		// Need to initialize this fist
		ValueTree tree_;

	public:		
		PatchHolder();
		PatchHolder(std::weak_ptr<Synth> activeSynth, std::shared_ptr<SourceInfo> sourceInfo, std::shared_ptr<DataFile> patch,
			MidiBankNumber bank, MidiProgramNumber place, 
			std::shared_ptr<AutomaticCategory> detector = nullptr);
		PatchHolder(PatchHolder const& other);

		void operator =(PatchHolder const& other);

		std::shared_ptr<DataFile> patch() const;
		Synth *synth() const;
		std::shared_ptr<Synth> smartSynth() const; // This is for refactoring

		int getType() const; // calculated from patch data, not settable

		juce::CachedValue<String> name;
		juce::CachedValue<String> sourceId;
		juce::CachedValue<MidiBankNumber> bank;
		juce::CachedValue<MidiProgramNumber> program;
		juce::CachedValue<Favorite> favorite;
		juce::CachedValue<bool> hidden;

		void setSourceInfo(std::shared_ptr<SourceInfo> newSourceInfo);

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

		// Some helpers for our drag and drop operatios
		static bool dragItemIsPatch(nlohmann::json const& dragInfo);
		static bool dragItemIsList(nlohmann::json const& dragInfo);

		ValueTree getTree() { return tree_;  };

	private:
		void createListeners();

		ListenerSet listeners_;
		
		std::shared_ptr<DataFile> patch_;
		std::weak_ptr<Synth> synth_;
		std::set<Category> categories_;
		std::set<Category> userDecisions_;
		std::shared_ptr<SourceInfo> sourceInfo_;
	};

}

template <> class juce::VariantConverter<midikraft::Favorite> {
public:
	static midikraft::Favorite fromVar(const var& v);
	static var toVar(const midikraft::Favorite& t);
};

