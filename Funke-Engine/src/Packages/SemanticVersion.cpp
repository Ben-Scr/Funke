#include "pch.hpp"
#include "Packages/SemanticVersion.hpp"

#include <algorithm>
#include <cctype>
#include <limits>

namespace Funke {
	namespace {
		bool IsDigits(std::string_view text) {
			return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
		}

		bool IsIdentifierChar(char c) {
			return std::isalnum(static_cast<unsigned char>(c)) || c == '-';
		}

		bool ParseNumber(std::string_view text, uint64_t& out) {
			if (!IsDigits(text) || (text.size() > 1 && text.front() == '0') || text.size() > 18) {
				return false;
			}
			out = 0;
			for (char c : text) out = out * 10 + static_cast<uint64_t>(c - '0');
			return true;
		}

		std::vector<std::string_view> Split(std::string_view text, char separator) {
			std::vector<std::string_view> parts;
			size_t start = 0;
			while (true) {
				const size_t end = text.find(separator, start);
				parts.push_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
				if (end == std::string_view::npos) break;
				start = end + 1;
			}
			return parts;
		}

		std::string_view Trim(std::string_view text) {
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
			return text;
		}

		bool ParseIdentifiers(std::string_view text, bool numericNoLeadingZero, std::vector<std::string>& out) {
			for (std::string_view identifier : Split(text, '.')) {
				if (identifier.empty() || !std::all_of(identifier.begin(), identifier.end(), IsIdentifierChar)) return false;
				if (numericNoLeadingZero && IsDigits(identifier) && identifier.size() > 1 && identifier.front() == '0') return false;
				out.emplace_back(identifier);
			}
			return true;
		}

		int ComparePreReleaseIdentifier(const std::string& a, const std::string& b) {
			const bool aNumeric = IsDigits(a);
			const bool bNumeric = IsDigits(b);
			if (aNumeric && bNumeric) {
				if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
				return a.compare(b) < 0 ? -1 : (a == b ? 0 : 1);
			}
			if (aNumeric != bNumeric) return aNumeric ? -1 : 1;
			const int result = a.compare(b);
			return result < 0 ? -1 : (result == 0 ? 0 : 1);
		}

		// A version with missing or wildcard components, as written in ranges ("1", "1.2", "1.x", "*").
		struct PartialVersion {
			uint64_t Numbers[3] = { 0, 0, 0 };
			int Given = 0;
			SemanticVersion Full;
		};

		bool IsWildcard(std::string_view part) {
			return part == "x" || part == "X" || part == "*";
		}

		bool ParsePartial(std::string_view text, PartialVersion& out) {
			if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) text.remove_prefix(1);
			if (text.empty()) return false;
			if (auto full = SemanticVersion::Parse(text)) {
				out.Full = *full;
				out.Numbers[0] = full->Major;
				out.Numbers[1] = full->Minor;
				out.Numbers[2] = full->Patch;
				out.Given = 3;
				return true;
			}
			const std::vector<std::string_view> parts = Split(text, '.');
			if (parts.size() > 3) return false;
			bool sawWildcard = false;
			for (std::string_view part : parts) {
				if (IsWildcard(part)) {
					sawWildcard = true;
					continue;
				}
				if (sawWildcard || !ParseNumber(part, out.Numbers[out.Given])) return false;
				++out.Given;
			}
			out.Full = { out.Numbers[0], out.Numbers[1], out.Numbers[2], {}, {} };
			return true;
		}

		SemanticVersion MakeVersion(uint64_t major, uint64_t minor, uint64_t patch, bool lowestPreRelease = false) {
			SemanticVersion version{ major, minor, patch, {}, {} };
			if (lowestPreRelease) version.PreRelease.push_back("0");
			return version;
		}

		// First version above every version the partial covers, e.g. "1.2" -> 1.3.0-0.
		SemanticVersion PartialUpperBound(const PartialVersion& partial) {
			if (partial.Given == 1) return MakeVersion(partial.Numbers[0] + 1, 0, 0, true);
			return MakeVersion(partial.Numbers[0], partial.Numbers[1] + 1, 0, true);
		}
	}

	std::optional<SemanticVersion> SemanticVersion::Parse(std::string_view text) {
		text = Trim(text);
		if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) text.remove_prefix(1);

		SemanticVersion version;
		const size_t plus = text.find('+');
		if (plus != std::string_view::npos) {
			const std::string_view build = text.substr(plus + 1);
			std::vector<std::string> ignored;
			if (!ParseIdentifiers(build, false, ignored)) return std::nullopt;
			version.Build = std::string(build);
			text = text.substr(0, plus);
		}
		const size_t dash = text.find('-');
		if (dash != std::string_view::npos) {
			if (!ParseIdentifiers(text.substr(dash + 1), true, version.PreRelease)) return std::nullopt;
			text = text.substr(0, dash);
		}

		const std::vector<std::string_view> core = Split(text, '.');
		if (core.size() != 3
			|| !ParseNumber(core[0], version.Major)
			|| !ParseNumber(core[1], version.Minor)
			|| !ParseNumber(core[2], version.Patch)) {
			return std::nullopt;
		}
		return version;
	}

	std::string SemanticVersion::ToString() const {
		std::string text = std::to_string(Major) + "." + std::to_string(Minor) + "." + std::to_string(Patch);
		for (size_t i = 0; i < PreRelease.size(); ++i) {
			text += (i == 0 ? "-" : ".") + PreRelease[i];
		}
		if (!Build.empty()) text += "+" + Build;
		return text;
	}

	std::strong_ordering SemanticVersion::operator<=>(const SemanticVersion& other) const {
		if (auto c = Major <=> other.Major; c != 0) return c;
		if (auto c = Minor <=> other.Minor; c != 0) return c;
		if (auto c = Patch <=> other.Patch; c != 0) return c;
		// A pre-release sorts before its release.
		if (PreRelease.empty() || other.PreRelease.empty()) {
			return other.PreRelease.size() == PreRelease.size()
				? std::strong_ordering::equal
				: (PreRelease.empty() ? std::strong_ordering::greater : std::strong_ordering::less);
		}
		const size_t count = std::min(PreRelease.size(), other.PreRelease.size());
		for (size_t i = 0; i < count; ++i) {
			const int result = ComparePreReleaseIdentifier(PreRelease[i], other.PreRelease[i]);
			if (result != 0) return result < 0 ? std::strong_ordering::less : std::strong_ordering::greater;
		}
		return PreRelease.size() <=> other.PreRelease.size();
	}

	VersionRange VersionRange::Any() {
		VersionRange range;
		range.m_Sets.push_back({ Comparator{ Op::GreaterEqual, MakeVersion(0, 0, 0), true } });
		range.m_Text = "*";
		return range;
	}

	std::optional<VersionRange> VersionRange::Parse(std::string_view text, std::string* outError) {
		auto fail = [&](const std::string& message) -> std::optional<VersionRange> {
			if (outError) *outError = message;
			return std::nullopt;
		};

		VersionRange range;
		range.m_Text = std::string(Trim(text));
		if (range.m_Text.empty() || range.m_Text == "*") {
			VersionRange any = Any();
			any.m_Text = range.m_Text.empty() ? "*" : range.m_Text;
			return any;
		}

		size_t setStart = 0;
		const std::string& source = range.m_Text;
		while (setStart <= source.size()) {
			const size_t setEnd = source.find("||", setStart);
			const std::string_view setText = Trim(std::string_view(source).substr(setStart,
				setEnd == std::string::npos ? std::string::npos : setEnd - setStart));
			setStart = setEnd == std::string::npos ? source.size() + 1 : setEnd + 2;

			ComparatorSet set;
			if (setText.empty() || setText == "*") {
				set.push_back({ Op::GreaterEqual, MakeVersion(0, 0, 0), true });
				range.m_Sets.push_back(std::move(set));
				continue;
			}

			// Tokenize, gluing operators to the version that follows ("> = 1.0" is not valid, ">= 1.0" is).
			std::vector<std::string> tokens;
			{
				std::string current;
				for (size_t i = 0; i <= setText.size(); ++i) {
					const char c = i < setText.size() ? setText[i] : ' ';
					if (std::isspace(static_cast<unsigned char>(c))) {
						if (!current.empty()) tokens.push_back(std::move(current));
						current.clear();
					}
					else {
						current.push_back(c);
					}
				}
				std::vector<std::string> glued;
				for (size_t i = 0; i < tokens.size(); ++i) {
					const std::string& token = tokens[i];
					const bool bareOperator = token == ">" || token == ">=" || token == "<" || token == "<="
						|| token == "=" || token == "^" || token == "~";
					if (bareOperator && i + 1 < tokens.size()) {
						glued.push_back(token + tokens[++i]);
					}
					else {
						glued.push_back(token);
					}
				}
				tokens = std::move(glued);
			}

			// Hyphen range "A - B".
			if (tokens.size() == 3 && tokens[1] == "-") {
				PartialVersion low, high;
				if (!ParsePartial(tokens[0], low) || !ParsePartial(tokens[2], high)) {
					return fail("invalid hyphen range '" + std::string(setText) + "'");
				}
				set.push_back({ Op::GreaterEqual, low.Full, false });
				if (high.Given == 3) set.push_back({ Op::LessEqual, high.Full, false });
				else if (high.Given > 0) set.push_back({ Op::Less, PartialUpperBound(high), true });
				range.m_Sets.push_back(std::move(set));
				continue;
			}

			for (const std::string& token : tokens) {
				std::string_view body = token;
				std::string_view op;
				for (std::string_view candidate : { ">=", "<=", ">", "<", "=", "^", "~" }) {
					if (body.substr(0, candidate.size()) == candidate) {
						op = candidate;
						body.remove_prefix(candidate.size());
						break;
					}
				}

				PartialVersion partial;
				if (!ParsePartial(body, partial)) {
					return fail("invalid version '" + std::string(body) + "' in range '" + range.m_Text + "'");
				}
				const uint64_t major = partial.Numbers[0];
				const uint64_t minor = partial.Numbers[1];
				const uint64_t patch = partial.Numbers[2];

				if (op == "^") {
					set.push_back({ Op::GreaterEqual, partial.Full, false });
					SemanticVersion upper;
					if (major > 0 || partial.Given == 1) upper = MakeVersion(major + 1, 0, 0, true);
					else if (minor > 0 || partial.Given == 2) upper = MakeVersion(0, minor + 1, 0, true);
					else upper = MakeVersion(0, 0, patch + 1, true);
					if (partial.Given == 0) continue; // "^*" == any
					set.push_back({ Op::Less, upper, true });
				}
				else if (op == "~") {
					set.push_back({ Op::GreaterEqual, partial.Full, false });
					if (partial.Given == 0) continue;
					set.push_back({ Op::Less, partial.Given == 1 ? MakeVersion(major + 1, 0, 0, true) : MakeVersion(major, minor + 1, 0, true), true });
				}
				else if (partial.Given == 3) {
					Op operation = Op::Equal;
					if (op == ">") operation = Op::Greater;
					else if (op == ">=") operation = Op::GreaterEqual;
					else if (op == "<") operation = Op::Less;
					else if (op == "<=") operation = Op::LessEqual;
					set.push_back({ operation, partial.Full, false });
				}
				else if (partial.Given == 0) {
					// "*", ">=*" match everything; "<*" or ">*" match nothing.
					if (op == "<" || op == ">") set.push_back({ Op::Less, MakeVersion(0, 0, 0, true), true });
					else set.push_back({ Op::GreaterEqual, MakeVersion(0, 0, 0), true });
				}
				else if (op.empty() || op == "=") {
					set.push_back({ Op::GreaterEqual, partial.Full, false });
					set.push_back({ Op::Less, PartialUpperBound(partial), true });
				}
				else if (op == ">") {
					set.push_back({ Op::GreaterEqual, PartialUpperBound(partial).WithoutPreRelease(), false });
				}
				else if (op == ">=") {
					set.push_back({ Op::GreaterEqual, partial.Full, false });
				}
				else if (op == "<") {
					SemanticVersion bound = partial.Full;
					bound.PreRelease = { "0" };
					set.push_back({ Op::Less, bound, true });
				}
				else if (op == "<=") {
					set.push_back({ Op::Less, PartialUpperBound(partial), true });
				}
			}
			if (set.empty()) {
				return fail("empty comparator set in range '" + range.m_Text + "'");
			}
			range.m_Sets.push_back(std::move(set));
		}

		if (range.m_Sets.empty()) {
			return fail("empty range");
		}
		return range;
	}

	bool VersionRange::Matches(const Comparator& comparator, const SemanticVersion& version) {
		const auto order = version <=> comparator.Version;
		switch (comparator.Operation) {
		case Op::Less: return order < 0;
		case Op::LessEqual: return order <= 0;
		case Op::Greater: return order > 0;
		case Op::GreaterEqual: return order >= 0;
		case Op::Equal: return order == 0;
		}
		return false;
	}

	bool VersionRange::IsSatisfiedBy(const SemanticVersion& version) const {
		for (const ComparatorSet& set : m_Sets) {
			if (!std::all_of(set.begin(), set.end(), [&](const Comparator& c) { return Matches(c, version); })) {
				continue;
			}
			if (!version.IsPreRelease()) {
				return true;
			}
			const bool optedIn = std::any_of(set.begin(), set.end(), [&](const Comparator& c) {
				return !c.Synthetic && c.Version.IsPreRelease() && c.Version.SameCore(version);
			});
			if (optedIn) return true;
		}
		return false;
	}

	bool VersionRange::IsAny() const {
		return m_Sets.size() == 1 && m_Sets.front().size() == 1
			&& m_Sets.front().front().Operation == Op::GreaterEqual
			&& m_Sets.front().front().Version == MakeVersion(0, 0, 0);
	}

}
