#include "pch.hpp"
#include "Commands.hpp"
#include "Commands/CommandHelpers.hpp"

#include "Animation/AnimationClip.hpp"
#include "Animation/AnimatorController.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Build/GameBuilder.hpp"
#include "Editor/SpriteSlicing.hpp"
#include "Graphics/CustomShaderLibrary.hpp"
#include "Graphics/ImageProcessing.hpp"
#include "Graphics/Text/FontHandle.hpp"
#include "Project/IndexBuildProfile.hpp"
#include "Project/IndexProject.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/SceneSerializerStorage.hpp"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Index::Cli {

	namespace {
		std::optional<uint64_t> ParseGuid(const Json::Value& value) {
			if (value.IsString()) {
				const std::string text = value.AsStringOr();
				if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) return std::nullopt;
				try {
					return static_cast<uint64_t>(std::stoull(text));
				}
				catch (...) {
					return std::nullopt;
				}
			}
			if (value.IsNumber()) return value.AsUInt64Or(0);
			return std::nullopt;
		}

		bool IsMetaPath(const std::filesystem::path& path) {
			return ToLower(Path::ToUtf8(path.extension())) == ".meta";
		}

		std::vector<std::filesystem::path> FilesUnder(const std::filesystem::path& root) {
			std::vector<std::filesystem::path> files;
			std::error_code error;
			for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error), end;
				it != end; it.increment(error)) {
				if (error) { error.clear(); continue; }
				if (it->is_regular_file(error) && !error) files.push_back(it->path());
				error.clear();
			}
			std::sort(files.begin(), files.end());
			return files;
		}

		std::string KindName(AssetKind kind) {
			switch (kind) {
				case AssetKind::Texture:   return "texture";
				case AssetKind::Audio:     return "audio";
				case AssetKind::Scene:     return "scene";
				case AssetKind::Prefab:    return "prefab";
				case AssetKind::Script:    return "script";
				case AssetKind::Font:      return "font";
				case AssetKind::Shader:    return "shader";
				case AssetKind::DataAsset: return "dataasset";
				case AssetKind::Material:  return "material";
				case AssetKind::Mesh:      return "mesh";
				case AssetKind::AnimationClip:      return "animationclip";
				case AssetKind::AnimatorController: return "animatorcontroller";
				case AssetKind::Other:     return "other";
				case AssetKind::Unknown:   break;
			}
			return "unknown";
		}

		std::filesystem::path TexturePathArgument(const IndexProject& project, const std::string& value) {
			std::filesystem::path path = ResolveProjectPath(project, value);
			if (IsMetaPath(path)) path.replace_extension();
			return path;
		}
	}

	// ── convert ─────────────────────────────────────────────────────────────

	int RunConvert(Args& args) {
		std::string error;
		IndexProject* project = OpenProject(args, error);
		const std::optional<std::string> formatOption = args.Option("format");
		const bool keepSetting = args.Flag("keep-setting");
		const std::vector<std::string> targets = args.RemainingPositionals();
		if (!args.Finish(error)) return Fail(error, k_ExitUsage);
		if (!project) return Fail(error);
		IndexProject::ProjectAssetSerializationFormat format{};
		if (!formatOption || !ParseAssetFormat(*formatOption, format)) return Fail("--format json|binary is required", k_ExitUsage);
		const SceneSerializationFormat sceneFormat = GameBuilder::ToSceneSerializationFormat(format);

		int converted = 0;
		std::string failure;
		if (targets.empty()) {
			converted = GameBuilder::ConvertSerializedAssets(Path::FromUtf8(project->AssetsDirectory), sceneFormat, {}, &failure);
			if (!keepSetting && project->AssetSerializationFormat != format) {
				project->AssetSerializationFormat = format;
				if (!project->Save()) return Fail("files converted, but project.json could not be saved");
				Note(std::string("project.json: assetSerializationFormat = ") + IndexProject::ProjectAssetSerializationFormatToString(format));
			}
		}
		else {
			for (const std::string& target : targets) {
				const std::filesystem::path path = ResolveProjectPath(*project, target);
				std::error_code fsError;
				if (std::filesystem::is_directory(path, fsError)) {
					converted += GameBuilder::ConvertSerializedAssets(path, sceneFormat, {}, &failure);
				}
				else if (GameBuilder::IsSerializedAssetPath(path)) {
					if (SceneSerializer::ConvertFileFormat(Path::ToUtf8(path), sceneFormat)) ++converted;
					else if (failure.empty()) failure = "failed to convert " + Path::ToUtf8(path);
				}
				else {
					return Fail(Path::ToUtf8(path) + " is not a .scene, .prefab, .dataasset or folder", k_ExitUsage);
				}
			}
		}
		Out("Converted " + std::to_string(converted) + " file(s) to " + IndexProject::ProjectAssetSerializationFormatToString(format));
		if (!failure.empty()) return Fail(failure);
		return k_ExitOk;
	}

	// ── validate ────────────────────────────────────────────────────────────

	namespace {
		struct Issue {
			bool IsError = false;
			std::string File;
			std::string Message;
		};

		class Validator {
		public:
			explicit Validator(IndexProject& project) : m_Project(project) {}

			std::vector<Issue> Run() {
				CheckProject();
				CheckBuildProfiles();
				IndexMetas();
				CheckSerializedFiles();
				CheckLocalization();
				CheckShaders();
				CheckAnimations();
				return std::move(m_Issues);
			}

		private:
			void Error(const std::filesystem::path& file, std::string message) { Add(true, file, std::move(message)); }
			void Warning(const std::filesystem::path& file, std::string message) { Add(false, file, std::move(message)); }

			void Add(bool isError, const std::filesystem::path& file, std::string message) {
				std::error_code error;
				std::filesystem::path shown = std::filesystem::relative(file, Path::FromUtf8(m_Project.RootDirectory), error);
				if (error || shown.empty() || *shown.begin() == "..") shown = file;
				m_Issues.push_back({ isError, Path::ToGenericUtf8(shown), std::move(message) });
			}

			void CheckProject() {
				const std::filesystem::path projectFile = Path::FromUtf8(m_Project.ProjectFilePath);
				Json::Value root;
				std::string parseError;
				if (!Json::TryParse(File::ReadAllText(m_Project.ProjectFilePath), root, &parseError) || !root.IsObject()) {
					Error(projectFile, "not valid JSON: " + parseError);
					return;
				}
				if (m_Project.AssetSerializationFormat == IndexProject::ProjectAssetSerializationFormat::Binary) {
					Warning(projectFile, "assetSerializationFormat is Binary: scenes and prefabs are not text-editable "
						"(`index convert --format json`)");
				}
				const auto sceneExists = [&](const std::string& name) {
					std::error_code error;
					return std::filesystem::is_regular_file(Path::FromUtf8(m_Project.GetSceneFilePath(name)), error);
				};
				if (!sceneExists(m_Project.StartupScene)) {
					Error(projectFile, "startupScene '" + m_Project.StartupScene + "' has no .scene file under Assets/");
				}
				for (const IndexProject::BuildSceneEntry& entry : m_Project.BuildSceneList) {
					if (entry.Enabled && !sceneExists(entry.Name)) {
						Error(projectFile, "build scene '" + entry.Name + "' has no .scene file under Assets/");
					}
				}
				if (m_Project.GetEnabledBuildScenes().empty()) Warning(projectFile, "no build scenes enabled; builds will fail");
			}

			void CheckBuildProfiles() {
				const std::filesystem::path directory = Path::FromUtf8(m_Project.GetBuildProfilesDirectory());
				bool activeFound = m_Project.ActiveBuildProfileName.empty();
				for (const std::filesystem::path& file : FilesUnder(directory)) {
					if (file.extension() != IndexBuildProfile::FileExtension) continue;
					Json::Value root;
					std::string parseError;
					if (!Json::TryParse(File::ReadAllText(Path::ToUtf8(file)), root, &parseError) || !root.IsObject()) {
						Error(file, "not valid JSON: " + parseError);
						continue;
					}
					if (Path::ToUtf8(file.stem()) == m_Project.ActiveBuildProfileName) activeFound = true;
					const IndexBuildProfile profile = IndexBuildProfile::Load(Path::ToUtf8(file));
					if (const Json::Value* platform = root.FindMember("platform"); platform && platform->IsString()) {
						const std::string name = platform->AsStringOr();
						if (ToLower(IndexBuildProfile::PlatformToString(IndexBuildProfile::PlatformFromString(name))) != ToLower(name)
							&& IndexBuildProfile::PlatformFromString(name) == BuildPlatform::Windows) {
							Warning(file, "unknown platform '" + name + "' is read as Windows");
						}
					}
					if (!IndexBuildProfile::IsBackendAllowed(profile.Platform, profile.RenderBackend)) {
						Warning(file, std::string("render backend ") + IndexProject::RenderBackendToString(profile.RenderBackend)
							+ " is not allowed for " + IndexBuildProfile::PlatformToString(profile.Platform) + " and gets replaced");
					}
				}
				if (!activeFound) {
					Warning(Path::FromUtf8(m_Project.ProjectFilePath), "activeBuildProfile '" + m_Project.ActiveBuildProfileName
						+ "' has no BuildProfiles/*.build file");
				}
			}

			void IndexMetas() {
				const std::filesystem::path assets = Path::FromUtf8(m_Project.AssetsDirectory);
				const std::vector<std::filesystem::path> files = FilesUnder(assets);
				std::set<std::filesystem::path> assetFiles;
				for (const std::filesystem::path& file : files) {
					if (!IsMetaPath(file)) assetFiles.insert(file);
				}

				for (const std::filesystem::path& file : files) {
					if (!IsMetaPath(file)) {
						std::error_code error;
						std::filesystem::path meta = file;
						meta += ".meta";
						if (!std::filesystem::exists(meta, error)) {
							Warning(file, "no .meta: the engine assigns a random GUID on first scan (`index assets refresh`)");
							m_MissingMetas = true;
						}
						continue;
					}
					std::filesystem::path asset = file;
					asset.replace_extension();
					if (!assetFiles.count(asset)) {
						Warning(file, "orphan .meta (its asset is missing)");
						continue;
					}
					CheckMeta(file, asset);
				}
			}

			void CheckMeta(const std::filesystem::path& meta, const std::filesystem::path& asset) {
				Json::Value root;
				std::string parseError;
				if (!Json::TryParse(File::ReadAllText(Path::ToUtf8(meta)), root, &parseError) || !root.IsObject()) {
					Error(meta, "not valid JSON (" + parseError + "): the engine would replace it with a new GUID");
					m_MissingMetas = true;
					return;
				}
				const Json::Value* guidValue = root.FindMember("AssetGUID");
				if (!guidValue) guidValue = root.FindMember("uuid");
				const std::optional<uint64_t> guid = guidValue ? ParseGuid(*guidValue) : std::nullopt;
				if (!guid || *guid == 0) {
					Error(meta, "missing or invalid AssetGUID (decimal string): the engine would assign a new one");
					m_MissingMetas = true;
					return;
				}
				if (auto existing = m_GuidToPath.find(*guid); existing != m_GuidToPath.end()) {
					Error(meta, "GUID " + std::to_string(*guid) + " is also used by " + existing->second
						+ ": the engine re-rolls one of them and breaks its references");
					return;
				}
				if (AssetRegistry::IsBuiltIn(*guid)) {
					Error(meta, "GUID " + std::to_string(*guid) + " collides with a built-in IndexAssets file");
				}
				m_GuidToPath[*guid] = Path::ToGenericUtf8(asset);
				m_GuidToExtension[*guid] = ToLower(Path::ToUtf8(asset.extension()));

				if (const Json::Value* import = root.FindMember("import"); import && import->IsObject()) {
					if (const Json::Value* filter = import->FindMember("filter"); filter && filter->IsString()) {
						static const std::set<std::string> k_Filters{ "Point", "Bilinear", "Trilinear", "Anisotropic" };
						if (!k_Filters.count(filter->AsStringOr())) Warning(meta, "import.filter '" + filter->AsStringOr() + "' is ignored (Point, Bilinear, Trilinear, Anisotropic)");
					}
					for (const char* key : { "wrapU", "wrapV" }) {
						Wrap wrap{};
						if (const Json::Value* value = import->FindMember(key); value && value->IsString() && !TryParse(value->AsStringOr(), wrap)) {
							Warning(meta, std::string("import.") + key + " '" + value->AsStringOr() + "' is ignored (Repeat, Clamp, Mirror, Border)");
						}
					}
				}
				if (const Json::Value* sprites = root.FindMember("sprites"); sprites && sprites->IsArray()) {
					std::unordered_set<std::string> names;
					int index = 0;
					for (const Json::Value& sprite : sprites->GetArray()) {
						const std::string label = "sprites[" + std::to_string(index++) + "]";
						const Json::Value* name = sprite.IsObject() ? sprite.FindMember("name") : nullptr;
						if (!name || !name->IsString() || name->AsStringOr().empty()) {
							Warning(meta, label + " has no name and is skipped");
							continue;
						}
						if (!names.insert(name->AsStringOr()).second) Warning(meta, label + " duplicates slice name '" + name->AsStringOr() + "'");
						const Json::Value* rect = sprite.FindMember("rect");
						if (!rect || !rect->IsArray() || rect->GetArray().size() < 4
							|| rect->GetArray()[2].AsIntOr(0) <= 0 || rect->GetArray()[3].AsIntOr(0) <= 0) {
							Warning(meta, label + " ('" + name->AsStringOr() + "') needs rect [x,y,w,h] with w,h > 0 and is skipped");
						}
					}
				}
			}

			bool KnownGuid(uint64_t guid) const {
				return guid == k_DefaultFontAssetId || m_GuidToPath.count(guid) || AssetRegistry::IsBuiltIn(guid)
					|| (guid >> 56) == 0xAB;
			}

			// Any "*Asset" member holding a GUID, plus prefab references.
			void CheckReferences(const std::filesystem::path& file, const Json::Value& value, const std::string& where) {
				if (value.IsArray()) {
					for (const Json::Value& element : value.GetArray()) CheckReferences(file, element, where);
					return;
				}
				if (!value.IsObject()) return;
				for (const auto& [key, member] : value.GetObject()) {
					const bool assetKey = key.size() > 5 && key.compare(key.size() - 5, 5, "Asset") == 0;
					if ((assetKey || key == "PrefabGUID") && (member.IsString() || member.IsNumber())) {
						const std::optional<uint64_t> guid = ParseGuid(member);
						if (!guid) {
							if (!(member.IsString() && member.AsStringOr().empty())) Error(file, where + key + ": '" + member.AsStringOr() + "' is not a GUID");
						}
						else if (*guid != 0 && !KnownGuid(*guid)) {
							Error(file, where + key + ": GUID " + std::to_string(*guid) + " matches no .meta or built-in asset");
						}
						else if (key == "PrefabGUID" && *guid != 0 && m_GuidToExtension.count(*guid) && m_GuidToExtension.at(*guid) != ".prefab") {
							Error(file, where + "PrefabGUID " + std::to_string(*guid) + " is not a .prefab (" + m_GuidToPath.at(*guid) + ")");
						}
						else if ((key == "controllerAsset" || key == "clipAsset") && *guid != 0 && m_GuidToExtension.count(*guid)) {
							// "clipAsset" is an Animator's .anim and an AudioSource's sound.
							const std::string& extension = m_GuidToExtension.at(*guid);
							const bool valid = key == "controllerAsset"
								? extension == ".animctrl"
								: extension == ".anim" || extension == ".wav" || extension == ".mp3" || extension == ".flac";
							if (!valid) {
								const char* expected = key == "controllerAsset" ? ".animctrl" : ".anim or audio";
								Error(file, where + key + " " + std::to_string(*guid) + " is not a " + expected + " file (" + m_GuidToPath.at(*guid) + ")");
							}
						}
						continue;
					}
					CheckReferences(file, member, where);
				}
			}

			void CheckEntities(const std::filesystem::path& file, const std::vector<Json::Value>& entities, bool isPrefab) {
				std::unordered_map<uint64_t, std::string> uuids;
				for (std::size_t i = 0; i < entities.size(); ++i) {
					const Json::Value& entity = entities[i];
					std::string label = "entity[" + std::to_string(i) + "]";
					if (!entity.IsObject()) {
						Error(file, label + " is not an object; the whole file fails to load");
						continue;
					}
					if (const Json::Value* name = entity.FindMember("name"); name && name->IsString()) label += " '" + name->AsStringOr() + "'";
					const Json::Value* uuid = entity.FindMember("uuid");
					if (!uuid) {
						if (isPrefab && entities.size() > 1) Error(file, label + ": prefab entities need a unique non-zero uuid");
						else if (!isPrefab) Warning(file, label + ": no uuid; it gets a random id on every load and cannot be referenced");
						continue;
					}
					const std::optional<uint64_t> id = ParseGuid(*uuid);
					if (!id || *id == 0) {
						Error(file, label + ": invalid uuid (use a non-zero decimal string)");
						continue;
					}
					if (!uuids.emplace(*id, label).second) Error(file, label + ": duplicate uuid " + std::to_string(*id) + " (also " + uuids[*id] + "); the file fails to load");
				}
				for (std::size_t i = 0; i < entities.size(); ++i) {
					const Json::Value& entity = entities[i];
					if (!entity.IsObject()) continue;
					if (const Json::Value* parent = entity.FindMember("parentUuid")) {
						const std::optional<uint64_t> id = ParseGuid(*parent);
						if (!parent->IsString()) Error(file, "entity[" + std::to_string(i) + "]: parentUuid must be a decimal string");
						else if (!id || !uuids.count(*id)) Error(file, "entity[" + std::to_string(i) + "]: parentUuid " + parent->AsStringOr() + " matches no entity in this file");
					}
					CheckReferences(file, entity, "entity[" + std::to_string(i) + "].");
					CheckLocalizedText(file, entity, "entity[" + std::to_string(i) + "]");
				}
				CheckScriptFieldReferences(file, entities);
			}

			// Collects Localized Text keys for CheckLocalization, including prefab-instance overrides of the key.
			void CheckLocalizedText(const std::filesystem::path& file, const Json::Value& entity, const std::string& label) {
				if (const Json::Value* localized = entity.FindMember("LocalizedText"); localized && localized->IsObject()) {
					const Json::Value* key = localized->FindMember("key");
					const std::string keyText = key ? key->AsStringOr() : std::string{};
					if (keyText.empty()) Warning(file, label + ": Localized Text has no key; the text stays as authored");
					else m_LocalizedTextKeys.emplace(keyText, file);
					if (EntityHasComponent(entity, "TextRenderer", FindComponentInfo("TextRenderer")) == Presence::No) {
						Error(file, label + ": Localized Text needs a Text Renderer on the same entity; nothing shows the translation");
					}
				}
				if (const Json::Value* overrides = entity.FindMember("Overrides"); overrides && overrides->IsObject()) {
					if (const Json::Value* key = overrides->FindMember("LocalizedText.key"); key && !key->AsStringOr().empty()) {
						m_LocalizedTextKeys.emplace(key->AsStringOr(), file);
					}
				}
			}

			// Assets/Localization/<code>.json, the way GameLocalization reads them: unreadable files are skipped at runtime,
			// a key missing from the fallback language shows as the raw key, one missing elsewhere shows the fallback text.
			void CheckLocalization() {
				const std::filesystem::path directory = Path::FromUtf8(m_Project.AssetsDirectory) / "Localization";
				struct Language {
					std::filesystem::path File;
					std::set<std::string> Keys;
				};
				std::map<std::string, Language> languages;
				std::error_code fsError;
				if (std::filesystem::is_directory(directory, fsError)) {
					for (const std::filesystem::path& file : FilesUnder(directory)) {
						if (file.parent_path() != directory || ToLower(Path::ToUtf8(file.extension())) != ".json") continue;
						Json::Value root;
						std::string parseError;
						if (!Json::TryParse(File::ReadAllText(Path::ToUtf8(file)), root, &parseError) || !root.IsObject()) {
							Error(file, "not valid JSON: " + parseError + "; the language is skipped");
							continue;
						}
						const Json::Value* strings = root.FindMember("strings");
						if (!strings || !strings->IsObject()) {
							Error(file, "needs a \"strings\" object of key -> text; the language is skipped");
							continue;
						}
						std::string code = Path::ToUtf8(file.stem());
						if (const Json::Value* meta = root.FindMember("meta"); meta && meta->IsObject()) {
							if (const Json::Value* value = meta->FindMember("code"); value && !value->AsStringOr().empty()) code = value->AsStringOr();
							if (!meta->FindMember("displayName")) Warning(file, "meta.displayName is missing; language pickers show '" + code + "'");
						}
						else {
							Warning(file, "no \"meta\" block; the language is '" + code + "' (file name) and pickers show the code");
						}
						if (languages.contains(code)) {
							Error(file, "language '" + code + "' is already declared by " + Path::ToUtf8(languages[code].File.filename()) + "; this file is ignored");
							continue;
						}
						Language& language = languages[code];
						language.File = file;
						for (const auto& [key, value] : strings->GetObject()) {
							if (value.IsString()) language.Keys.insert(key);
							else Error(file, "strings." + key + " is not a string; it is ignored");
						}
					}
				}

				if (languages.empty()) {
					if (!m_LocalizedTextKeys.empty()) {
						const auto& [key, file] = *m_LocalizedTextKeys.begin();
						Error(file, "Localized Text uses key '" + key + "' but Assets/Localization has no language files; it shows the raw key");
					}
					return;
				}

				const std::string fallback = languages.contains("en") ? "en" : languages.begin()->first;
				const Language& reference = languages.at(fallback);
				for (const auto& [key, file] : m_LocalizedTextKeys) {
					if (!reference.Keys.contains(key)) {
						Error(file, "Localized Text key '" + key + "' is not in " + Path::ToUtf8(reference.File.filename())
							+ " (the fallback language); it shows the raw key");
					}
				}

				const auto listed = [](const std::vector<std::string>& keys) {
					std::string text;
					for (std::size_t i = 0; i < keys.size() && i < 5; ++i) text += (i ? ", " : "") + keys[i];
					if (keys.size() > 5) text += ", ...";
					return text;
				};
				for (const auto& [code, language] : languages) {
					if (code == fallback) continue;
					std::vector<std::string> missing;
					std::vector<std::string> extra;
					std::set_difference(reference.Keys.begin(), reference.Keys.end(), language.Keys.begin(), language.Keys.end(), std::back_inserter(missing));
					std::set_difference(language.Keys.begin(), language.Keys.end(), reference.Keys.begin(), reference.Keys.end(), std::back_inserter(extra));
					if (!missing.empty()) {
						Warning(language.File, std::to_string(missing.size()) + " key(s) of " + Path::ToUtf8(reference.File.filename())
							+ " untranslated (they show the " + fallback + " text): " + listed(missing));
					}
					if (!extra.empty()) {
						Warning(language.File, std::to_string(extra.size()) + " key(s) not in " + Path::ToUtf8(reference.File.filename())
							+ " (typo or leftover?): " + listed(extra));
					}
				}
			}

			// Entity refs (bare uuid, "type": "entity") and component refs ("<uuid>:<Component>") in ScriptFields;
			// a wrong uuid or component name otherwise only shows up as null at runtime.
			void CheckScriptFieldReferences(const std::filesystem::path& file, const std::vector<Json::Value>& entities) {
				std::unordered_map<uint64_t, const Json::Value*> byUuid;
				for (const Json::Value& entity : entities) {
					const Json::Value* uuid = entity.IsObject() ? entity.FindMember("uuid") : nullptr;
					if (const std::optional<uint64_t> id = uuid ? ParseGuid(*uuid) : std::nullopt; id && *id != 0) byUuid.emplace(*id, &entity);
				}
				for (std::size_t i = 0; i < entities.size(); ++i) {
					const Json::Value* scriptFields = entities[i].IsObject() ? entities[i].FindMember("ScriptFields") : nullptr;
					if (!scriptFields || !scriptFields->IsObject()) continue;
					for (const auto& [className, fields] : scriptFields->GetObject()) {
						if (!fields.IsArray()) continue;
						for (const Json::Value& field : fields.GetArray()) {
							const Json::Value* value = field.IsObject() ? field.FindMember("value") : nullptr;
							if (!value || !value->IsString()) continue;
							const Json::Value* name = field.FindMember("name");
							const Json::Value* type = field.FindMember("type");
							const std::string where = "entity[" + std::to_string(i) + "] ScriptFields." + std::string(className) + "."
								+ (name && name->IsString() ? name->AsStringOr() : std::string("?")) + ": ";
							CheckScriptFieldReference(file, where, type && type->IsString() ? type->AsStringOr() : std::string(),
								value->AsStringOr(), byUuid);
						}
					}
				}
			}

			void CheckScriptFieldReference(const std::filesystem::path& file, const std::string& where, const std::string& type,
				const std::string& text, const std::unordered_map<uint64_t, const Json::Value*>& byUuid) {
				if (text.empty() || text == "0" || text.rfind("prefab:", 0) == 0 || text.find('\n') != std::string::npos) return;
				const bool componentField = type.rfind("component:", 0) == 0;
				const std::size_t colon = text.find(':');
				const std::string idText = colon == std::string::npos ? text : text.substr(0, colon);
				const bool numericId = !idText.empty() && idText.find_first_not_of("0123456789") == std::string::npos;
				if (colon != std::string::npos ? !numericId : type != "entity") {
					if (componentField) Error(file, where + "'" + text + "' is not \"<entity uuid>:<Component>\"");
					return;
				}
				if (!numericId) {
					Error(file, where + "'" + text + "' is not an entity uuid");
					return;
				}
				const std::optional<uint64_t> id = ParseGuid(Json::Value(idText));
				if (!id || *id == 0) return;
				const auto target = byUuid.find(*id);
				if (target == byUuid.end()) {
					Error(file, where + "entity " + idText + " matches no entity in this file; the field loads as null");
					return;
				}
				if (colon == std::string::npos) return;

				const std::string component = text.substr(colon + 1);
				const ComponentInfo* info = FindComponentInfo(component);
				if (componentField) {
					const std::string declared = type.substr(std::string_view("component:").size());
					const ComponentInfo* declaredInfo = FindComponentInfo(declared);
					if (info && declaredInfo && info != declaredInfo) {
						Error(file, where + "points at '" + component + "' but the field is a " + declared + "; it loads as null");
						return;
					}
				}
				switch (EntityHasComponent(*target->second, component, info)) {
					case Presence::Yes:
					case Presence::Unknown:
						break;
					case Presence::No:
						if (info) Error(file, where + "entity " + idText + " has no " + component + "; the field loads as null");
						else Warning(file, where + "'" + component + "' is neither a built-in component nor a script on entity " + idText);
						break;
				}
			}

			enum class Presence { Yes, No, Unknown };

			// Accepts the display name ("Rect Transform 2D"), the serialized key ("RectTransform2D") and the C# class name
			// ("RectTransform", "NameComponent"), like the runtime does.
			static const ComponentInfo* FindComponentInfo(const std::string& name) {
				const auto normalize = [](std::string_view text) {
					std::string out;
					for (char c : text) {
						if (c != ' ') out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					}
					return out;
				};
				const auto strip = [](const std::string& text, std::string_view suffix) {
					return text.size() > suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0
						? text.substr(0, text.size() - suffix.size()) : text;
				};
				const std::string wanted = normalize(name);
				if (wanted.empty()) return nullptr;
				const ComponentInfo* found = nullptr;
				SceneManager::GetComponentRegistryOrBuiltIn().ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& info) {
					if (found) return;
					const std::string display = normalize(info.displayName);
					const std::string serialized = normalize(info.serializedName);
					if (wanted == display || wanted == serialized || strip(wanted, "component") == display
						|| (!serialized.empty() && wanted == strip(serialized, "2d"))) {
						found = &info;
					}
				});
				return found;
			}

			Presence EntityHasComponent(const Json::Value& entity, const std::string& component, const ComponentInfo* info) {
				if (info) {
					if (info->displayName == "Name") return Presence::Yes;
					std::string compact = info->displayName;
					compact.erase(std::remove(compact.begin(), compact.end(), ' '), compact.end());
					for (const std::string& key : { info->serializedName, info->displayName, compact }) {
						if (!key.empty() && entity.FindMember(key)) return Presence::Yes;
					}
				}
				else {
					const auto matchesClass = [&](const std::string& className) {
						return className == component
							|| (className.size() > component.size() && className.compare(className.size() - component.size(), component.size(), component) == 0
								&& className[className.size() - component.size() - 1] == '.');
					};
					for (const char* listKey : { "Scripts", "ManagedComponents" }) {
						const Json::Value* list = entity.FindMember(listKey);
						if (!list || !list->IsArray()) continue;
						for (const Json::Value& entry : list->GetArray()) {
							std::string className = entry.IsString() ? entry.AsStringOr() : std::string();
							if (entry.IsObject()) {
								const Json::Value* classValue = entry.FindMember("className");
								if (!classValue) classValue = entry.FindMember("class");
								if (classValue && classValue->IsString()) className = classValue->AsStringOr();
							}
							if (!className.empty() && matchesClass(className)) return Presence::Yes;
						}
					}
					if (entity.FindMember(component)) return Presence::Yes;
				}

				// A prefab instance stores only overrides; its components live in the prefab's root entity.
				const Json::Value* prefabGuid = entity.FindMember("PrefabGUID");
				if (!prefabGuid) return Presence::No;
				const std::optional<uint64_t> guid = ParseGuid(*prefabGuid);
				const Json::Value* prefabRoot = guid ? PrefabRootEntity(*guid) : nullptr;
				if (!prefabRoot) return Presence::Unknown;
				const Presence inPrefab = EntityHasComponent(*prefabRoot, component, info);
				if (inPrefab != Presence::No) return inPrefab;
				// An override path "<Component>.<field>" adds the component to the instance.
				const Json::Value* overrides = entity.FindMember("Overrides");
				if (!overrides) return Presence::No;
				if (!overrides->IsObject()) return Presence::Unknown;
				for (const auto& [path, value] : overrides->GetObject()) {
					const std::size_t dot = path.find('.');
					if (dot == std::string::npos) continue;
					Json::Value probe = Json::Value::MakeObject();
					probe.AddMember(std::string(path.substr(0, dot)), Json::Value::MakeObject());
					if (EntityHasComponent(probe, component, info) == Presence::Yes) return Presence::Yes;
				}
				return Presence::No;
			}

			const Json::Value* PrefabRootEntity(uint64_t guid) {
				if (auto cached = m_PrefabRoots.find(guid); cached != m_PrefabRoots.end()) {
					return cached->second.IsObject() ? &cached->second : nullptr;
				}
				Json::Value rootEntity;
				if (auto path = m_GuidToPath.find(guid); path != m_GuidToPath.end()) {
					Json::Value root;
					std::string readError;
					if (SceneSerializerStorage::ReadRootFromFile(path->second, root, &readError) && root.IsObject()) {
						if (const Json::Value* list = root.FindMember("Entities"); list && list->IsArray() && !list->GetArray().empty()) {
							rootEntity = list->GetArray().front();
						}
						else if (const Json::Value* single = root.FindMember("Entity"); single && single->IsObject()) {
							rootEntity = *single;
						}
					}
				}
				const Json::Value& stored = m_PrefabRoots.emplace(guid, std::move(rootEntity)).first->second;
				return stored.IsObject() ? &stored : nullptr;
			}

			// User shaders compile here as in the engine (headless device), and materials must point at one.
			void CheckShaders() {
				const std::filesystem::path assets = Path::FromUtf8(m_Project.AssetsDirectory);
				for (const std::filesystem::path& file : FilesUnder(assets)) {
					const std::string extension = ToLower(Path::ToUtf8(file.extension()));
					if (extension == ".wgsl") {
						std::vector<ShaderDiagnostic> diagnostics;
						CustomShaderLibrary::ValidateFile(Path::ToUtf8(file), diagnostics);
						for (const ShaderDiagnostic& diagnostic : diagnostics) {
							std::string message = diagnostic.Line > 0 ? "line " + std::to_string(diagnostic.Line) + ": " : std::string{};
							message += diagnostic.Message;
							Add(diagnostic.IsError, file, std::move(message));
						}
						continue;
					}
					if (extension != ".material") continue;
					Json::Value root;
					std::string parseError;
					if (!Json::TryParse(File::ReadAllText(Path::ToUtf8(file)), root, &parseError) || !root.IsObject()) {
						Error(file, "not valid JSON: " + parseError);
						continue;
					}
					const Json::Value* shader = root.FindMember("shader");
					if (!shader) continue;
					const std::optional<uint64_t> guid = ParseGuid(*shader);
					if (!guid) {
						Error(file, "shader: '" + shader->AsStringOr() + "' is not a GUID");
						continue;
					}
					if (*guid == 0) continue;
					const auto extensionIt = m_GuidToExtension.find(*guid);
					if (extensionIt == m_GuidToExtension.end()) {
						Error(file, "shader: GUID " + std::to_string(*guid) + " matches no .meta; the material draws with the fallback shader");
					}
					else if (extensionIt->second != ".wgsl") {
						Error(file, "shader: GUID " + std::to_string(*guid) + " is a " + extensionIt->second + " file, not a .wgsl shader");
					}
				}
			}

			// .anim tracks need a component and property; .animctrl states must point at clips, transitions at states and
			// conditions at parameters (the loader drops broken entries silently).
			void CheckAnimations() {
				const std::filesystem::path assets = Path::FromUtf8(m_Project.AssetsDirectory);
				for (const std::filesystem::path& file : FilesUnder(assets)) {
					const std::string extension = ToLower(Path::ToUtf8(file.extension()));
					if (extension != k_AnimationClipExtension && extension != k_AnimatorControllerExtension) continue;
					Json::Value root;
					std::string parseError;
					if (!Json::TryParse(File::ReadAllText(Path::ToUtf8(file)), root, &parseError) || !root.IsObject()) {
						Error(file, "not valid JSON: " + parseError);
						continue;
					}
					if (extension == k_AnimationClipExtension) {
						CheckClip(file, root);
					}
					else {
						CheckController(file, root);
					}
				}
			}

			void CheckClip(const std::filesystem::path& file, const Json::Value& root) {
				const Json::Value* tracks = root.FindMember("tracks");
				if (!tracks || !tracks->IsArray()) return;
				for (size_t i = 0; i < tracks->GetArray().size(); ++i) {
					const Json::Value& track = tracks->GetArray()[i];
					const std::string where = "tracks[" + std::to_string(i) + "]: ";
					const Json::Value* component = track.FindMember("component");
					const Json::Value* property = track.FindMember("property");
					if (!component || component->AsStringOr().empty() || !property || property->AsStringOr().empty()) {
						Error(file, where + "needs \"component\" and \"property\"; the track is skipped");
						continue;
					}
					if (!track.FindMember("keys") && !track.FindMember("discrete")) {
						Warning(file, where + component->AsStringOr() + "." + property->AsStringOr() + " has no \"keys\" or \"discrete\" keys");
					}
				}
				if (const Json::Value* events = root.FindMember("events"); events && events->IsArray()) {
					for (size_t i = 0; i < events->GetArray().size(); ++i) {
						const Json::Value* function = events->GetArray()[i].FindMember("function");
						if (!function || function->AsStringOr().empty()) Error(file, "events[" + std::to_string(i) + "]: needs a \"function\"");
					}
				}
			}

			void CheckController(const std::filesystem::path& file, const Json::Value& root) {
				std::set<std::string> states;
				std::set<std::string> parameters;
				if (const Json::Value* list = root.FindMember("parameters"); list && list->IsArray()) {
					for (const Json::Value& p : list->GetArray()) {
						if (const Json::Value* name = p.FindMember("name")) parameters.insert(name->AsStringOr());
					}
				}
				if (const Json::Value* list = root.FindMember("states"); list && list->IsArray()) {
					for (const Json::Value& state : list->GetArray()) {
						const Json::Value* name = state.FindMember("name");
						const std::string stateName = name ? name->AsStringOr() : std::string{};
						if (stateName.empty()) {
							Error(file, "a state has no \"name\"; it is skipped");
							continue;
						}
						if (!states.insert(stateName).second) Error(file, "state '" + stateName + "' appears twice; the second is skipped");
						const Json::Value* clip = state.FindMember("clip");
						if (!clip) continue;
						const std::optional<uint64_t> guid = ParseGuid(*clip);
						if (!guid) {
							Error(file, "state '" + stateName + "': clip '" + clip->AsStringOr() + "' is not a GUID");
						}
						else if (*guid != 0 && !m_GuidToExtension.count(*guid)) {
							Error(file, "state '" + stateName + "': clip GUID " + std::to_string(*guid) + " matches no .meta");
						}
						else if (*guid != 0 && m_GuidToExtension.at(*guid) != k_AnimationClipExtension) {
							Error(file, "state '" + stateName + "': clip GUID " + std::to_string(*guid) + " is not an .anim (" + m_GuidToPath.at(*guid) + ")");
						}
					}
				}
				if (const Json::Value* name = root.FindMember("defaultState"); name && !name->AsStringOr().empty() && !states.count(name->AsStringOr())) {
					Error(file, "defaultState '" + name->AsStringOr() + "' is not a state");
				}
				if (const Json::Value* list = root.FindMember("transitions"); list && list->IsArray()) {
					for (size_t i = 0; i < list->GetArray().size(); ++i) {
						const Json::Value& t = list->GetArray()[i];
						const std::string where = "transitions[" + std::to_string(i) + "]: ";
						const Json::Value* fromAny = t.FindMember("fromAny");
						if (!(fromAny && fromAny->AsBoolOr(false))) {
							const Json::Value* from = t.FindMember("from");
							if (!from || !states.count(from->AsStringOr())) Error(file, where + "from '" + (from ? from->AsStringOr() : std::string{}) + "' is not a state; the transition is skipped");
						}
						const Json::Value* to = t.FindMember("to");
						if (!to || !states.count(to->AsStringOr())) Error(file, where + "to '" + (to ? to->AsStringOr() : std::string{}) + "' is not a state; the transition is skipped");
						if (const Json::Value* conditions = t.FindMember("conditions"); conditions && conditions->IsArray()) {
							for (const Json::Value& c : conditions->GetArray()) {
								const Json::Value* parameter = c.FindMember("parameter");
								if (!parameter || !parameters.count(parameter->AsStringOr())) {
									Error(file, where + "condition on unknown parameter '" + (parameter ? parameter->AsStringOr() : std::string{}) + "' never holds");
								}
							}
						}
					}
				}
			}

			void CheckSerializedFiles() {
				const std::filesystem::path assets = Path::FromUtf8(m_Project.AssetsDirectory);
				std::vector<std::filesystem::path> scenes;
				std::vector<std::filesystem::path> prefabs;
				for (const std::filesystem::path& file : FilesUnder(assets)) {
					if (!GameBuilder::IsSerializedAssetPath(file)) continue;
					const std::string extension = ToLower(Path::ToUtf8(file.extension()));
					Json::Value root;
					std::string readError;
					if (!SceneSerializerStorage::ReadRootFromFile(Path::ToUtf8(file), root, &readError) || !root.IsObject()) {
						Error(file, "cannot be read: " + readError);
						continue;
					}
					if (extension == ".dataasset") {
						const Json::Value* type = root.FindMember("type");
						if (!type || !type->IsString() || type->AsStringOr().empty()) Error(file, "missing \"type\" (the C# DataAsset class)");
						if (const Json::Value* fields = root.FindMember("fields"); fields && fields->IsObject()) {
							for (const auto& [name, value] : fields->GetObject()) {
								if (!value.IsString()) Error(file, "fields." + name + " must be a string (other values load as empty)");
							}
						}
						continue;
					}
					if (extension == ".scene") {
						scenes.push_back(file);
						const Json::Value* entities = root.FindMember("entities");
						if (!entities || !entities->IsArray()) {
							Error(file, "missing \"entities\" array; the scene fails to load");
							continue;
						}
						CheckEntities(file, entities->GetArray(), false);
						if (const Json::Value* systems = root.FindMember("systems")) CheckReferences(file, *systems, "systems.");
					}
					else {
						prefabs.push_back(file);
						if (const Json::Value* entities = root.FindMember("Entities"); entities && entities->IsArray()) {
							CheckEntities(file, entities->GetArray(), true);
						}
						else if (const Json::Value* entity = root.FindMember("Entity"); entity && entity->IsObject()) {
							CheckEntities(file, { *entity }, true);
						}
						else if (!root.FindMember("PrefabRef")) {
							Error(file, "a prefab needs \"Entities\", \"Entity\" or \"PrefabRef\"");
						}
						else {
							CheckReferences(file, root, "");
						}
					}
				}

				if (m_MissingMetas) {
					Warning(assets, "skipped the engine load test: fix the .meta issues above first (it would rewrite them)");
					return;
				}
				// Last word goes to the engine itself: anything it rejects here fails in the editor and the runtime too.
				for (const std::filesystem::path& scene : scenes) {
					LoadedScene loaded;
					std::string loadError;
					if (!LoadScene(scene, loaded, loadError)) Error(scene, "rejected by the engine's scene loader");
				}
				for (const std::filesystem::path& prefab : prefabs) {
					auto scene = Scene::CreateDetachedScene("##Validate");
					if (SceneSerializer::LoadEntityFromFile(*scene, Path::ToUtf8(prefab)) == entt::null) {
						Error(prefab, "rejected by the engine's prefab loader");
					}
				}
			}

			IndexProject& m_Project;
			std::vector<Issue> m_Issues;
			std::unordered_map<uint64_t, std::string> m_GuidToPath;
			std::unordered_map<uint64_t, std::string> m_GuidToExtension;
			std::unordered_map<uint64_t, Json::Value> m_PrefabRoots;
			// Localized Text key -> first file using it.
			std::map<std::string, std::filesystem::path> m_LocalizedTextKeys;
			bool m_MissingMetas = false;
		};

		void RegisterBuiltInAssets() {
			const std::string root = Path::ResolveIndexAssets("");
			if (!root.empty()) AssetRegistry::RegisterBuiltInDirectory(root);
		}
	}

	int RunValidate(Args& args) {
		std::string error;
		IndexProject* project = OpenProject(args, error);
		const bool strict = args.Flag("strict");
		if (!args.Finish(error)) return Fail(error, k_ExitUsage);
		if (!project) return Fail(error);
		RegisterBuiltInAssets();

		const std::vector<Issue> issues = Validator(*project).Run();
		const auto errors = std::count_if(issues.begin(), issues.end(), [](const Issue& issue) { return issue.IsError; });
		const auto warnings = static_cast<std::ptrdiff_t>(issues.size()) - errors;

		if (Options().Json) {
			Json::Value root = Json::Value::MakeObject();
			Json::Value list = Json::Value::MakeArray();
			for (const Issue& issue : issues) {
				Json::Value value = Json::Value::MakeObject();
				value.AddMember("severity", std::string(issue.IsError ? "error" : "warning"));
				value.AddMember("file", issue.File);
				value.AddMember("message", issue.Message);
				list.Append(std::move(value));
			}
			root.AddMember("errors", static_cast<int>(errors));
			root.AddMember("warnings", static_cast<int>(warnings));
			root.AddMember("issues", std::move(list));
			Out(Json::Stringify(root, true));
		}
		else {
			for (const Issue& issue : issues) {
				Out(std::string(issue.IsError ? "error" : "warning") + ": " + issue.File + ": " + issue.Message);
			}
			Out(std::to_string(errors) + " error(s), " + std::to_string(warnings) + " warning(s)");
		}
		return errors > 0 || (strict && warnings > 0) ? k_ExitFailure : k_ExitOk;
	}

	// ── assets ──────────────────────────────────────────────────────────────

	int RunAssets(Args& args) {
		const std::optional<std::string> action = args.Positional();
		std::string error;
		IndexProject* project = OpenProject(args, error);
		const std::optional<std::string> kindFilter = args.Option("kind");
		const std::vector<std::string> values = args.RemainingPositionals();
		if (!args.Finish(error)) return Fail(error, k_ExitUsage);
		if (!project) return Fail(error);
		if (!action) return Fail("missing action: refresh, list or guid", k_ExitUsage);

		if (*action == "refresh") {
			std::size_t missingBefore = 0;
			for (const std::filesystem::path& file : FilesUnder(Path::FromUtf8(project->AssetsDirectory))) {
				std::filesystem::path meta = file;
				meta += ".meta";
				std::error_code fsError;
				if (!IsMetaPath(file) && !std::filesystem::exists(meta, fsError)) ++missingBefore;
			}
			AssetRegistry::MarkDirty();
			AssetRegistry::Sync();
			Out("Indexed the project's assets; wrote " + std::to_string(missingBefore) + " new .meta file(s)");
			return k_ExitOk;
		}

		if (*action == "guid") {
			if (values.empty()) return Fail("missing asset path", k_ExitUsage);
			for (const std::string& value : values) {
				const std::filesystem::path path = ResolveProjectPath(*project, value);
				const uint64_t guid = AssetRegistry::GetOrCreateAssetUUID(Path::ToUtf8(path));
				if (guid == 0) return Fail(Path::ToUtf8(path) + " is not an asset under Assets/");
				Out(std::to_string(guid));
			}
			return k_ExitOk;
		}

		if (*action == "list") {
			AssetRegistry::Sync();
			std::vector<AssetRegistry::Record> records;
			for (AssetKind kind : { AssetKind::Texture, AssetKind::Audio, AssetKind::Scene, AssetKind::Prefab, AssetKind::Script,
					AssetKind::Font, AssetKind::Shader, AssetKind::DataAsset, AssetKind::Material, AssetKind::Mesh, AssetKind::AnimationClip, AssetKind::AnimatorController, AssetKind::Other }) {
				std::vector<AssetRegistry::Record> ofKind = AssetRegistry::GetAssetsByKind(kind);
				records.insert(records.end(), ofKind.begin(), ofKind.end());
			}
			const std::filesystem::path root = Path::FromUtf8(project->RootDirectory);
			Json::Value list = Json::Value::MakeArray();
			std::string text;
			std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) { return a.Path < b.Path; });
			for (const AssetRegistry::Record& record : records) {
				const std::string kind = KindName(record.Kind);
				if (kindFilter && ToLower(*kindFilter) != kind) continue;
				std::error_code fsError;
				const std::string relative = Path::ToGenericUtf8(std::filesystem::relative(Path::FromUtf8(record.Path), root, fsError));
				if (Options().Json) {
					Json::Value value = Json::Value::MakeObject();
					value.AddMember("guid", std::to_string(record.Id));
					value.AddMember("kind", kind);
					value.AddMember("path", relative);
					list.Append(std::move(value));
				}
				else {
					text += std::to_string(record.Id) + "  " + kind + std::string(kind.size() < 10 ? 10 - kind.size() : 1, ' ') + relative + "\n";
				}
			}
			Out(Options().Json ? Json::Stringify(list, true) : text);
			return k_ExitOk;
		}
		return Fail("unknown action '" + *action + "' (refresh, list, guid)", k_ExitUsage);
	}

	// ── sprite ──────────────────────────────────────────────────────────────

	namespace {
		bool ParsePair(const std::string& value, char separator, int& outA, int& outB) {
			const std::size_t split = value.find(separator);
			try {
				if (split == std::string::npos) {
					outA = outB = std::stoi(value);
					return true;
				}
				outA = std::stoi(value.substr(0, split));
				outB = std::stoi(value.substr(split + 1));
				return true;
			}
			catch (...) {
				return false;
			}
		}

		std::string SlicesToText(const std::vector<SpriteSlice>& slices) {
			std::string text;
			for (const SpriteSlice& slice : slices) {
				text += slice.Name + "  [" + std::to_string(slice.X) + ", " + std::to_string(slice.Y) + ", "
					+ std::to_string(slice.W) + ", " + std::to_string(slice.H) + "]";
				if (!slice.Border.IsZero()) {
					text += "  border [" + std::to_string(slice.Border.Left) + ", " + std::to_string(slice.Border.Bottom) + ", "
						+ std::to_string(slice.Border.Right) + ", " + std::to_string(slice.Border.Top) + "]";
				}
				text += "\n";
			}
			return text;
		}

		std::string SlicesToJson(const std::vector<SpriteSlice>& slices) {
			Json::Value list = Json::Value::MakeArray();
			for (const SpriteSlice& slice : slices) {
				Json::Value value = Json::Value::MakeObject();
				value.AddMember("name", slice.Name);
				Json::Value rect = Json::Value::MakeArray();
				for (int component : { slice.X, slice.Y, slice.W, slice.H }) rect.Append(Json::Value(component));
				value.AddMember("rect", std::move(rect));
				Json::Value pivot = Json::Value::MakeArray();
				pivot.Append(Json::Value(static_cast<double>(slice.PivotX)));
				pivot.Append(Json::Value(static_cast<double>(slice.PivotY)));
				value.AddMember("pivot", std::move(pivot));
				if (!slice.Border.IsZero()) {
					Json::Value border = Json::Value::MakeArray();
					for (int edge : { slice.Border.Left, slice.Border.Bottom, slice.Border.Right, slice.Border.Top }) border.Append(Json::Value(edge));
					value.AddMember("border", std::move(border));
				}
				list.Append(std::move(value));
			}
			return Json::Stringify(list, true);
		}
	}

	int RunSprite(Args& args) {
		const std::optional<std::string> action = args.Positional();
		const std::optional<std::string> textureArgument = args.Positional();
		std::string error;
		IndexProject* project = OpenProject(args, error);
		const std::optional<std::string> grid = args.Option("grid");
		const std::optional<std::string> count = args.Option("count");
		const bool alpha = args.Flag("alpha");
		const std::optional<std::string> padding = args.Option("padding");
		const std::optional<std::string> offset = args.Option("offset");
		const std::optional<std::string> threshold = args.Option("threshold");
		const std::optional<std::string> minSize = args.Option("min-size");
		const std::optional<std::string> pivot = args.Option("pivot");
		const std::optional<std::string> stemOption = args.Option("name");
		const std::optional<std::string> borderOption = args.Option("border");
		const std::optional<std::string> sliceOption = args.Option("slice");
		const bool keep = args.Flag("keep");
		const bool dryRun = args.Flag("dry-run");
		if (!args.Finish(error)) return Fail(error, k_ExitUsage);
		if (!project) return Fail(error);
		if (!action || (*action != "slice" && *action != "list" && *action != "border")) {
			return Fail("usage: index sprite slice|list|border <texture>", k_ExitUsage);
		}
		if (!textureArgument) return Fail("missing texture path", k_ExitUsage);

		const std::filesystem::path texture = TexturePathArgument(*project, *textureArgument);
		std::error_code fsError;
		if (!std::filesystem::is_regular_file(texture, fsError)) return Fail("texture not found: " + Path::ToUtf8(texture));
		const std::string texturePath = Path::ToUtf8(texture);
		TextureMeta meta = AssetRegistry::ReadTextureMeta(texturePath);

		if (*action == "list") {
			Out(Options().Json ? SlicesToJson(meta.Sprites) : SlicesToText(meta.Sprites));
			return k_ExitOk;
		}

		if (*action == "border") {
			if (!borderOption) return Fail("--border L,B,R,T is required (texture pixels, 0 clears it)", k_ExitUsage);
			std::vector<int> values;
			try {
				std::size_t start = 0;
				while (true) {
					const std::size_t comma = borderOption->find(',', start);
					values.push_back(std::max(std::stoi(borderOption->substr(start, comma == std::string::npos ? std::string::npos : comma - start)), 0));
					if (comma == std::string::npos) break;
					start = comma + 1;
				}
			}
			catch (...) {
				values.clear();
			}
			if (values.size() == 1) values.assign(4, values[0]);
			if (values.size() != 4) return Fail("--border expects L,B,R,T or one value for all four", k_ExitUsage);
			const SpriteBorder border{ values[0], values[1], values[2], values[3] };
			if (sliceOption) {
				const int index = meta.IndexOfName(*sliceOption);
				if (index < 0) return Fail("no slice named '" + *sliceOption + "' in " + texturePath + ".meta");
				meta.Sprites[static_cast<std::size_t>(index)].Border = border;
			}
			else {
				meta.Border = border;
			}
			if (dryRun) return k_ExitOk;
			AssetRegistry::GetOrCreateAssetUUID(texturePath);
			if (!AssetRegistry::WriteTextureMeta(texturePath, meta)) return Fail("could not write " + texturePath + ".meta");
			Out("border of " + (sliceOption ? "'" + *sliceOption + "'" : std::string("the texture")) + " written to " + texturePath + ".meta");
			return k_ExitOk;
		}

		if (static_cast<int>(grid.has_value()) + static_cast<int>(count.has_value()) + static_cast<int>(alpha) != 1) {
			return Fail("choose exactly one of --grid WxH, --count CxR, --alpha", k_ExitUsage);
		}
		const std::unique_ptr<ImageData> image = ImageProcessing::LoadFile(texture);
		if (!image) return Fail("cannot decode " + texturePath);

		int padX = 0, padY = 0, offX = 0, offY = 0;
		if (padding && !ParsePair(*padding, ',', padX, padY)) return Fail("--padding expects X or X,Y", k_ExitUsage);
		if (offset && !ParsePair(*offset, ',', offX, offY)) return Fail("--offset expects X or X,Y", k_ExitUsage);
		float pivotX = 0.5f, pivotY = 0.5f;
		if (pivot) {
			const std::size_t comma = pivot->find(',');
			try {
				pivotX = std::stof(pivot->substr(0, comma));
				pivotY = comma == std::string::npos ? pivotX : std::stof(pivot->substr(comma + 1));
			}
			catch (...) {
				return Fail("--pivot expects X,Y (0..1)", k_ExitUsage);
			}
		}

		std::vector<SpriteSlice> slices = keep ? meta.Sprites : std::vector<SpriteSlice>{};
		const std::size_t before = slices.size();
		const std::string stem = stemOption.value_or(Path::ToUtf8(texture.stem()));
		if (grid) {
			int cellW = 0, cellH = 0;
			if (!ParsePair(*grid, 'x', cellW, cellH) || cellW < 1 || cellH < 1) return Fail("--grid expects WxH, e.g. 16x16", k_ExitUsage);
			SpriteSlicing::ByGridSize(slices, stem, image->Width, image->Height, cellW, cellH, padX, padY, offX, offY);
		}
		else if (count) {
			int columns = 0, rows = 0;
			if (!ParsePair(*count, 'x', columns, rows)) return Fail("--count expects CxR, e.g. 4x2", k_ExitUsage);
			if (SpriteSlicing::ByGridCount(slices, stem, image->Width, image->Height, columns, rows, padX, padY) < 0) {
				return Fail("a " + *count + " grid does not fit the " + std::to_string(image->Width) + "x" + std::to_string(image->Height) + " texture");
			}
		}
		else {
			int alphaThreshold = 1, minimum = 1;
			try {
				if (threshold) alphaThreshold = std::clamp(std::stoi(*threshold), 0, 255);
				if (minSize) minimum = std::max(1, std::stoi(*minSize));
			}
			catch (...) {
				return Fail("--threshold and --min-size expect integers", k_ExitUsage);
			}
			SpriteSlicing::ByAlpha(slices, stem, image->Pixels, image->Width, image->Height,
				static_cast<std::uint8_t>(alphaThreshold), minimum);
		}
		for (std::size_t i = before; i < slices.size(); ++i) {
			slices[i].PivotX = pivotX;
			slices[i].PivotY = pivotY;
		}

		if (dryRun) {
			Out(Options().Json ? SlicesToJson(slices) : SlicesToText(slices));
			return k_ExitOk;
		}
		// The registry only knows assets under Assets/; make sure the texture has its GUID before writing slices.
		AssetRegistry::GetOrCreateAssetUUID(texturePath);
		meta.Sprites = std::move(slices);
		if (!AssetRegistry::WriteTextureMeta(texturePath, meta)) return Fail("could not write " + texturePath + ".meta");
		if (Options().Json) Out(SlicesToJson(meta.Sprites));
		else Out(std::to_string(meta.Sprites.size() - (keep ? before : 0)) + " slice(s) written to " + texturePath + ".meta");
		return k_ExitOk;
	}

} // namespace Index::Cli
