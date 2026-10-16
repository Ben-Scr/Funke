#include "pch.hpp"
#include "BuildAssetPruning.hpp"

#include "Assets/AssetRegistry.hpp"
#include "Graphics/Text/FontHandle.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializerStorage.hpp"

#include <algorithm>
#include <charconv>
#include <deque>
#include <fstream>
#include <iterator>
#include <unordered_map>

namespace Index::BuildAssetPruning {

	namespace {
		constexpr std::uintmax_t k_MaxScannedFileSize = 256ull * 1024 * 1024;
		constexpr std::string_view k_BuiltInPrefix = "indexassets/";

		std::string ToLower(std::string s) {
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		std::string RelKey(const std::filesystem::path& relative) {
			std::string key = Path::ToUtf8(relative);
			std::replace(key.begin(), key.end(), '\\', '/');
			return ToLower(std::move(key));
		}

		std::string_view ExtensionOf(std::string_view key) {
			const size_t slash = key.find_last_of('/');
			const size_t dot = key.find_last_of('.');
			if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return {};
			return key.substr(dot);
		}

		std::string_view StemOf(std::string_view key) {
			const size_t slash = key.find_last_of('/');
			std::string_view name = slash == std::string_view::npos ? key : key.substr(slash + 1);
			const size_t dot = name.find_last_of('.');
			return dot == std::string_view::npos ? name : name.substr(0, dot);
		}

		bool IsSerializedExtension(std::string_view ext) {
			return ext == ".scene" || ext == ".prefab" || ext == ".dataasset";
		}

		// Formats that cannot hold a reference to another asset.
		bool IsOpaqueExtension(std::string_view ext) {
			static const std::unordered_set<std::string_view> k_Opaque{
				".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tga", ".psd", ".ico", ".webp",
				".wav", ".mp3", ".ogg", ".opus", ".flac", ".mp4", ".webm", ".mov",
				".ttf", ".otf", ".ttc", ".dll", ".exe", ".pdb", ".so", ".dylib", ".zip", ".cs",
				// Binary glTF embeds its buffers; .bin holds a .gltf's geometry.
				".glb", ".bin",
			};
			return k_Opaque.contains(ext);
		}

		bool ReadFile(const std::filesystem::path& path, std::string& out) {
			std::error_code ec;
			const std::uintmax_t size = std::filesystem::file_size(path, ec);
			if (ec || size > k_MaxScannedFileSize) return false;
			std::ifstream in(path, std::ios::binary);
			if (!in) return false;
			out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			return true;
		}

		uint64_t ReadMetaGuid(const std::filesystem::path& metaPath) {
			std::string text;
			if (!ReadFile(metaPath, text)) return 0;
			Json::Value root;
			if (!Json::TryParse(text, root) || !root.IsObject()) return 0;
			for (const char* key : { "AssetGUID", "uuid" }) {
				const Json::Value* value = root.FindMember(key);
				if (!value) continue;
				if (value->IsNumber()) return value->AsUInt64Or(0);
				if (value->IsString()) {
					const std::string s = value->AsStringOr();
					uint64_t guid = 0;
					if (std::from_chars(s.data(), s.data() + s.size(), guid).ec == std::errc{}) return guid;
				}
			}
			return 0;
		}

		// Paths in content are written many ways: "Assets/X.png", "index:Fonts/Y.ttf", "Maps\\Official\\".
		std::string NormalizeReference(std::string_view raw) {
			while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) raw.remove_prefix(1);
			while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.remove_suffix(1);
			std::string s;
			s.reserve(raw.size());
			for (char c : raw) {
				if (c == '\\') c = '/';
				if (c == '/' && !s.empty() && s.back() == '/') continue;
				s.push_back(c);
			}
			s = ToLower(std::move(s));
			while (s.starts_with("./")) s.erase(0, 2);
			while (s.starts_with("/")) s.erase(0, 1);
			if (s.starts_with("index:")) s = std::string(k_BuiltInPrefix) + s.substr(6);
			if (s.starts_with("assets/")) s.erase(0, 7);
			while (!s.empty() && s.back() == '/') s.pop_back();
			return s;
		}

		// Calls onLiteral for every string literal (interpolated ones split at their holes); comments
		// and char literals are skipped so an apostrophe in "// don't" cannot derail the scan.
		template<typename F>
		void ForEachCSharpLiteral(std::string_view src, F&& onLiteral) {
			const size_t n = src.size();
			size_t i = 0;
			while (i < n) {
				const char c = src[i];
				if (c == '/' && i + 1 < n && src[i + 1] == '/') {
					const size_t eol = src.find('\n', i);
					i = eol == std::string_view::npos ? n : eol;
					continue;
				}
				if (c == '/' && i + 1 < n && src[i + 1] == '*') {
					const size_t end = src.find("*/", i + 2);
					i = end == std::string_view::npos ? n : end + 2;
					continue;
				}
				if (c == '\'') {
					i += (i + 1 < n && src[i + 1] == '\\') ? 3 : 2;
					while (i < n && src[i] != '\'' && src[i] != '\n') ++i;
					++i;
					continue;
				}

				bool verbatim = false;
				bool interpolated = false;
				size_t j = i;
				while (j < n && (src[j] == '@' || src[j] == '$')) {
					(src[j] == '@' ? verbatim : interpolated) = true;
					++j;
				}
				if (j >= n || src[j] != '"') {
					i = j > i ? j : i + 1;
					continue;
				}

				size_t quotes = 0;
				while (j + quotes < n && src[j + quotes] == '"') ++quotes;
				if (quotes >= 3) {
					const std::string delimiter(quotes, '"');
					const size_t end = src.find(delimiter, j + quotes);
					const size_t stop = end == std::string_view::npos ? n : end;
					onLiteral(src.substr(j + quotes, stop - (j + quotes)));
					i = stop + quotes;
					continue;
				}
				if (quotes == 2 && !(verbatim && j + 2 < n && src[j + 2] == '"')) {
					i = j + 2;
					continue;
				}

				std::string current;
				int holeDepth = 0;
				size_t k = j + 1;
				while (k < n) {
					const char ch = src[k];
					if (holeDepth > 0) {
						if (ch == '{') ++holeDepth;
						else if (ch == '}') --holeDepth;
						++k;
						continue;
					}
					if (verbatim) {
						if (ch == '"') {
							if (k + 1 < n && src[k + 1] == '"') { current.push_back('"'); k += 2; continue; }
							break;
						}
					}
					else {
						if (ch == '\\' && k + 1 < n) {
							const char e = src[k + 1];
							current.push_back(e == 'n' ? '\n' : e == 't' ? '\t' : e);
							k += 2;
							continue;
						}
						if (ch == '"' || ch == '\n') break;
					}
					if (interpolated && (ch == '{' || ch == '}')) {
						if (k + 1 < n && src[k + 1] == ch) { current.push_back(ch); k += 2; continue; }
						if (ch == '{') {
							onLiteral(current);
							current.clear();
							holeDepth = 1;
						}
						++k;
						continue;
					}
					current.push_back(ch);
					++k;
				}
				onLiteral(current);
				i = k + 1;
			}
		}

		// Extensions AssetManager.FindAll<T> enumerates for T (see AssetManager.KindForType).
		std::vector<std::string_view> ExtensionsForManagedType(std::string_view typeName) {
			if (const size_t dot = typeName.find_last_of('.'); dot != std::string_view::npos) typeName.remove_prefix(dot + 1);
			if (typeName == "Texture") return { ".png", ".jpg", ".jpeg", ".bmp", ".tga" };
			if (typeName == "Audio") return { ".wav", ".mp3", ".ogg", ".flac" };
			if (typeName == "Entity") return { ".prefab" };
			if (typeName == "Font") return { ".ttf", ".otf" };
			if (typeName == "Material") return { ".material" };
			if (typeName == "Mesh") return { ".obj", ".gltf", ".glb" };
			if (typeName == "AnimationClip") return { ".anim" };
			if (typeName == "AnimatorController") return { ".animctrl" };
			return { ".dataasset" };
		}

		class Reachability {
		public:
			enum class Domain : uint8_t { Project, BuiltIn };

			bool IndexFiles(const Inputs& inputs, std::string& error) {
				std::vector<std::pair<std::string, uint64_t>> metaGuids;
				if (!Enumerate(inputs.AssetsRoot, Domain::Project, &metaGuids, error)) return false;
				if (!inputs.IndexAssetsRoot.empty() && std::filesystem::exists(inputs.IndexAssetsRoot)
					&& !Enumerate(inputs.IndexAssetsRoot, Domain::BuiltIn, nullptr, error)) return false;

				for (const auto& [assetKey, guid] : metaGuids) {
					if (const auto it = m_ByExactKey.find(assetKey); it != m_ByExactKey.end() && guid != 0) {
						m_ByGuid[guid].push_back(it->second);
					}
				}
				if (const auto it = m_ByExactKey.find(std::string(k_BuiltInPrefix) + "fonts/googlesans-regular.ttf");
					it != m_ByExactKey.end()) {
					m_ByGuid[k_DefaultFontAssetId].push_back(it->second);
				}
				m_Kept.assign(m_Files.size(), false);
				return true;
			}

			void KeepKey(std::string_view key) {
				if (const auto it = m_ByExactKey.find(std::string(key)); it != m_ByExactKey.end()) Keep(it->second);
			}

			void KeepPrefix(std::string_view prefix, const std::vector<std::string_view>& extensions = {}) {
				for (size_t i = 0; i < m_Files.size(); ++i) {
					if (!m_Files[i].Key.starts_with(prefix)) continue;
					if (!extensions.empty()
						&& std::find(extensions.begin(), extensions.end(), ExtensionOf(m_Files[i].Key)) == extensions.end()) continue;
					Keep(i);
				}
			}

			void KeepWhere(bool (*predicate)(std::string_view key)) {
				for (size_t i = 0; i < m_Files.size(); ++i) {
					if (predicate(m_Files[i].Key)) Keep(i);
				}
			}

			bool KeepScene(std::string_view name) {
				const auto it = m_ScenesByName.find(ToLower(std::string(name)));
				if (it == m_ScenesByName.end()) return false;
				for (size_t index : it->second) Keep(index);
				return true;
			}

			void MatchGuidsIn(std::string_view text) {
				size_t i = 0;
				while (i < text.size()) {
					if (!std::isdigit(static_cast<unsigned char>(text[i]))) { ++i; continue; }
					size_t end = i;
					while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) ++end;
					if (end - i <= 20) {
						uint64_t value = 0;
						if (std::from_chars(text.data() + i, text.data() + end, value).ec == std::errc{}) MatchGuid(value);
					}
					i = end;
				}
			}

			void MatchGuid(uint64_t guid) {
				if (guid == 0) return;
				if (const auto it = m_ByGuid.find(guid); it != m_ByGuid.end()) {
					for (size_t index : it->second) Keep(index);
				}
			}

			// Bare single words ("Audio") only count as folders where code names them; in serialized
			// data they are far more often entity names.
			void MatchReference(std::string_view raw, bool singleWordFolders) {
				if (raw.size() > 1024) return;
				const std::string key = NormalizeReference(raw);
				if (key.empty()) return;
				if (const auto it = m_ByPath.find(key); it != m_ByPath.end()) {
					for (size_t index : it->second) Keep(index);
				}
				const bool singleWord = key.find('/') == std::string::npos;
				if (singleWord) KeepScene(key);
				if (m_Directories.contains(key) && (singleWordFolders || !singleWord)) KeepPrefix(key + "/");
			}

			void ScanSerialized(const Json::Value& value) { Walk(value); }

			void ScanCSharp(std::string_view source) {
				MatchGuidsIn(source);
				ForEachCSharpLiteral(source, [this](std::string_view literal) { MatchReference(literal, true); });
				ScanFindAllCalls(source);
			}

			bool Drain(std::string& error) {
				while (!m_Pending.empty()) {
					const size_t index = m_Pending.front();
					m_Pending.pop_front();
					const File& file = m_Files[index];
					if (file.Where != Domain::Project) continue;
					const std::string_view ext = ExtensionOf(file.Key);
					if (IsOpaqueExtension(ext)) continue;

					if (IsSerializedExtension(ext)) {
						Json::Value root;
						if (SceneSerializerStorage::ReadRootFromFile(Path::ToUtf8(file.Absolute), root)) {
							Walk(root);
							continue;
						}
					}
					std::string text;
					if (!ReadFile(file.Absolute, text)) {
						error = "could not scan " + Path::ToUtf8(file.Absolute);
						return false;
					}
					ScanText(text);
					// A .gltf names its buffers and images relative to itself.
					if (ext == ".gltf") ScanRelativeUris(text, file.Key);
				}
				return true;
			}

			void Collect(Plan& plan) const {
				for (size_t i = 0; i < m_Files.size(); ++i) {
					if (!m_Kept[i]) continue;
					if (m_Files[i].Where == Domain::Project) plan.ProjectFiles.insert(m_Files[i].Key);
					else plan.BuiltInFiles.insert(m_Files[i].Key.substr(k_BuiltInPrefix.size()));
				}
			}

		private:
			struct File {
				Domain Where;
				std::string Key;
				std::filesystem::path Absolute;
			};

			bool Enumerate(const std::filesystem::path& root, Domain where,
				std::vector<std::pair<std::string, uint64_t>>* metaGuids, std::string& error)
			{
				const std::string prefix = where == Domain::BuiltIn ? std::string(k_BuiltInPrefix) : std::string();
				std::error_code ec;
				for (std::filesystem::recursive_directory_iterator it(root,
						std::filesystem::directory_options::skip_permission_denied, ec), end;
					it != end; it.increment(ec))
				{
					if (ec) break;
					std::error_code entryEc;
					const std::string rel = RelKey(it->path().lexically_relative(root));
					if (it->is_directory(entryEc)) {
						if (where == Domain::BuiltIn && rel == "textures/editor") it.disable_recursion_pending();
						else m_Directories.insert(prefix + rel);
						continue;
					}
					if (!it->is_regular_file(entryEc)) continue;
					if (ExtensionOf(rel) == ".meta") {
						if (metaGuids) metaGuids->emplace_back(rel.substr(0, rel.size() - 5), ReadMetaGuid(it->path()));
						continue;
					}

					const size_t index = m_Files.size();
					File file{ where, prefix + rel, it->path() };
					m_ByExactKey[file.Key] = index;
					m_ByPath[file.Key].push_back(index);
					if (const std::string_view ext = ExtensionOf(file.Key); !ext.empty()) {
						m_ByPath[file.Key.substr(0, file.Key.size() - ext.size())].push_back(index);
					}
					if (where == Domain::Project && ExtensionOf(rel) == ".scene") {
						m_ScenesByName[std::string(StemOf(rel))].push_back(index);
					}
					if (where == Domain::BuiltIn) m_ByGuid[AssetRegistry::ComputeBuiltInPathGuid(rel)].push_back(index);
					m_Files.push_back(std::move(file));
				}
				if (ec) {
					error = "could not enumerate " + Path::ToUtf8(root) + ": " + ec.message();
					return false;
				}
				return true;
			}

			void Keep(size_t index) {
				if (m_Kept[index]) return;
				m_Kept[index] = true;
				m_Pending.push_back(index);
			}

			void Walk(const Json::Value& value) {
				switch (value.GetType()) {
				case Json::Value::Type::String: {
					const std::string s = value.AsStringOr();
					MatchGuidsIn(s);
					MatchReference(s, false);
					break;
				}
				case Json::Value::Type::Number:
					if (value.GetNumberKind() != Json::Value::NumberKind::Double) MatchGuid(value.AsUInt64Or(0));
					break;
				case Json::Value::Type::Object:
					for (const auto& [key, member] : value.GetObject()) Walk(member);
					break;
				case Json::Value::Type::Array:
					for (const Json::Value& item : value.GetArray()) Walk(item);
					break;
				default:
					break;
				}
			}

			// Loose data files (level JSON, CSV, ...) may name assets by GUID or quoted path.
			void ScanRelativeUris(std::string_view text, std::string_view key) {
				const size_t slash = key.find_last_of('/');
				const std::string directory = slash == std::string_view::npos ? std::string{} : std::string(key.substr(0, slash + 1));
				for (size_t i = text.find("\"uri\""); i != std::string_view::npos; i = text.find("\"uri\"", i + 5)) {
					const size_t open = text.find('"', text.find(':', i + 5));
					if (open == std::string_view::npos) break;
					const size_t close = text.find('"', open + 1);
					if (close == std::string_view::npos) break;
					const std::string_view uri = text.substr(open + 1, close - open - 1);
					if (!uri.starts_with("data:")) MatchReference(directory + std::string(uri), false);
				}
			}

			void ScanText(std::string_view text) {
				MatchGuidsIn(text);
				for (size_t i = text.find('"'); i != std::string_view::npos; ) {
					std::string current;
					size_t k = i + 1;
					for (; k < text.size() && text[k] != '"' && text[k] != '\n'; ++k) {
						if (text[k] == '\\' && k + 1 < text.size()) ++k;
						current.push_back(text[k]);
					}
					MatchReference(current, false);
					i = k < text.size() ? text.find('"', k + 1) : std::string_view::npos;
				}
			}

			// FindAll<T>("prefix") enumerates by folder; without a literal prefix every asset of T may load.
			void ScanFindAllCalls(std::string_view source) {
				constexpr std::string_view k_Token = "FindAll";
				auto skipSpace = [&](size_t& p) {
					while (p < source.size() && std::isspace(static_cast<unsigned char>(source[p]))) ++p;
				};
				for (size_t pos = source.find(k_Token); pos != std::string_view::npos; pos = source.find(k_Token, pos + 1)) {
					size_t p = pos + k_Token.size();
					skipSpace(p);
					if (p >= source.size() || source[p] != '<') continue;
					const size_t close = source.find('>', p);
					if (close == std::string_view::npos) continue;
					std::string_view typeName = source.substr(p + 1, close - p - 1);
					while (!typeName.empty() && std::isspace(static_cast<unsigned char>(typeName.front()))) typeName.remove_prefix(1);
					while (!typeName.empty() && std::isspace(static_cast<unsigned char>(typeName.back()))) typeName.remove_suffix(1);
					const std::vector<std::string_view> extensions = ExtensionsForManagedType(typeName);

					p = close + 1;
					skipSpace(p);
					if (p >= source.size() || source[p] != '(') continue;
					++p;
					skipSpace(p);
					std::string prefix;
					if (p < source.size() && source[p] == '"') {
						const size_t endQuote = source.find('"', p + 1);
						size_t after = endQuote == std::string_view::npos ? source.size() : endQuote + 1;
						skipSpace(after);
						if (endQuote != std::string_view::npos && after < source.size() && source[after] == ')') {
							prefix = NormalizeReference(source.substr(p + 1, endQuote - p - 1));
						}
					}
					if (prefix.empty()) {
						KeepPrefix("", extensions);
					}
					else {
						KeepPrefix(prefix, extensions);
						KeepPrefix(std::string(k_BuiltInPrefix) + prefix, extensions);
					}
				}
			}

			std::vector<File> m_Files;
			std::vector<bool> m_Kept;
			std::deque<size_t> m_Pending;
			std::unordered_map<std::string, size_t> m_ByExactKey;
			std::unordered_map<std::string, std::vector<size_t>> m_ByPath;
			std::unordered_map<std::string, std::vector<size_t>> m_ScenesByName;
			std::unordered_map<uint64_t, std::vector<size_t>> m_ByGuid;
			std::unordered_set<std::string> m_Directories;
		};

		bool IsSkippedSourceDirectory(const std::filesystem::path& dir, const std::filesystem::path& projectRoot,
			const std::vector<std::filesystem::path>& skipped)
		{
			static const std::unordered_set<std::string> k_SkippedNames{
				"bin", "obj", ".index", ".git", ".vs", "indexassets",
			};
			const std::string name = ToLower(Path::ToUtf8(dir.filename()));
			if (k_SkippedNames.contains(name)) return true;
			// Default build output root; old builds hold stale script copies.
			if (name == "builds" && dir.parent_path() == projectRoot) return true;
			for (const auto& s : skipped) {
				std::error_code ec;
				if (!s.empty() && std::filesystem::equivalent(dir, s, ec)) return true;
			}
			return false;
		}

		bool IsLicenseNotice(std::string_view key) {
			if (!key.starts_with(k_BuiltInPrefix)) return false;
			const std::string_view ext = ExtensionOf(key);
			return ext == ".md" || ext == ".txt";
		}
	}

	bool Plan::KeepsProjectFile(const std::filesystem::path& relative) const {
		std::string key = RelKey(relative);
		if (key.ends_with(".meta")) key.resize(key.size() - 5);
		return ProjectFiles.contains(key);
	}

	bool Plan::KeepsBuiltInFile(const std::filesystem::path& relative) const {
		return BuiltInFiles.contains(RelKey(relative));
	}

	Plan ComputeShippedAssets(const Inputs& inputs) {
		Plan plan;
		Reachability reach;
		if (!reach.IndexFiles(inputs, plan.Error)) return plan;

		// Loaded by fixed path, never by GUID (FontManager, ImGuiFonts, TextureManager, splash, Localization).
		const std::string builtIn(k_BuiltInPrefix);
		reach.KeepKey(builtIn + "fonts/googlesans-regular.ttf");
		reach.KeepPrefix(builtIn + "fonts/notosanscjk/");
		for (const char* name : { "square", "pixel", "circle", "capsule", "isometricdiamond",
			"hexagonflattop", "hexagonpointedtop", "9sliced", "invisible" }) {
			reach.KeepKey(builtIn + "textures/default/" + name + ".png");
		}
		for (const char* logo : { "icon.png", "index64.png" }) reach.KeepKey(builtIn + "textures/" + logo);
		reach.KeepPrefix(builtIn + "localization/");
		// The game's string tables: GameLocalization scans the folder, nothing references them by GUID or path.
		reach.KeepPrefix("localization/");
		reach.KeepWhere(&IsLicenseNotice);
		reach.KeepPrefix(ToLower(std::string(k_StreamingAssetsFolder)) + "/");

		for (const std::string& scene : inputs.BuildScenes) {
			if (!reach.KeepScene(scene)) {
				plan.Error = "build scene '" + scene + "' was not found under Assets";
				return plan;
			}
		}

		{
			std::string text;
			Json::Value root;
			if (!ReadFile(inputs.ProjectRoot / "project.json", text) || !Json::TryParse(text, root) || !root.IsObject()) {
				plan.Error = "project.json could not be read";
				return plan;
			}
			// Build scenes are the enabled subset above; the app icon is staged separately.
			for (const auto& [key, value] : root.GetObject()) {
				if (key != "buildScenes" && key != "appIcon") reach.ScanSerialized(value);
			}
		}

		std::error_code ec;
		for (std::filesystem::recursive_directory_iterator it(inputs.ProjectRoot,
				std::filesystem::directory_options::skip_permission_denied, ec), end;
			it != end; it.increment(ec))
		{
			if (ec) break;
			std::error_code entryEc;
			if (it->is_directory(entryEc)) {
				if (IsSkippedSourceDirectory(it->path(), inputs.ProjectRoot, inputs.SkippedDirectories)) it.disable_recursion_pending();
				continue;
			}
			if (!it->is_regular_file(entryEc) || ToLower(Path::ToUtf8(it->path().extension())) != ".cs") continue;
			std::string source;
			if (!ReadFile(it->path(), source)) {
				plan.Error = "could not scan " + Path::ToUtf8(it->path());
				return plan;
			}
			reach.ScanCSharp(source);
		}
		if (ec) {
			plan.Error = "could not enumerate scripts: " + ec.message();
			return plan;
		}

		if (!reach.Drain(plan.Error)) return plan;
		reach.Collect(plan);
		plan.Valid = true;
		return plan;
	}

	std::string FilterRegistryManifest(std::string_view manifest, const Plan& plan) {
		if (!plan.Valid) return std::string(manifest);
		std::string out;
		out.reserve(manifest.size());
		bool header = true;
		size_t pos = 0;
		while (pos < manifest.size()) {
			size_t eol = manifest.find('\n', pos);
			if (eol == std::string_view::npos) eol = manifest.size();
			const std::string_view line = manifest.substr(pos, eol - pos);
			pos = eol + 1;

			bool keep = header;
			header = false;
			if (!keep) {
				size_t cursor = 0;
				int tabs = 0;
				for (; tabs < 4; ++tabs) {
					const size_t tab = line.find('\t', cursor);
					if (tab == std::string_view::npos) break;
					cursor = tab + 1;
				}
				// Malformed lines are skipped by the runtime anyway.
				keep = tabs < 4 || plan.ProjectFiles.contains(ToLower(std::string(line.substr(cursor))));
			}
			if (keep) {
				out.append(line);
				out.push_back('\n');
			}
		}
		return out;
	}

}
