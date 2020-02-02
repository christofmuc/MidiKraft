/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "Patch.h"
#include "MidiBankNumber.h"
#include "AutomaticCategory.h"

#include <set>

namespace midikraft {

	class Favorite {
	public:
		enum class TFavorite { DONTKNOW = -1, NO = 0, YES = 1 };
		Favorite(); // Creates an "unknown favorite"
		explicit Favorite(bool isFavorite); // Creates a favorite with a user decision

		TFavorite is() const;

	private:
		TFavorite favorite_;
	};

	class SourceInfo {
	public:
		virtual std::string toString() const;
		virtual std::string toDisplayString(Synth *synth) const = 0;
		static std::shared_ptr<SourceInfo> fromString(std::string const &str);

	protected:
		std::string jsonRep_;
	};

	class FromSynthSource : public SourceInfo {
	public:
		FromSynthSource(Time timestamp, MidiBankNumber bankNo);
		virtual std::string toDisplayString(Synth *synth) const override;
		static std::shared_ptr<FromSynthSource> fromString(std::string const &jsonString);

	private:
		const Time timestamp_;
		const MidiBankNumber bankNo_;
	};

	class FromFileSource : public SourceInfo {
	public:
		FromFileSource(std::string const &filename, std::string const &fullpath, MidiProgramNumber program);
		virtual std::string toDisplayString(Synth *synth) const override;
		static std::shared_ptr<FromFileSource> fromString(std::string const &jsonString);

	private:
		const std::string filename_;
	};

	class FromBulkImportSource : public SourceInfo {
	public:
		FromBulkImportSource(Time timestamp, std::shared_ptr<SourceInfo> individualInfo);
		virtual std::string toDisplayString(Synth *synth) const override;
		static std::shared_ptr<FromBulkImportSource> fromString(std::string const &jsonString);
		std::shared_ptr<SourceInfo> individualInfo() const;

	private:
		const Time timestamp_;
		std::shared_ptr<SourceInfo> individualInfo_;
	};

	class PatchHolder {
	public:
		PatchHolder();
		PatchHolder(std::shared_ptr<SourceInfo> sourceInfo, std::shared_ptr<Patch> patch, bool autoDetectCategories = false);

		std::shared_ptr<Patch> patch() const;

		bool isFavorite() const;
		Favorite howFavorite() const;
		void setFavorite(Favorite fav);
		void setSourceInfo(std::shared_ptr<SourceInfo> newSourceInfo);

		bool hasCategory(Category const &category) const;
		void setCategory(Category const &category, bool hasIt);
		void setCategory(std::string const &categoryName, bool hasIt);
		void clearCategories();
		std::set<Category> categories() const;
		std::shared_ptr<SourceInfo> sourceInfo() const;

	private:
		std::shared_ptr<Patch> patch_;
		Favorite isFavorite_;
		std::set<Category> categories_;
		std::shared_ptr<SourceInfo> sourceInfo_;
	};

}
