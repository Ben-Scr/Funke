#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Funke {

	enum class ScriptType : uint8_t { Unknown, Managed, Native, Bolt };

	inline constexpr const char* ScriptTypeToString(ScriptType type)
	{
		switch (type) {
			case ScriptType::Bolt:    return "Bolt";
			case ScriptType::Managed: return "Managed";
			case ScriptType::Native:  return "Native";
			default:                  return "Unknown";
		}
	}

	inline constexpr bool CanBeManagedScript(ScriptType type) {
		return type == ScriptType::Managed || type == ScriptType::Unknown;
	}

	inline bool ScriptTypeEqualsIgnoreCase(std::string_view a, std::string_view b)
	{
		if (a.size() != b.size()) {
			return false;
		}

		for (std::size_t i = 0; i < a.size(); ++i) {
			char ca = a[i];
			char cb = b[i];
			if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
			if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
			if (ca != cb) {
				return false;
			}
		}

		return true;
	}

	inline ScriptType ScriptTypeFromString(std::string_view value)
	{
		if (ScriptTypeEqualsIgnoreCase(value, "Bolt")) return ScriptType::Bolt;
		if (ScriptTypeEqualsIgnoreCase(value, "Managed") || ScriptTypeEqualsIgnoreCase(value, "CSharp") || value == "C#") {
			return ScriptType::Managed;
		}
		if (ScriptTypeEqualsIgnoreCase(value, "Native") || ScriptTypeEqualsIgnoreCase(value, "Cpp") || value == "C++") {
			return ScriptType::Native;
		}

		return ScriptType::Unknown;
	}

} // namespace Funke
