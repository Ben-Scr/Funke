#pragma once

#include "Project/ProjectManager.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptType.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Funke::EditorScriptDiscovery {

	struct ScriptEntry {
		std::string ClassName;
		std::filesystem::path Path;
		std::string Extension;
		ScriptType Type = ScriptType::Unknown;
		// Mutually exclusive: managed (: Component, via ScriptComponent) vs native-backed (: IComponent, via ComponentRegistry). Attach paths differ, so the popup must distinguish them.
		bool IsManagedComponent = false;
		bool IsNativeComponent = false;
		std::string NativeName;
		bool IsSceneSystem = false;
		bool IsGlobalSystem = false;
		// Assembly (Pkg.*) of a class that ships in a package; those have no Path.
		std::string Package;

		bool IsPackage() const { return !Package.empty(); }
	};

	inline std::string MenuLabel(const ScriptEntry& entry)
	{
		return entry.Extension.empty() ? entry.ClassName : entry.ClassName + "  " + entry.Extension;
	}

	// ImGui id that stays unique for entries without a file.
	inline std::string EntryId(const ScriptEntry& entry)
	{
		if (entry.Path.empty()) return entry.Package + ":" + entry.ClassName;
		return entry.Type == ScriptType::Native ? entry.Path.string() + ":" + entry.ClassName : entry.Path.string();
	}

	// "Funke.Networking.NetworkObject" -> "NetworkObject".
	inline std::string ShortClassName(const std::string& className)
	{
		const std::size_t dot = className.find_last_of('.');
		return dot == std::string::npos ? className : className.substr(dot + 1);
	}

	// "Pkg.Funke.Networking" -> "Funke.Networking".
	inline std::string PackageDisplayName(const std::string& assembly)
	{
		constexpr std::string_view prefix = "Pkg.";
		return assembly.rfind(prefix, 0) == 0 ? assembly.substr(prefix.size()) : assembly;
	}

	inline std::string ToLowerCopy(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		return value;
	}

	inline std::string TrimWhitespace(std::string value)
	{
		const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
			return std::isspace(ch) != 0;
		});
		const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
			return std::isspace(ch) != 0;
		}).base();

		if (first >= last) {
			return {};
		}

		return std::string(first, last);
	}

	inline bool IsNativeScriptBootstrapFile(const std::filesystem::path& path)
	{
		return path.filename().string() == "NativeScriptExports.cpp";
	}

	inline bool IsCSharpScriptExtension(const std::string& extension)
	{
		return ToLowerCopy(extension) == ".cs";
	}

	inline bool IsNativeScriptSourceExtension(const std::string& extension)
	{
		const std::string normalized = ToLowerCopy(extension);
		return normalized == ".cpp" || normalized == ".cc" || normalized == ".cxx";
	}

	inline bool IsValidRegisteredScriptToken(const std::string& token)
	{
		std::size_t offset = token.starts_with("::") ? 2 : 0;
		while (offset < token.size()) {
			const unsigned char first = static_cast<unsigned char>(token[offset]);
			if (std::isalpha(first) == 0 && first != '_') return false;
			while (++offset < token.size()) {
				const unsigned char ch = static_cast<unsigned char>(token[offset]);
				if (std::isalnum(ch) == 0 && ch != '_') break;
			}
			if (offset == token.size()) return true;
			if (token.compare(offset, 2, "::") != 0) return false;
			offset += 2;
		}
		return false;
	}

	inline std::string BuildNativeScriptSource(const std::string& className)
	{
		return "#include <Scripting/NativeScript.hpp>\n"
			"\n"
			"class " + className + " : public Funke::NativeScript {\n"
			"public:\n"
			"    void Start() override\n"
			"    {\n"
			"    }\n"
			"\n"
			"    void Update() override\n"
			"    {\n"
			"    }\n"
			"\n"
			"    void FixedUpdate() override\n"
			"    {\n"
			"    }\n"
			"\n"
			"    void OnDestroy() override\n"
			"    {\n"
			"    }\n"
			"};\n"
			"\n"
			"REGISTER_SCRIPT(" + className + ")\n";
	}

	inline std::string SanitizeIdentifier(const std::string& input, const std::string& fallback)
	{
		std::string out;
		out.reserve(input.size());
		for (char ch : input) {
			const unsigned char uch = static_cast<unsigned char>(ch);
			if (std::isalnum(uch) != 0 || ch == '_') {
				out.push_back(ch);
			}
		}
		if (out.empty()) {
			return fallback;
		}
		if (std::isdigit(static_cast<unsigned char>(out[0])) != 0) {
			out.insert(out.begin(), '_');
		}
		return out;
	}

	inline bool ContainsScriptEntry(
		const std::vector<ScriptEntry>& entries,
		const std::string& className,
		ScriptType type)
	{
		return std::any_of(entries.begin(), entries.end(), [&](const ScriptEntry& entry) {
			return entry.ClassName == className && entry.Type == type;
		});
	}

	inline void AppendScriptEntry(
		std::vector<ScriptEntry>& entries,
		const std::string& className,
		const std::filesystem::path& path,
		const std::string& extension,
		ScriptType type,
		bool isManagedComponent = false,
		bool isNativeComponent = false,
		std::string nativeName = {},
		bool isSceneSystem = false,
		bool isGlobalSystem = false)
	{
		if (className.empty() || type == ScriptType::Unknown || ContainsScriptEntry(entries, className, type)) {
			return;
		}

		ScriptEntry entry;
		entry.ClassName = className;
		entry.Path = path;
		entry.Extension = extension;
		entry.Type = type;
		entry.IsManagedComponent = isManagedComponent;
		entry.IsNativeComponent = isNativeComponent;
		entry.NativeName = std::move(nativeName);
		entry.IsSceneSystem = isSceneSystem;
		entry.IsGlobalSystem = isGlobalSystem;
		entries.push_back(std::move(entry));
	}

	inline std::string ReadSourceWithoutWhitespace(const std::filesystem::path& filePath)
	{
		std::ifstream input(filePath, std::ios::binary);
		if (!input) {
			return {};
		}

		const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		std::string compactSource;
		compactSource.reserve(source.size());
		for (char ch : source) {
			if (std::isspace(static_cast<unsigned char>(ch)) == 0) {
				compactSource.push_back(ch);
			}
		}

		return compactSource;
	}

	inline bool SourceHasBaseClass(const std::string& compactSource, std::string_view baseClassName)
	{
		const std::string shortPattern = ":" + std::string(baseClassName);
		const std::string qualifiedPattern = ":Funke." + std::string(baseClassName);
		const std::string globalPattern = ":global::Funke." + std::string(baseClassName);
		return compactSource.find(shortPattern) != std::string::npos
			|| compactSource.find(qualifiedPattern) != std::string::npos
			|| compactSource.find(globalPattern) != std::string::npos;
	}

	inline bool SourceLooksLikeManagedComponent(const std::filesystem::path& filePath)
	{
		const std::string compactSource = ReadSourceWithoutWhitespace(filePath);
		return compactSource.find(":Component") != std::string::npos
			|| compactSource.find(":Funke.Component") != std::string::npos
			|| compactSource.find(":global::Funke.Component") != std::string::npos
			|| compactSource.find(":IComponent") != std::string::npos
			|| compactSource.find(":Funke.Components.IComponent") != std::string::npos
			|| compactSource.find(":global::Funke.Components.IComponent") != std::string::npos;
	}

	// Must use raw source, not whitespace-stripped: interior spaces in attribute string literals would be collapsed and never match the C++ display name.
	inline std::string ExtractNativeComponentAttributeName(const std::filesystem::path& filePath)
	{
		std::ifstream input(filePath, std::ios::binary);
		if (!input) {
			return {};
		}

		const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		constexpr std::string_view marker = "NativeComponent";
		std::size_t pos = 0;
		while ((pos = source.find(marker, pos)) != std::string::npos) {
			// Look back past whitespace for '[' to confirm this is an attribute, not ':IComponent' or 'using' occurrences of the token.
			std::size_t bracket = pos;
			while (bracket > 0 && std::isspace(static_cast<unsigned char>(source[bracket - 1]))) {
				--bracket;
			}
			if (bracket == 0 || source[bracket - 1] != '[') {
				pos += marker.size();
				continue;
			}

			std::size_t open = source.find('(', pos + marker.size());
			if (open == std::string::npos) return {};
			std::size_t quoteOpen = source.find('"', open);
			if (quoteOpen == std::string::npos) return {};
			std::size_t quoteClose = source.find('"', quoteOpen + 1);
			if (quoteClose == std::string::npos) return {};
			return TrimWhitespace(source.substr(quoteOpen + 1, quoteClose - quoteOpen - 1));
		}
		return {};
	}

	inline std::string NativeSourceWithoutCommentsAndLiterals(const std::string& source)
	{
		std::string code = source;
		for (std::size_t i = 0; i < source.size();) {
			const std::size_t start = i;
			if (source[i] == '\'' && i > 0 && i + 1 < source.size()
				&& std::isxdigit(static_cast<unsigned char>(source[i - 1])) != 0
				&& std::isxdigit(static_cast<unsigned char>(source[i + 1])) != 0) {
				std::size_t numberStart = i - 1;
				while (numberStart > 0 && (std::isalnum(static_cast<unsigned char>(source[numberStart - 1])) != 0
					|| source[numberStart - 1] == '\'' || source[numberStart - 1] == '_')) --numberStart;
				if (std::isdigit(static_cast<unsigned char>(source[numberStart])) != 0) { ++i; continue; }
			}
			if (source.compare(i, 2, "//") == 0 || source[i] == '#') {
				i = source.find('\n', i);
				while (i != std::string::npos && i > start && (source[i - 1] == '\\'
					|| (i > start + 1 && source[i - 1] == '\r' && source[i - 2] == '\\'))) {
					i = source.find('\n', i + 1);
				}
				if (i == std::string::npos) i = source.size();
			}
			else if (source.compare(i, 2, "/*") == 0) {
				const std::size_t end = source.find("*/", i + 2);
				i = end == std::string::npos ? source.size() : end + 2;
			}
			else if (source.compare(i, 2, "R\"") == 0) {
				const std::size_t delimiterEnd = source.find('(', i + 2);
				if (delimiterEnd != std::string::npos && delimiterEnd - i <= 18) {
					const std::string terminator = ")" + source.substr(i + 2, delimiterEnd - i - 2) + "\"";
					const std::size_t end = source.find(terminator, delimiterEnd + 1);
					i = end == std::string::npos ? source.size() : end + terminator.size();
				}
				else {
					++i;
					continue;
				}
			}
			else if (source[i] == '"' || source[i] == '\'') {
				const char quote = source[i++];
				while (i < source.size()) {
					if (source[i] == '\\' && i + 1 < source.size()) { i += 2; continue; }
					if (source[i++] == quote) break;
				}
			}
			else {
				++i;
				continue;
			}
			for (std::size_t j = start; j < i; ++j) {
				if (code[j] != '\n') code[j] = ' ';
			}
		}
		return code;
	}

	inline std::vector<std::string> ExtractRegisteredNativeScriptClasses(const std::filesystem::path& filePath)
	{
		std::ifstream input(filePath, std::ios::binary);
		if (!input) {
			return {};
		}

		const std::string source = NativeSourceWithoutCommentsAndLiterals(
			std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>()));
		constexpr std::string_view token = "REGISTER_SCRIPT";
		const auto isIdentifierChar = [](char ch) {
			return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
		};

		std::vector<std::string> classes;
		std::size_t searchOffset = 0;
		while (true) {
			const std::size_t tokenPosition = source.find(token.data(), searchOffset, token.size());
			if (tokenPosition == std::string::npos) {
				break;
			}

			searchOffset = tokenPosition + token.size();
			if ((tokenPosition > 0 && isIdentifierChar(source[tokenPosition - 1]))
				|| (searchOffset < source.size() && isIdentifierChar(source[searchOffset]))) {
				continue;
			}
			std::size_t openParen = searchOffset;
			while (openParen < source.size() && std::isspace(static_cast<unsigned char>(source[openParen])) != 0) ++openParen;
			if (openParen == source.size() || source[openParen] != '(') {
				continue;
			}

			const std::size_t closeParen = source.find(')', openParen + 1);
			if (closeParen == std::string::npos) {
				break;
			}

			std::string className = TrimWhitespace(source.substr(openParen + 1, closeParen - openParen - 1));
			if (IsValidRegisteredScriptToken(className)
				&& std::find(classes.begin(), classes.end(), className) == classes.end()) {
				classes.push_back(std::move(className));
			}

			searchOffset = closeParen + 1;
		}

		return classes;
	}

	inline void CollectScriptFile(const std::filesystem::path& filePath, std::vector<ScriptEntry>& entries)
	{
		std::string extension = ToLowerCopy(filePath.extension().string());
		if (IsCSharpScriptExtension(extension)) {
			const std::string compactSource = ReadSourceWithoutWhitespace(filePath);
			// IComponent check must precede Component: ":Component" would otherwise fire on ":IComponent" too (substring match).
			const bool isNativeComponent = SourceHasBaseClass(compactSource, "IComponent")
				|| SourceHasBaseClass(compactSource, "Components.IComponent");
			const bool isManagedComponent = !isNativeComponent
				&& (SourceHasBaseClass(compactSource, "Component")
					|| SourceHasBaseClass(compactSource, "Funke.Component"));
			std::string nativeName;
			if (isNativeComponent) {
				nativeName = ExtractNativeComponentAttributeName(filePath);
				if (nativeName.empty()) {
					nativeName = filePath.stem().string();
				}
			}
			const bool isSceneSystem = SourceHasBaseClass(compactSource, "SceneSystem");
			const bool isGlobalSystem = SourceHasBaseClass(compactSource, "GlobalSystem");
			AppendScriptEntry(entries, filePath.stem().string(), filePath, extension, ScriptType::Managed,
				isManagedComponent, isNativeComponent, std::move(nativeName),
				isSceneSystem, isGlobalSystem);
			return;
		}

		if (!IsNativeScriptSourceExtension(extension) || IsNativeScriptBootstrapFile(filePath)) {
			return;
		}

		for (const std::string& className : ExtractRegisteredNativeScriptClasses(filePath)) {
			AppendScriptEntry(entries, className, filePath, extension, ScriptType::Native);
		}
	}

	inline void CollectScriptFiles(const std::filesystem::path& directory, std::vector<ScriptEntry>& entries)
	{
		std::error_code ec;
		if (directory.empty() || !std::filesystem::exists(directory, ec) || ec) {
			return;
		}

		for (std::filesystem::recursive_directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied, ec), end;
			 it != end;
			 it.increment(ec)) {
			if (ec) {
				ec.clear();
				continue;
			}

			if (!it->is_regular_file(ec) || ec) {
				ec.clear();
				continue;
			}

			CollectScriptFile(it->path(), entries);
		}
	}

	inline void SortScriptEntries(std::vector<ScriptEntry>& entries)
	{
		std::sort(entries.begin(), entries.end(), [](const ScriptEntry& a, const ScriptEntry& b) {
			if (a.ClassName != b.ClassName) {
				return a.ClassName < b.ClassName;
			}
			if (a.Type != b.Type) {
				return a.Type < b.Type;
			}
			return a.Path.string() < b.Path.string();
		});
	}

	// Parses the managed GetPackageScriptTypes JSON; names the project scan already found are skipped.
	inline void AppendPackageScriptEntries(const std::string& json, std::vector<ScriptEntry>& entries)
	{
		Json::Value root;
		if (!Json::TryParse(json, root) || !root.IsArray()) {
			return;
		}

		for (const Json::Value& item : root.GetArray()) {
			const Json::Value* name = item.IsObject() ? item.FindMember("name") : nullptr;
			const Json::Value* kind = item.IsObject() ? item.FindMember("kind") : nullptr;
			const Json::Value* assembly = item.IsObject() ? item.FindMember("assembly") : nullptr;
			if (!name || !name->IsString() || !kind || !kind->IsString() || !assembly || !assembly->IsString()) {
				continue;
			}

			ScriptEntry entry;
			entry.ClassName = name->AsStringOr();
			entry.Package = assembly->AsStringOr();
			entry.Type = ScriptType::Managed;
			const std::string kindName = kind->AsStringOr();
			entry.IsManagedComponent = kindName == "component";
			entry.IsSceneSystem = kindName == "sceneSystem";
			entry.IsGlobalSystem = kindName == "globalSystem";
			const bool knownKind = kindName == "script" || entry.IsManagedComponent || entry.IsSceneSystem || entry.IsGlobalSystem;
			if (!knownKind || entry.ClassName.empty() || entry.Package.empty()
				|| ContainsScriptEntry(entries, entry.ClassName, ScriptType::Managed)) {
				continue;
			}
			entries.push_back(std::move(entry));
		}
	}

	// 1-second debounce cache for the directory walk (called once per frame from multiple panels). MarkDirty() forces an immediate rebuild on file mutations.
	namespace detail {
		inline std::vector<ScriptEntry> s_Cache;
		inline std::atomic<bool> s_Dirty{ true };
		inline std::chrono::steady_clock::time_point s_LastScan{};
		inline std::mutex s_CacheMutex;
	}

	inline void MarkDirty()
	{
		detail::s_Dirty.store(true, std::memory_order_release);
	}

	inline void CollectProjectScriptEntries(std::vector<ScriptEntry>& entries)
	{
		using namespace std::chrono;
		const auto now = steady_clock::now();

		{
			std::scoped_lock lock(detail::s_CacheMutex);
			const bool dirty = detail::s_Dirty.load(std::memory_order_acquire);
			const auto sinceLast = duration_cast<milliseconds>(now - detail::s_LastScan);
			if (!dirty && sinceLast < milliseconds(1000)) {
				entries.insert(entries.end(), detail::s_Cache.begin(), detail::s_Cache.end());
				return;
			}
		}

		// Rebuild outside the lock — directory iteration is the slow part
		// and we don't want to block other readers while it runs. The
		// cache write is short and re-takes the lock.
		std::vector<ScriptEntry> rebuilt;
		if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
			CollectScriptFiles(std::filesystem::path(project->AssetsDirectory), rebuilt);
			CollectScriptFiles(std::filesystem::path(project->NativeSourceDir), rebuilt);
		}
		else {
			const std::filesystem::path base = std::filesystem::path(Path::ExecutableDir());
			CollectScriptFiles(base / ".." / ".." / ".." / "Funke-Sandbox" / "Source", rebuilt);
			CollectScriptFiles(base / ".." / ".." / ".." / "Funke-NativeScripts" / "Source", rebuilt);
		}
		// Package sources aren't in the project, so the loaded assemblies are asked instead.
		AppendPackageScriptEntries(ScriptEngine::GetPackageScriptTypes(), rebuilt);
		SortScriptEntries(rebuilt);

		{
			std::scoped_lock lock(detail::s_CacheMutex);
			detail::s_Cache = rebuilt;
			detail::s_LastScan = now;
			detail::s_Dirty.store(false, std::memory_order_release);
		}

		entries.insert(entries.end(), rebuilt.begin(), rebuilt.end());
	}

} // namespace Funke::EditorScriptDiscovery
