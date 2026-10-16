#include "pch.hpp"
#include "Editor/ScriptWindowTypes.hpp"

#include "Serialization/Json.hpp"

#include <algorithm>

namespace Funke {

	std::vector<ScriptWindowType> ParseScriptWindowTypes(const std::string& json) {
		std::vector<ScriptWindowType> types;
		Json::Value root;
		if (!Json::TryParse(json, root) || !root.IsArray()) return types;
		for (const Json::Value& item : root.GetArray()) {
			if (!item.IsObject()) continue;
			const Json::Value* type = item.FindMember("type");
			if (!type || !type->IsString() || type->AsStringOr().empty()) continue;
			ScriptWindowType entry;
			entry.Type = type->AsStringOr();
			if (const Json::Value* title = item.FindMember("title"); title && title->IsString()) entry.Title = title->AsStringOr();
			if (const Json::Value* menu = item.FindMember("menu"); menu && menu->IsString()) entry.Menu = menu->AsStringOr();
			if (const Json::Value* width = item.FindMember("width"); width && width->IsNumber()) entry.Width = static_cast<float>(width->AsDoubleOr(0.0));
			if (const Json::Value* height = item.FindMember("height"); height && height->IsNumber()) entry.Height = static_cast<float>(height->AsDoubleOr(0.0));
			if (entry.Title.empty()) entry.Title = entry.Type.substr(entry.Type.find_last_of('.') + 1);
			if (SplitMenuPath(entry.Menu).empty()) entry.Menu = entry.Title;
			types.push_back(std::move(entry));
		}
		std::stable_sort(types.begin(), types.end(), [](const auto& a, const auto& b) { return a.Menu < b.Menu; });
		return types;
	}

	std::vector<std::string> SplitMenuPath(const std::string& path) {
		std::vector<std::string> parts;
		size_t start = 0;
		while (start <= path.size()) {
			const size_t slash = path.find('/', start);
			const size_t end = slash == std::string::npos ? path.size() : slash;
			if (end > start) parts.push_back(path.substr(start, end - start));
			if (slash == std::string::npos) break;
			start = slash + 1;
		}
		return parts;
	}

}
