#pragma once

#include "Core/Export.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace Index {

	// Values are the GL texture-wrap constants (GL_REPEAT, GL_CLAMP_TO_EDGE,
	// GL_MIRRORED_REPEAT, GL_CLAMP_TO_BORDER) so they can be handed to the
	// backend without a translation table.
	enum class INDEX_API Wrap : uint32_t { Repeat = 0x2901, Clamp = 0x812F, Mirror = 0x8370, Border = 0x812D };

	// Hand-written name mapping, deliberately NOT magic_enum.
	//
	// magic_enum reflects an enum by instantiating a constexpr __FUNCSIG__ parse
	// for every integer in [enum_range::min, enum_range::max]. Because these
	// values are sparse GL constants, making reflection work here needed a
	// customised range of 0x2901..0x8370 — 23,152 instantiations to recover four
	// enumerators. That one specialisation also opted Wrap out of the workspace's
	// global MAGIC_ENUM_RANGE_MAX=32 guard, and it was the direct cause of
	// nondeterministic `fatal error C1060: out of heap space` in every TU that
	// reached AssetRegistry.hpp (SpriteUVResolver.cpp, ShaderManager.cpp,
	// FontManager.cpp — the .meta read/write path reflects Wrap).
	//
	// The strings below MUST keep matching the magic_enum names exactly:
	// existing .meta files on disk store them as "Repeat"/"Clamp"/"Mirror"/
	// "Border", and ReadTextureMeta parses them back.
	constexpr std::string_view ToString(Wrap wrap) noexcept {
		switch (wrap) {
		case Wrap::Repeat: return "Repeat";
		case Wrap::Clamp:  return "Clamp";
		case Wrap::Mirror: return "Mirror";
		case Wrap::Border: return "Border";
		}
		return "Repeat";
	}

	// Returns false and leaves `outWrap` untouched on an unknown name, so callers
	// keep whatever default they already hold (matches the previous
	// magic_enum::enum_cast(...).has_value() flow).
	constexpr bool TryParse(std::string_view name, Wrap& outWrap) noexcept {
		if (name == "Repeat") { outWrap = Wrap::Repeat; return true; }
		if (name == "Clamp")  { outWrap = Wrap::Clamp;  return true; }
		if (name == "Mirror") { outWrap = Wrap::Mirror; return true; }
		if (name == "Border") { outWrap = Wrap::Border; return true; }
		return false;
	}

	// Enumeration hook for UI that would otherwise call magic_enum::enum_values.
	// Found by ADL on std::type_identity; ImGuiUtils::DrawEnumCombo prefers this
	// (and ToString above) over reflection whenever an enum provides them.
	constexpr std::array<Wrap, 4> EnumValues(std::type_identity<Wrap>) noexcept {
		return { Wrap::Repeat, Wrap::Clamp, Wrap::Mirror, Wrap::Border };
	}

} // namespace Index
