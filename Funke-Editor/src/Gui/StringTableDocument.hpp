#pragma once
#include "Serialization/Json.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace Funke {
	class AssetFileHistory;
	class StringTableDocument {
	public:
		bool Open(const std::filesystem::path& path);
		bool Create(const std::filesystem::path& directory, const std::string& code, const std::string& name);
		bool Save(AssetFileHistory& history);
		bool Refresh();
		bool Set(const std::string& key, const std::string& value);
		bool Rename(const std::string& key, const std::string& next);
		bool Remove(const std::string& key);
		const Json::Value* Strings() const { return m_Valid ? m_Root.FindMember("strings") : nullptr; }
		const std::filesystem::path& Path() const { return m_Path; }
		const std::string& Error() const { return m_Error; }
		bool IsValid() const { return m_Valid; }
		bool IsDirty() const { return m_Dirty; }
		bool HasConflict() const { return m_Conflict; }
	private:
		bool DiskUnchanged() const;
		std::filesystem::path m_Path;
		Json::Value m_Root;
		std::string m_Baseline, m_Error;
		bool m_Valid = false, m_Dirty = false, m_New = false, m_Conflict = false;
	};
}
