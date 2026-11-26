#include <pch.hpp>
#include "Gui/StringTableDocument.hpp"
#include "Undo/AssetFileHistory.hpp"
#include "Localization/GameLocalization.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include <algorithm>
#include <cctype>

namespace Funke {
	bool StringTableDocument::Open(const std::filesystem::path& path) {
		m_Path = path;
		m_Valid = m_Dirty = m_New = m_Conflict = false;
		m_Error.clear();
		m_Baseline = File::ReadAllText(Funke::Path::ToUtf8(path));
		if (!Json::TryParseUnique(m_Baseline, m_Root, &m_Error) || !m_Root.IsObject()) {
			if (m_Error.empty()) m_Error = "The table must be a JSON object.";
			return false;
		}
		auto* strings = m_Root.FindMember("strings");
		if (!strings || !strings->IsObject()) { m_Error = "Missing strings object."; return false; }
		for (const auto& [key, value] : strings->EnsureObject()) {
			if (!value.IsString()) { m_Error = "Translation is not a string: " + key; return false; }
		}
		if (const auto* meta = m_Root.FindMember("meta")) {
			if (!meta->IsObject()) { m_Error = "Invalid language metadata."; return false; }
			for (const char* field : { "code", "displayName" })
				if (const auto* value = meta->FindMember(field); value && !value->IsString()) {
					m_Error = "Invalid language metadata field: " + std::string(field); return false;
				}
		}
		m_Valid = true;
		return true;
	}

	bool StringTableDocument::Create(const std::filesystem::path& directory, const std::string& code, const std::string& name) {
		if (code.empty() || code.size() > 32 || !std::all_of(code.begin(), code.end(), [](unsigned char c) {
			return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
		})) { m_Error = "Use a lowercase language code, for example en or pt-br."; return false; }
		const auto path = directory / (code + ".json");
		if (!AssetFileHistory::DestinationAvailable(path)) { m_Error = "That language file already exists."; return false; }
		m_Path = path;
		m_Root = Json::Value::MakeObject();
		auto meta = Json::Value::MakeObject();
		meta.AddMember("code", code);
		meta.AddMember("displayName", name.empty() ? code : name);
		m_Root.AddMember("meta", std::move(meta));
		m_Root.AddMember("strings", Json::Value::MakeObject());
		m_Baseline.clear(); m_Error.clear();
		m_Valid = m_Dirty = m_New = true; m_Conflict = false;
		return true;
	}

	bool StringTableDocument::DiskUnchanged() const {
		return m_New ? AssetFileHistory::DestinationAvailable(m_Path)
			: File::IsRegularFile(m_Path) && File::ReadAllText(Funke::Path::ToUtf8(m_Path)) == m_Baseline;
	}
	bool StringTableDocument::Refresh() {
		if (m_Path.empty() || DiskUnchanged()) return true;
		if (m_Dirty) {
			m_Conflict = true; m_Error = "The file changed externally. Reload it before saving."; return false;
		}
		return Open(m_Path);
	}
	bool StringTableDocument::Save(AssetFileHistory& history) {
		if (!m_Valid || m_Conflict || !m_Dirty) return false;
		if (!DiskUnchanged()) { m_Conflict = true; m_Error = "The file changed externally. Reload it before saving."; return false; }
		try {
			std::filesystem::create_directories(m_Path.parent_path());
			auto change = history.Begin({ Funke::Path::ToUtf8(m_Path) }, "Edit String Table");
			if (!DiskUnchanged()) { m_Conflict = true; m_Error = "The file changed while preparing the save."; return false; }
			const auto text = Json::Stringify(m_Root, true) + "\n";
			if (!File::WriteAllText(Funke::Path::ToUtf8(m_Path), text)) { m_Error = "Could not write the string table."; return false; }
			// Keep runtime/preview tables in sync for save and both history directions.
			change->Applied = [](bool) { GameLocalization::Reload(); };
			try { history.Commit(change); }
            catch (...) {
                // Roll back only our write; preserve a concurrent external replacement.
                bool restored = false;
                if (File::ReadAllText(Funke::Path::ToUtf8(m_Path)) == text) {
                    if (m_New) { std::error_code ec; restored = std::filesystem::remove(m_Path, ec) && !ec; }
                    else restored = File::WriteAllText(Funke::Path::ToUtf8(m_Path), m_Baseline);
                }
                if (!restored) { change->KeepRecoveryFiles = true; m_Conflict = true; }
                throw;
            }
			m_Baseline = text; m_Dirty = m_New = false; m_Error.clear();
			GameLocalization::Reload();
			return true;
		} catch (const std::exception& e) { m_Error = e.what(); return false; }
	}
	bool StringTableDocument::Set(const std::string& key, const std::string& value) {
		if (!m_Valid || key.empty()) return false;
		auto* strings = m_Root.FindMember("strings");
		if (const auto* old = strings->FindMember(key); old && old->AsStringOr() == value) return true;
		strings->AddMember(key, value); m_Dirty = true; return true;
	}
	bool StringTableDocument::Rename(const std::string& key, const std::string& next) {
		if (!m_Valid || next.empty() || key == next) return false;
		auto* strings = m_Root.FindMember("strings");
		if (strings->FindMember(next)) { m_Error = "That key already exists."; return false; }
		for (auto& [name, value] : strings->EnsureObject()) {
			if (name == key) { name = next; m_Dirty = true; m_Error.clear(); return true; }
		}
		return false;
	}
	bool StringTableDocument::Remove(const std::string& key) {
		if (!m_Valid) return false;
		auto& items = m_Root.FindMember("strings")->EnsureObject();
		const auto count = std::erase_if(items, [&](const auto& item) { return item.first == key; });
		m_Dirty |= count != 0; return count != 0;
	}
}
