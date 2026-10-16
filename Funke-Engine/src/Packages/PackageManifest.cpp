#include "pch.hpp"
#include "Packages/PackageManifest.hpp"

#include "Core/EngineModules.hpp"
#include "Core/Version.hpp"
#include "Serialization/Path.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

namespace Funke {
	namespace {
		// ── Minimal reader for Lua table literals ───────────────────────────
		// Manifests are data: strings, numbers, booleans and nested tables. Anything that would need
		// a Lua interpreter (calls, concatenation, variables) is rejected instead of guessed at.
		struct LuaTable;

		struct LuaValue {
			enum class Type : uint8_t { Nil, Bool, Number, String, Table };
			Type Kind = Type::Nil;
			bool Bool = false;
			double Number = 0.0;
			std::string String;
			std::shared_ptr<LuaTable> Table;
		};

		struct LuaTable {
			std::vector<LuaValue> Array;
			std::vector<std::pair<std::string, LuaValue>> Fields;

			const LuaValue* Find(std::string_view key) const {
				for (const auto& [name, value] : Fields) {
					if (name == key) return &value;
				}
				return nullptr;
			}
		};

		class LuaLiteralReader {
		public:
			explicit LuaLiteralReader(std::string_view source) : m_Source(source) {}

			bool ReadManifestRoot(LuaTable& outRoot, std::string& outError) {
				SkipTrivia();
				std::string localName;
				if (ConsumeKeyword("local")) {
					if (!ReadIdentifier(localName) || !Expect('=')) return Fail(outError, "expected `local <name> = { ... }`");
					LuaValue value;
					if (!ReadValue(value) || value.Kind != LuaValue::Type::Table) return Fail(outError, m_Error.empty() ? "expected a table" : m_Error);
					outRoot = *value.Table;
					SkipTrivia();
					std::string returned;
					if (!ConsumeKeyword("return") || !ReadIdentifier(returned) || returned != localName) {
						return Fail(outError, "expected `return " + localName + "` after the table");
					}
				}
				else {
					if (!ConsumeKeyword("return")) return Fail(outError, "manifest must start with `return {`");
					LuaValue value;
					if (!ReadValue(value) || value.Kind != LuaValue::Type::Table) return Fail(outError, m_Error.empty() ? "expected a table after `return`" : m_Error);
					outRoot = *value.Table;
				}
				SkipTrivia();
				if (m_Position < m_Source.size()) return Fail(outError, "unexpected text after the manifest table");
				return true;
			}

		private:
			bool Fail(std::string& outError, const std::string& message) {
				outError = "line " + std::to_string(Line()) + ": " + message;
				return false;
			}

			int Line() const {
				int line = 1;
				for (size_t i = 0; i < m_Position && i < m_Source.size(); ++i) {
					if (m_Source[i] == '\n') ++line;
				}
				return line;
			}

			char Peek(size_t offset = 0) const {
				return m_Position + offset < m_Source.size() ? m_Source[m_Position + offset] : '\0';
			}

			// Long bracket opener "[[", "[=[", ... ; returns the level or -1.
			int LongBracketLevel() const {
				if (Peek() != '[') return -1;
				size_t offset = 1;
				int level = 0;
				while (Peek(offset) == '=') { ++level; ++offset; }
				return Peek(offset) == '[' ? level : -1;
			}

			bool ReadLongBracket(int level, std::string* outText) {
				m_Position += static_cast<size_t>(level) + 2;
				if (Peek() == '\n') ++m_Position;
				const std::string closer = "]" + std::string(static_cast<size_t>(level), '=') + "]";
				const size_t end = m_Source.find(closer, m_Position);
				if (end == std::string_view::npos) {
					m_Error = "unterminated long bracket";
					return false;
				}
				if (outText) *outText = std::string(m_Source.substr(m_Position, end - m_Position));
				m_Position = end + closer.size();
				return true;
			}

			void SkipTrivia() {
				while (m_Position < m_Source.size()) {
					const char c = Peek();
					if (std::isspace(static_cast<unsigned char>(c))) {
						++m_Position;
					}
					else if (c == '-' && Peek(1) == '-') {
						m_Position += 2;
						const int level = LongBracketLevel();
						if (level >= 0) {
							if (!ReadLongBracket(level, nullptr)) return;
						}
						else {
							while (m_Position < m_Source.size() && Peek() != '\n') ++m_Position;
						}
					}
					else {
						break;
					}
				}
			}

			bool Expect(char c) {
				SkipTrivia();
				if (Peek() != c) return false;
				++m_Position;
				return true;
			}

			static bool IsIdentifierStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
			static bool IsIdentifierPart(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

			bool ReadIdentifier(std::string& out) {
				SkipTrivia();
				if (!IsIdentifierStart(Peek())) return false;
				const size_t start = m_Position;
				while (IsIdentifierPart(Peek())) ++m_Position;
				out = std::string(m_Source.substr(start, m_Position - start));
				return true;
			}

			bool ConsumeKeyword(std::string_view keyword) {
				SkipTrivia();
				if (m_Source.substr(m_Position, keyword.size()) != keyword || IsIdentifierPart(Peek(keyword.size()))) return false;
				m_Position += keyword.size();
				return true;
			}

			bool ReadQuotedString(std::string& out) {
				const char quote = Peek();
				++m_Position;
				out.clear();
				while (m_Position < m_Source.size()) {
					const char c = m_Source[m_Position++];
					if (c == quote) return true;
					if (c == '\n') break;
					if (c != '\\') {
						out.push_back(c);
						continue;
					}
					const char escaped = Peek();
					++m_Position;
					switch (escaped) {
					case 'n': out.push_back('\n'); break;
					case 't': out.push_back('\t'); break;
					case 'r': out.push_back('\r'); break;
					case '\\': out.push_back('\\'); break;
					case '"': out.push_back('"'); break;
					case '\'': out.push_back('\''); break;
					case '\n': out.push_back('\n'); break;
					default:
						m_Error = std::string("unsupported escape '\\") + escaped + "'";
						return false;
					}
				}
				m_Error = "unterminated string";
				return false;
			}

			bool ReadValue(LuaValue& out) {
				SkipTrivia();
				const char c = Peek();
				if (c == '"' || c == '\'') {
					out.Kind = LuaValue::Type::String;
					if (!ReadQuotedString(out.String)) return false;
				}
				else if (const int level = LongBracketLevel(); level >= 0) {
					out.Kind = LuaValue::Type::String;
					if (!ReadLongBracket(level, &out.String)) return false;
				}
				else if (c == '{') {
					out.Kind = LuaValue::Type::Table;
					out.Table = std::make_shared<LuaTable>();
					if (!ReadTable(*out.Table)) return false;
				}
				else if (std::isdigit(static_cast<unsigned char>(c)) || (c == '-' && std::isdigit(static_cast<unsigned char>(Peek(1))))) {
					const size_t start = m_Position;
					++m_Position;
					while (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '.') ++m_Position;
					try {
						out.Number = std::stod(std::string(m_Source.substr(start, m_Position - start)));
					}
					catch (...) {
						m_Error = "invalid number";
						return false;
					}
					out.Kind = LuaValue::Type::Number;
				}
				else if (ConsumeKeyword("true")) { out.Kind = LuaValue::Type::Bool; out.Bool = true; }
				else if (ConsumeKeyword("false")) { out.Kind = LuaValue::Type::Bool; out.Bool = false; }
				else if (ConsumeKeyword("nil")) { out.Kind = LuaValue::Type::Nil; }
				else {
					m_Error = "only literal values are supported (strings, numbers, booleans, tables)";
					return false;
				}

				SkipTrivia();
				if (Peek() == '.' && Peek(1) == '.') {
					m_Error = "string concatenation is not supported; write the full literal";
					return false;
				}
				if (Peek() == '(') {
					m_Error = "function calls are not supported in manifests";
					return false;
				}
				return true;
			}

			bool ReadTable(LuaTable& table) {
				++m_Position; // '{'
				while (true) {
					SkipTrivia();
					if (Peek() == '}') {
						++m_Position;
						return true;
					}
					if (m_Position >= m_Source.size()) {
						m_Error = "unterminated table";
						return false;
					}

					std::string key;
					bool keyed = false;
					if (Peek() == '[' && LongBracketLevel() < 0) {
						++m_Position;
						LuaValue keyValue;
						if (!ReadValue(keyValue) || keyValue.Kind != LuaValue::Type::String) {
							if (m_Error.empty()) m_Error = "only string keys are supported in [\"...\"] = form";
							return false;
						}
						if (!Expect(']') || !Expect('=')) {
							m_Error = "expected `] =` after table key";
							return false;
						}
						key = std::move(keyValue.String);
						keyed = true;
					}
					else if (IsIdentifierStart(Peek())) {
						const size_t save = m_Position;
						std::string identifier;
						ReadIdentifier(identifier);
						SkipTrivia();
						const bool isKeyword = identifier == "true" || identifier == "false" || identifier == "nil";
						if (!isKeyword && Peek() == '=' && Peek(1) != '=') {
							++m_Position;
							key = std::move(identifier);
							keyed = true;
						}
						else {
							m_Position = save;
						}
					}

					LuaValue value;
					if (!ReadValue(value)) return false;
					if (keyed) {
						if (table.Find(key)) {
							m_Error = "duplicate key '" + key + "'";
							return false;
						}
						table.Fields.emplace_back(std::move(key), std::move(value));
					}
					else {
						table.Array.push_back(std::move(value));
					}

					SkipTrivia();
					if (Peek() == ',' || Peek() == ';') {
						++m_Position;
					}
					else if (Peek() != '}') {
						m_Error = "expected ',' or '}' in table";
						return false;
					}
				}
			}

			std::string_view m_Source;
			size_t m_Position = 0;
			std::string m_Error;
		};

		const std::string* FindString(const LuaTable& table, std::string_view key) {
			const LuaValue* value = table.Find(key);
			return value && value->Kind == LuaValue::Type::String ? &value->String : nullptr;
		}

		bool ParseDependency(const LuaValue& value, const std::string* mapKey, PackageDependency& out, std::string& outError) {
			if (mapKey) {
				if (value.Kind != LuaValue::Type::String) {
					outError = "dependency '" + *mapKey + "' must map to a version range string";
					return false;
				}
				out.Name = *mapKey;
				out.Range = value.String.empty() ? "*" : value.String;
				return true;
			}
			if (value.Kind == LuaValue::Type::String) {
				const size_t at = value.String.find('@');
				out.Name = value.String.substr(0, at);
				out.Range = at == std::string::npos ? "*" : value.String.substr(at + 1);
				if (out.Range.empty()) out.Range = "*";
				return true;
			}
			if (value.Kind == LuaValue::Type::Table) {
				const std::string* name = FindString(*value.Table, "name");
				if (!name) {
					outError = "dependency tables need a `name` string";
					return false;
				}
				out.Name = *name;
				const std::string* range = FindString(*value.Table, "version");
				out.Range = range && !range->empty() ? *range : "*";
				return true;
			}
			outError = "dependencies must be names, \"Name@range\" strings or { name = ..., version = ... } tables";
			return false;
		}

		bool ReadFile(const std::filesystem::path& path, std::string& out) {
			std::ifstream stream(path, std::ios::binary);
			if (!stream.is_open()) return false;
			std::ostringstream buffer;
			buffer << stream.rdbuf();
			out = buffer.str();
			return true;
		}
	}

	bool PackageManifest::IsValidName(std::string_view name) {
		if (name.empty() || name.front() < 'A' || name.front() > 'Z' || name.back() == '.') return false;
		bool sawDot = false;
		bool segmentStart = true;
		for (char c : name) {
			if (c == '.') {
				if (segmentStart) return false;
				sawDot = true;
				segmentStart = true;
				continue;
			}
			if (!std::isalnum(static_cast<unsigned char>(c))) return false;
			if (segmentStart && (c < 'A' || c > 'Z')) return false;
			segmentStart = false;
		}
		return sawDot;
	}

	bool PackageManifest::TryParse(std::string_view luaSource, PackageManifest& outManifest, std::string* outError) {
		std::string error;
		auto fail = [&](const std::string& message) {
			if (outError) *outError = message;
			return false;
		};

		// Lua skips a UTF-8 BOM, and Windows editors write one.
		if (luaSource.substr(0, 3) == "\xEF\xBB\xBF") luaSource.remove_prefix(3);

		LuaTable root;
		LuaLiteralReader reader(luaSource);
		if (!reader.ReadManifestRoot(root, error)) return fail(error);

		PackageManifest manifest;
		const std::string* name = FindString(root, "name");
		if (!name || !IsValidName(*name)) {
			return fail("`name` must be PascalCase segments joined by dots, e.g. \"Funke.Tilemap2D\"");
		}
		manifest.Name = *name;

		const std::string* version = FindString(root, "version");
		if (!version || version->empty()) return fail("package '" + manifest.Name + "' has no `version` string");
		manifest.VersionText = *version;
		manifest.Version = SemanticVersion::Parse(*version);

		if (const std::string* description = FindString(root, "description")) manifest.Description = *description;
		if (const std::string* kind = FindString(root, "kind")) manifest.Kind = *kind;

		const std::string* engine = FindString(root, "engine");
		if (!engine) engine = FindString(root, "engineVersion");
		if (engine && !engine->empty()) {
			std::string rangeError;
			if (!VersionRange::Parse(*engine, &rangeError)) {
				return fail("package '" + manifest.Name + "' has an invalid `engine` range: " + rangeError);
			}
			manifest.EngineRange = *engine;
		}

		if (const LuaValue* dependencies = root.Find("dependencies"); dependencies && dependencies->Kind != LuaValue::Type::Nil) {
			if (dependencies->Kind != LuaValue::Type::Table) {
				return fail("package '" + manifest.Name + "': `dependencies` must be a table");
			}
			auto add = [&](const LuaValue& value, const std::string* key) {
				PackageDependency dependency;
				std::string dependencyError;
				if (!ParseDependency(value, key, dependency, dependencyError)) {
					error = dependencyError;
					return false;
				}
				if (!IsValidName(dependency.Name)) {
					error = "dependency name '" + dependency.Name + "' is not a valid package name";
					return false;
				}
				if (dependency.Name == manifest.Name) {
					error = "a package cannot depend on itself";
					return false;
				}
				std::string rangeError;
				if (!VersionRange::Parse(dependency.Range, &rangeError)) {
					error = "dependency '" + dependency.Name + "' has an invalid version range: " + rangeError;
					return false;
				}
				if (manifest.FindDependency(dependency.Name)) {
					error = "dependency '" + dependency.Name + "' is listed twice";
					return false;
				}
				manifest.Dependencies.push_back(std::move(dependency));
				return true;
			};
			for (const LuaValue& value : dependencies->Table->Array) {
				if (!add(value, nullptr)) return fail("package '" + manifest.Name + "': " + error);
			}
			for (const auto& [key, value] : dependencies->Table->Fields) {
				if (!add(value, &key)) return fail("package '" + manifest.Name + "': " + error);
			}
		}

		if (const LuaValue* modules = root.Find("modules"); modules && modules->Kind != LuaValue::Type::Nil) {
			if (modules->Kind != LuaValue::Type::Table || !modules->Table->Fields.empty()) {
				return fail("package '" + manifest.Name + "': `modules` must be a list such as { \"Funke.2D\" }");
			}
			for (const LuaValue& value : modules->Table->Array) {
				if (value.Kind != LuaValue::Type::String || !EngineModules::FromId(value.String)) {
					return fail("package '" + manifest.Name + "': unknown engine module '" + value.String
						+ "' in `modules` (known: Funke.2D, Funke.3D)");
				}
				if (std::find(manifest.Modules.begin(), manifest.Modules.end(), value.String) == manifest.Modules.end()) {
					manifest.Modules.push_back(value.String);
				}
			}
		}

		if (const LuaValue* layers = root.Find("layers"); layers && layers->Kind == LuaValue::Type::Table) {
			for (const auto& [layerName, layer] : layers->Table->Fields) {
				if (layerName == "native" || layerName == "engine_core") manifest.HasNativeLayer = true;
				else if (layerName == "native_standalone" || layerName == "standalone_cpp") manifest.HasNativeStandaloneLayer = true;
				else if (layerName == "csharp") {
					manifest.HasCSharpLayer = true;
					if (layer.Kind == LuaValue::Type::Table) {
						const LuaValue* unsafe = layer.Table->Find("allow_unsafe");
						manifest.CSharpAllowUnsafe = unsafe && unsafe->Kind == LuaValue::Type::Bool && unsafe->Bool;
					}
				}
			}
		}

		outManifest = std::move(manifest);
		return true;
	}

	bool PackageManifest::TryLoad(const std::string& packageDir, PackageManifest& outManifest, std::string* outError) {
		const std::filesystem::path manifestPath = Path::FromUtf8(packageDir) / std::string(k_FileName);
		std::string source;
		if (!ReadFile(manifestPath, source)) {
			if (outError) *outError = std::string(k_FileName) + " not found in " + packageDir;
			return false;
		}
		std::string error;
		if (!TryParse(source, outManifest, &error)) {
			if (outError) *outError = Path::ToUtf8(manifestPath) + ": " + error;
			return false;
		}
		outManifest.PackageDir = Path::ToGenericUtf8(Path::FromUtf8(packageDir));
		return true;
	}

	bool PackageManifest::IsCompatibleWithEngine(const SemanticVersion& engineVersion) const {
		if (EngineRange.empty()) return true;
		const auto range = VersionRange::Parse(EngineRange);
		return range && range->IsSatisfiedBy(engineVersion.WithoutPreRelease());
	}

	const PackageDependency* PackageManifest::FindDependency(std::string_view name) const {
		for (const PackageDependency& dependency : Dependencies) {
			if (dependency.Name == name) return &dependency;
		}
		return nullptr;
	}

	SemanticVersion GetEngineCompatibilityVersion() {
		const auto version = SemanticVersion::Parse(FNK_VERSION);
		return version ? version->WithoutPreRelease() : SemanticVersion{};
	}

}
