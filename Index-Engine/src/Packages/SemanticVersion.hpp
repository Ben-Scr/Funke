#pragma once
#include "Core/Export.hpp"

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Index {

	// SemVer 2.0 version: MAJOR.MINOR.PATCH[-prerelease][+build]. Build metadata is kept but never compared.
	struct INDEX_API SemanticVersion {
		uint64_t Major = 0;
		uint64_t Minor = 0;
		uint64_t Patch = 0;
		std::vector<std::string> PreRelease;
		std::string Build;

		// Strict: all three numbers required. A leading 'v' is accepted.
		static std::optional<SemanticVersion> Parse(std::string_view text);

		bool IsPreRelease() const { return !PreRelease.empty(); }
		SemanticVersion WithoutPreRelease() const { return { Major, Minor, Patch, {}, {} }; }
		bool SameCore(const SemanticVersion& other) const {
			return Major == other.Major && Minor == other.Minor && Patch == other.Patch;
		}
		std::string ToString() const;

		std::strong_ordering operator<=>(const SemanticVersion& other) const;
		bool operator==(const SemanticVersion& other) const { return (*this <=> other) == 0; }
	};

	// npm-style range: comparator sets joined by "||"; a set is space-separated comparators that must all hold.
	// Supported: "*", "", "1.2.3", "=1.2.3", ">1.2.3", ">=", "<", "<=", "^1.2.3", "~1.2.3",
	// partial versions ("1", "1.2", "1.x", "1.2.*") and hyphen ranges ("1.0.0 - 2.0.0").
	class INDEX_API VersionRange {
	public:
		static std::optional<VersionRange> Parse(std::string_view text, std::string* outError = nullptr);
		static VersionRange Any();

		// Pre-release versions only match when a comparator in the same set names that exact
		// MAJOR.MINOR.PATCH with a pre-release, so "^1.0.0" never pulls in "2.0.0-beta".
		bool IsSatisfiedBy(const SemanticVersion& version) const;
		bool IsAny() const;
		const std::string& GetText() const { return m_Text; }

	private:
		enum class Op : uint8_t { Less, LessEqual, Greater, GreaterEqual, Equal };
		struct Comparator {
			Op Operation = Op::Equal;
			SemanticVersion Version;
			// Upper bounds like "<2.0.0-0" come from desugaring "^"/"~"/x-ranges; they never opt a set into pre-releases.
			bool Synthetic = false;
		};
		using ComparatorSet = std::vector<Comparator>;

		static bool Matches(const Comparator& comparator, const SemanticVersion& version);

		std::vector<ComparatorSet> m_Sets;
		std::string m_Text;
	};

}
