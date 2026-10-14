#include "pch.hpp"
#include "Graphics/Material.hpp"

#include "Assets/AssetRegistry.hpp"
#include "Serialization/File.hpp"
#include "Serialization/FileStamp.hpp"

#include <charconv>
#include <chrono>
#include <unordered_map>

namespace Index {
	namespace {
		Json::Value ColorToJson(const Color& c) {
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("r", Json::Value(c.r));
			v.AddMember("g", Json::Value(c.g));
			v.AddMember("b", Json::Value(c.b));
			v.AddMember("a", Json::Value(c.a));
			return v;
		}

		Color ColorFromJson(const Json::Value* v, const Color& fallback) {
			Color c = fallback;
			if (!v || !v->IsObject()) return c;
			if (const Json::Value* x = v->FindMember("r")) c.r = static_cast<float>(x->AsDoubleOr(c.r));
			if (const Json::Value* x = v->FindMember("g")) c.g = static_cast<float>(x->AsDoubleOr(c.g));
			if (const Json::Value* x = v->FindMember("b")) c.b = static_cast<float>(x->AsDoubleOr(c.b));
			if (const Json::Value* x = v->FindMember("a")) c.a = static_cast<float>(x->AsDoubleOr(c.a));
			return c;
		}

		Json::Value Vec2ToJson(const glm::vec2& value) {
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("x", Json::Value(value.x));
			v.AddMember("y", Json::Value(value.y));
			return v;
		}

		glm::vec2 Vec2FromJson(const Json::Value* v, const glm::vec2& fallback) {
			glm::vec2 result = fallback;
			if (!v || !v->IsObject()) return result;
			if (const Json::Value* x = v->FindMember("x")) result.x = static_cast<float>(x->AsDoubleOr(result.x));
			if (const Json::Value* y = v->FindMember("y")) result.y = static_cast<float>(y->AsDoubleOr(result.y));
			return result;
		}

		float ReadFloat(const Json::Value& v, const char* key, float fallback) {
			const Json::Value* m = v.FindMember(key);
			return m ? static_cast<float>(m->AsDoubleOr(fallback)) : fallback;
		}

		// GUIDs travel as decimal strings: JSON doubles cannot hold 64 bits, and build pruning finds digit runs.
		uint64_t ReadGuid(const Json::Value* v) {
			if (!v) return 0;
			if (v->IsNumber()) return v->AsUInt64Or(0);
			const std::string text = v->AsStringOr();
			uint64_t guid = 0;
			const auto result = std::from_chars(text.data(), text.data() + text.size(), guid);
			return result.ec == std::errc() ? guid : 0;
		}

		struct Entry {
			Material Value;
			std::string Path;
			bool Valid = false;
			std::chrono::steady_clock::time_point LastAttempt{};
		};

		std::unordered_map<uint64_t, Entry>& Entries() {
			static std::unordered_map<uint64_t, Entry> s_Entries;
			return s_Entries;
		}

		bool LoadEntry(uint64_t guid, Entry& entry) {
			entry.LastAttempt = std::chrono::steady_clock::now();
			entry.Path = AssetRegistry::ResolvePath(guid);
			entry.Valid = !entry.Path.empty() && MaterialLibrary::LoadFromFile(entry.Path, entry.Value);
			if (!entry.Valid) entry.Value = Material{};
			return entry.Valid;
		}
	}

	Json::Value MaterialToJson(const Material& material) {
		Json::Value root = Json::Value::MakeObject();
		root.AddMember("type", Json::Value("Material"));
		root.AddMember("version", Json::Value(k_MaterialFormatVersion));
		root.AddMember("lit", Json::Value(material.Lit));
		root.AddMember("surface", Json::Value(static_cast<int>(material.Surface)));
		root.AddMember("baseColor", ColorToJson(material.BaseColor));
		root.AddMember("baseColorTexture", Json::Value(std::to_string(material.BaseColorTexture)));
		root.AddMember("metallic", Json::Value(material.Metallic));
		root.AddMember("roughness", Json::Value(material.Roughness));
		root.AddMember("emissiveColor", ColorToJson(material.EmissiveColor));
		root.AddMember("emissiveIntensity", Json::Value(material.EmissiveIntensity));
		root.AddMember("tiling", Vec2ToJson(material.Tiling));
		root.AddMember("offset", Vec2ToJson(material.Offset));
		return root;
	}

	Material MaterialFromJson(const Json::Value& value) {
		const Material d{};
		Material m;
		if (!value.IsObject()) return m;
		const Json::Value* lit = value.FindMember("lit");
		m.Lit = lit ? lit->AsBoolOr(d.Lit) : d.Lit;
		const Json::Value* surface = value.FindMember("surface");
		const int surfaceValue = surface ? surface->AsIntOr(static_cast<int>(d.Surface)) : static_cast<int>(d.Surface);
		m.Surface = surfaceValue >= 0 && surfaceValue < k_MaterialSurfaceCount ? static_cast<MaterialSurface>(surfaceValue) : d.Surface;
		m.BaseColor = ColorFromJson(value.FindMember("baseColor"), d.BaseColor);
		m.BaseColorTexture = ReadGuid(value.FindMember("baseColorTexture"));
		m.Metallic = std::clamp(ReadFloat(value, "metallic", d.Metallic), 0.0f, 1.0f);
		m.Roughness = std::clamp(ReadFloat(value, "roughness", d.Roughness), 0.0f, 1.0f);
		m.EmissiveColor = ColorFromJson(value.FindMember("emissiveColor"), d.EmissiveColor);
		m.EmissiveIntensity = std::max(ReadFloat(value, "emissiveIntensity", d.EmissiveIntensity), 0.0f);
		m.Tiling = Vec2FromJson(value.FindMember("tiling"), d.Tiling);
		m.Offset = Vec2FromJson(value.FindMember("offset"), d.Offset);
		return m;
	}

	const Material* MaterialLibrary::Get(uint64_t guid) {
		if (guid == 0) return nullptr;
		auto& entries = Entries();
		auto it = entries.find(guid);
		if (it == entries.end()) {
			it = entries.emplace(guid, Entry{}).first;
			LoadEntry(guid, it->second);
		}
		else if (!it->second.Valid && std::chrono::steady_clock::now() - it->second.LastAttempt > std::chrono::seconds(1)) {
			LoadEntry(guid, it->second);
		}
		return it->second.Valid ? &it->second.Value : nullptr;
	}

	bool MaterialLibrary::LoadFromFile(const std::string& path, Material& outMaterial) {
		if (!File::Exists(path)) return false;
		Json::Value root;
		if (!Json::TryParse(File::ReadAllText(path), root) || !root.IsObject()) {
			IDX_CORE_WARN_TAG("Material", "Could not parse material '{}'", path);
			return false;
		}
		KnownFileStamps::Record(path);
		outMaterial = MaterialFromJson(root);
		return true;
	}

	bool MaterialLibrary::SaveToFile(const std::string& path, const Material& material) {
		if (!File::WriteAllText(path, Json::Stringify(MaterialToJson(material), true))) {
			IDX_CORE_ERROR_TAG("Material", "Could not write material '{}'", path);
			return false;
		}
		KnownFileStamps::Record(path);
		return true;
	}

	bool MaterialLibrary::Save(uint64_t guid, const Material& material) {
		if (guid == 0) return false;
		const std::string path = AssetRegistry::ResolvePath(guid);
		if (path.empty() || !SaveToFile(path, material)) return false;
		Entry& entry = Entries()[guid];
		entry.Value = material;
		entry.Path = path;
		entry.Valid = true;
		return true;
	}

	void MaterialLibrary::Set(uint64_t guid, const Material& material) {
		if (guid == 0) return;
		Entry& entry = Entries()[guid];
		entry.Value = material;
		entry.Valid = true;
	}

	void MaterialLibrary::Invalidate(uint64_t guid) {
		Entries().erase(guid);
	}

	size_t MaterialLibrary::ReloadChangedFromDisk() {
		size_t reloaded = 0;
		for (auto& [guid, entry] : Entries()) {
			if (entry.Path.empty() || !KnownFileStamps::HasChangedOnDisk(entry.Path)) continue;
			LoadEntry(guid, entry);
			++reloaded;
		}
		return reloaded;
	}

	void MaterialLibrary::Clear() {
		Entries().clear();
	}

}
