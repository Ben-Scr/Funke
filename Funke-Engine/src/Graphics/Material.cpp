#include "pch.hpp"
#include "Graphics/Material.hpp"

#include "Assets/AssetRegistry.hpp"
#include "Serialization/File.hpp"
#include "Serialization/FileStamp.hpp"

#include <charconv>
#include <cstring>
#include <chrono>
#include <unordered_map>

namespace Funke {
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

		// Self-describing so a material loads without its shader: number = float, array = vecN, {r,g,b,a} = color,
		// string = texture GUID.
		Json::Value PropertyToJson(const MaterialPropertyValue& property) {
			switch (property.Type) {
				case ShaderPropertyType::Float:
					return Json::Value(property.Value.x);
				case ShaderPropertyType::Color:
					return ColorToJson(Color(property.Value.r, property.Value.g, property.Value.b, property.Value.a));
				case ShaderPropertyType::Texture:
					return Json::Value(std::to_string(property.Texture));
				case ShaderPropertyType::Vec2:
				case ShaderPropertyType::Vec3:
				case ShaderPropertyType::Vec4: {
					Json::Value array = Json::Value::MakeArray();
					const int count = property.Type == ShaderPropertyType::Vec2 ? 2 : property.Type == ShaderPropertyType::Vec3 ? 3 : 4;
					for (int i = 0; i < count; ++i) array.Append(Json::Value(property.Value[i]));
					return array;
				}
			}
			return Json::Value(property.Value.x);
		}

		bool PropertyFromJson(const Json::Value& value, MaterialPropertyValue& out) {
			if (value.IsNumber()) {
				out.Type = ShaderPropertyType::Float;
				out.Value = glm::vec4(static_cast<float>(value.AsDoubleOr(0.0)), 0.0f, 0.0f, 0.0f);
				return true;
			}
			if (value.IsString()) {
				out.Type = ShaderPropertyType::Texture;
				out.Texture = ReadGuid(&value);
				return true;
			}
			if (value.IsObject()) {
				const Color c = ColorFromJson(&value, Color(1.0f, 1.0f, 1.0f, 1.0f));
				out.Type = ShaderPropertyType::Color;
				out.Value = glm::vec4(c.r, c.g, c.b, c.a);
				return true;
			}
			if (value.IsArray()) {
				const auto& items = value.GetArray();
				if (items.size() < 2 || items.size() > 4) return false;
				out.Type = items.size() == 2 ? ShaderPropertyType::Vec2 : items.size() == 3 ? ShaderPropertyType::Vec3 : ShaderPropertyType::Vec4;
				out.Value = glm::vec4(0.0f);
				for (size_t i = 0; i < items.size(); ++i) out.Value[static_cast<int>(i)] = static_cast<float>(items[i].AsDoubleOr(0.0));
				return true;
			}
			return false;
		}

		// Vectors and colors share the same storage, so a color property reads a vec4 value and vice versa.
		bool IsCompatible(ShaderPropertyType stored, ShaderPropertyType declared) {
			if (stored == declared) return true;
			auto isFourWide = [](ShaderPropertyType t) { return t == ShaderPropertyType::Vec4 || t == ShaderPropertyType::Color; };
			return isFourWide(stored) && isFourWide(declared);
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
		if (!material.Fog) root.AddMember("fog", Json::Value(false));
		root.AddMember("surface", Json::Value(static_cast<int>(material.Surface)));
		root.AddMember("baseColor", ColorToJson(material.BaseColor));
		root.AddMember("baseColorTexture", Json::Value(std::to_string(material.BaseColorTexture)));
		root.AddMember("metallic", Json::Value(material.Metallic));
		root.AddMember("roughness", Json::Value(material.Roughness));
		root.AddMember("emissiveColor", ColorToJson(material.EmissiveColor));
		root.AddMember("emissiveIntensity", Json::Value(material.EmissiveIntensity));
		root.AddMember("tiling", Vec2ToJson(material.Tiling));
		root.AddMember("offset", Vec2ToJson(material.Offset));
		if (material.Shader != 0) root.AddMember("shader", Json::Value(std::to_string(material.Shader)));
		if (!material.Properties.empty()) {
			Json::Value properties = Json::Value::MakeObject();
			for (const auto& [name, property] : material.Properties) properties.AddMember(name, PropertyToJson(property));
			root.AddMember("properties", std::move(properties));
		}
		return root;
	}

	MaterialPropertyValue MaterialPropertyValue::FromDefault(const ShaderProperty& property) {
		MaterialPropertyValue value;
		value.Type = property.Type;
		value.Value = property.Default;
		return value;
	}

	MaterialPropertyValue ResolveMaterialProperty(const Material& material, const ShaderProperty& property) {
		MaterialPropertyValue value = MaterialPropertyValue::FromDefault(property);
		const auto it = material.Properties.find(property.Name);
		if (it == material.Properties.end() || !IsCompatible(it->second.Type, property.Type)) return value;
		if (property.Type == ShaderPropertyType::Texture) {
			value.Texture = it->second.Texture;
			return value;
		}
		value.Value = it->second.Value;
		if (property.HasRange) value.Value = glm::clamp(value.Value, glm::vec4(property.Min), glm::vec4(property.Max));
		return value;
	}

	void PackMaterialProperties(const Material& material, const ShaderDescription& description, std::vector<uint8_t>& outBytes) {
		outBytes.assign(description.UniformSize, 0);
		for (const ShaderProperty& property : description.Properties) {
			if (property.Type == ShaderPropertyType::Texture) continue;
			const size_t bytes = static_cast<size_t>(property.GetComponentCount()) * sizeof(float);
			if (property.Offset + bytes > outBytes.size()) continue;
			const MaterialPropertyValue value = ResolveMaterialProperty(material, property);
			std::memcpy(outBytes.data() + property.Offset, &value.Value[0], bytes);
		}
	}

	Material MaterialFromJson(const Json::Value& value) {
		const Material d{};
		Material m;
		if (!value.IsObject()) return m;
		const Json::Value* lit = value.FindMember("lit");
		m.Lit = lit ? lit->AsBoolOr(d.Lit) : d.Lit;
		const Json::Value* fog = value.FindMember("fog");
		m.Fog = fog ? fog->AsBoolOr(d.Fog) : d.Fog;
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
		m.Shader = ReadGuid(value.FindMember("shader"));
		if (const Json::Value* properties = value.FindMember("properties"); properties && properties->IsObject()) {
			for (const auto& [name, property] : properties->GetObject()) {
				MaterialPropertyValue parsed;
				if (PropertyFromJson(property, parsed)) m.Properties[name] = parsed;
			}
		}
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
			FNK_CORE_WARN_TAG("Material", "Could not parse material '{}'", path);
			return false;
		}
		KnownFileStamps::Record(path);
		outMaterial = MaterialFromJson(root);
		return true;
	}

	bool MaterialLibrary::SaveToFile(const std::string& path, const Material& material) {
		if (!File::WriteAllText(path, Json::Stringify(MaterialToJson(material), true))) {
			FNK_CORE_ERROR_TAG("Material", "Could not write material '{}'", path);
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
