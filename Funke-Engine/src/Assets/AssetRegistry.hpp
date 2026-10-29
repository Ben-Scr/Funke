#pragma once

#include "Assets/AssetKind.hpp"
#include "Assets/TextureMeta.hpp"
#include "Core/Export.hpp"
#include "Core/UUID.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <magic_enum/magic_enum.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Funke {

	class AssetRegistry {
	public:
		struct Record {
			uint64_t Id = 0;
			std::string Path;
			AssetKind Kind = AssetKind::Unknown;
		};

		static constexpr std::string_view MetaExtension = ".meta";
		static constexpr std::string_view CorruptMetaExtension = ".meta.corrupt";

		static bool IsMetaFilePath(std::string_view path) {
			const std::string normalized = ToLowerCopy(std::string(path));
			// The .meta.corrupt backups LoadMetaJson leaves behind are not assets either.
			for (const std::string_view extension : { MetaExtension, CorruptMetaExtension }) {
				if (normalized.size() >= extension.size()
					&& normalized.compare(normalized.size() - extension.size(), extension.size(), extension) == 0) {
					return true;
				}
			}
			return false;
		}

		static void MarkDirty() {
			State().Dirty = true;
			++State().ChangeVersion;
			// Clear the known-missing cache: a FileWatcher event (or any
			// other dirtying signal) means the on-disk layout might have
			// changed and previously-missing assets could now exist.
			State().KnownMissingIds.clear();
		}

		static bool IsDirty() {
			return State().Dirty;
		}

		static uint64_t GetChangeVersion() {
			return State().ChangeVersion;
		}

		// Top-byte convention: 0xAB = hand-picked stable GUID (safe in defaults), 0xAA = path-hash GUID (changes on rename/move within FunkeAssets/).
		static void RegisterBuiltInAsset(const std::string& absolutePath, uint64_t guid, AssetKind kind) {
			if (absolutePath.empty() || guid == 0) {
				return;
			}
			const std::string normalizedPath = NormalizePath(absolutePath);
			Record record;
			record.Id = guid;
			record.Path = normalizedPath;
			record.Kind = kind;
			State().BuiltInById[guid] = record;
			State().BuiltInPathToId[normalizedPath] = guid;
		}

		// FNV-1a 64 of the path relative to FunkeAssets root, lowercased + forward-slashed for cross-platform stability.
		static uint64_t ComputeBuiltInPathGuid(std::string relativeGenericPath) {
			std::transform(relativeGenericPath.begin(), relativeGenericPath.end(), relativeGenericPath.begin(),
				[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			uint64_t hash = 0xcbf29ce484222325ULL;
			for (char ch : relativeGenericPath) {
				hash ^= static_cast<uint8_t>(ch);
				hash *= 0x100000001b3ULL;
			}
			return (hash & 0x00FFFFFFFFFFFFFFULL) | 0xAA00000000000000ULL;
		}

		// Call AFTER RegisterBuiltInAsset so hand-picked GUIDs win over path-hash auto-registration.
		static void RegisterBuiltInDirectory(const std::string& absoluteRoot) {
			if (absoluteRoot.empty()) {
				return;
			}
			const std::filesystem::path nativeRoot = Path::FromUtf8(absoluteRoot);
			std::error_code rootEc;
			if (!std::filesystem::is_directory(nativeRoot, rootEc) || rootEc) {
				return;
			}

			std::error_code ec;
			for (std::filesystem::recursive_directory_iterator it(
				 nativeRoot,
				 std::filesystem::directory_options::skip_permission_denied,
				 ec), end;
				 it != end;
				 it.increment(ec))
			{
				if (ec) { ec.clear(); continue; }
				if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }

				const std::string absPath = NormalizePath(Path::ToUtf8(it->path()));
				if (absPath.empty() || IsMetaFilePath(absPath)) {
					continue;
				}
				if (State().BuiltInPathToId.find(absPath) != State().BuiltInPathToId.end()) {
					continue;  // hand-picked or earlier scan already owns this path
				}

				const AssetKind kind = Classify(absPath);
				if (kind == AssetKind::Unknown) {
					continue;
				}

				std::error_code relEc;
				std::filesystem::path relPath = std::filesystem::relative(
					Path::FromUtf8(absPath), nativeRoot, relEc);
				const uint64_t guid = ComputeBuiltInPathGuid(relEc ? absPath : Path::ToGenericUtf8(relPath));

				if (State().BuiltInById.find(guid) != State().BuiltInById.end()) {
					FNK_CORE_WARN_TAG("AssetRegistry",
						"Built-in path-hash collision: '{}' hashed to GUID {} which is already registered — skipping",
						absPath, guid);
					continue;
				}

				Record record;
				record.Id = guid;
				record.Path = absPath;
				record.Kind = kind;
				State().BuiltInById[guid] = record;
				State().BuiltInPathToId[absPath] = guid;
			}
		}

		static void Sync() {
			EnsureUpToDate(true);
		}

		// Standalone runtimes set this true: their Assets/ never changes on disk, so the registry
		// trusts a shipped manifest (skips the directory walk) and never re-scans on a resolve miss.
		// Editors leave it false so on-disk edits are detected. Set BEFORE the first Sync.
		static void SetAssetsImmutable(bool immutable) {
			State().AssetsImmutable = immutable;
		}

		static uint64_t GetOrCreateAssetUUID(const std::string& path) {
			const std::string normalizedPath = NormalizePath(path);
			if (normalizedPath.empty() || IsMetaFilePath(normalizedPath)) {
				return 0;
			}

			// Built-in (engine-shipped) assets bypass the project-tracked
			// path check — they live under <exeDir>/FunkeAssets/ and have
			// pre-assigned stable GUIDs.
			if (auto it = State().BuiltInPathToId.find(normalizedPath); it != State().BuiltInPathToId.end()) {
				return it->second;
			}

			EnsureUpToDate();
			if (!IsTrackedAsset(normalizedPath)) {
				return 0;
			}

			if (const auto it = State().PathToId.find(normalizedPath); it != State().PathToId.end()) {
				return it->second;
			}

			uint64_t id = ReadMetaId(normalizedPath);
			if (id == 0) {
				do {
					id = static_cast<uint64_t>(UUID());
				} while (id != 0 && State().IdToRecord.find(id) != State().IdToRecord.end());
			} else if (auto existing = State().IdToRecord.find(id); existing != State().IdToRecord.end()) {
				// GUID collision: a different asset already owns this GUID.
				// Regenerate so we don't break the existing reference, but
				// surface the collision — silent overwrite breaks references
				// in scene/prefab files that point at the original asset.
				FNK_CORE_WARN_TAG("AssetRegistry",
					"GUID collision while indexing '{}': id {} already owned by '{}' — regenerating new GUID for '{}'",
					normalizedPath, id, existing->second.Path, normalizedPath);
				do {
					id = static_cast<uint64_t>(UUID());
				} while (id != 0 && State().IdToRecord.find(id) != State().IdToRecord.end());
			}

			WriteMeta(normalizedPath, id, Classify(normalizedPath));
			Register(normalizedPath, id, Classify(normalizedPath));
			State().KnownMissingIds.erase(id);
			++State().ChangeVersion;
			return id;
		}

		// Registers one known project asset without forcing a full tree rebuild.
		// Editor importers use this after they have already walked/copied the
		// imported files, so UUID/meta creation can be spread across frames.
		static uint64_t ImportAssetPathIncremental(const std::string& path) {
			const std::string normalizedPath = NormalizePath(path);
			if (normalizedPath.empty() || IsMetaFilePath(normalizedPath)) {
				return 0;
			}

			if (auto it = State().BuiltInPathToId.find(normalizedPath); it != State().BuiltInPathToId.end()) {
				return it->second;
			}

			const std::string root = GetAssetsRoot();
			if (root != State().TrackedRoot) {
				State().TrackedRoot = root;
				State().IdToRecord.clear();
				State().PathToId.clear();
				State().KnownMissingIds.clear();
				State().Dirty = false;
			}

			if (!IsTrackedAsset(normalizedPath)) {
				return 0;
			}

			if (const auto it = State().PathToId.find(normalizedPath); it != State().PathToId.end()) {
				return it->second;
			}

			uint64_t id = ReadMetaId(normalizedPath);
			if (id == 0) {
				do {
					id = static_cast<uint64_t>(UUID());
				} while (id != 0 && State().IdToRecord.find(id) != State().IdToRecord.end());
			}
			else if (auto existing = State().IdToRecord.find(id);
				existing != State().IdToRecord.end() && existing->second.Path != normalizedPath) {
				FNK_CORE_WARN_TAG("AssetRegistry",
					"GUID collision while indexing '{}': id {} already owned by '{}' - regenerating new GUID for '{}'",
					normalizedPath, id, existing->second.Path, normalizedPath);
				do {
					id = static_cast<uint64_t>(UUID());
				} while (id != 0 && State().IdToRecord.find(id) != State().IdToRecord.end());
			}

			const AssetKind kind = Classify(normalizedPath);
			WriteMeta(normalizedPath, id, kind);
			Register(normalizedPath, id, kind);
			State().KnownMissingIds.erase(id);
			++State().ChangeVersion;
			return id;
		}

		static void ForgetAssetPathIncremental(const std::string& path) {
			const std::string normalizedPath = NormalizePath(path);
			if (normalizedPath.empty() || IsMetaFilePath(normalizedPath)) {
				return;
			}

			auto pathIt = State().PathToId.find(normalizedPath);
			if (pathIt == State().PathToId.end()) {
				return;
			}

			const uint64_t id = pathIt->second;
			State().PathToId.erase(pathIt);
			if (auto recordIt = State().IdToRecord.find(id);
				recordIt != State().IdToRecord.end() && recordIt->second.Path == normalizedPath) {
				State().IdToRecord.erase(recordIt);
			}
			State().KnownMissingIds.erase(id);
			++State().ChangeVersion;
		}

		static uint64_t MoveAssetPathIncremental(const std::string& from, const std::string& to) {
			const std::string normalizedFrom = NormalizePath(from);
			const std::string normalizedTo = NormalizePath(to);
			if (normalizedFrom.empty() || normalizedTo.empty()
				|| IsMetaFilePath(normalizedFrom) || IsMetaFilePath(normalizedTo)) {
				return 0;
			}

			auto pathIt = State().PathToId.find(normalizedFrom);
			if (pathIt == State().PathToId.end()) {
				return ImportAssetPathIncremental(normalizedTo);
			}

			const uint64_t id = pathIt->second;
			State().PathToId.erase(pathIt);
			const AssetKind kind = Classify(normalizedTo);
			WriteMeta(normalizedTo, id, kind);
			Register(normalizedTo, id, kind);
			State().KnownMissingIds.erase(id);
			++State().ChangeVersion;
			return id;
		}

		static uint64_t GetGUID(const std::string& path) {
			return GetOrCreateAssetUUID(path);
		}

		static std::string ResolvePath(uint64_t assetId) {
			// Built-ins skip dirty/rebuild — their paths don't change at runtime.
			if (auto it = State().BuiltInById.find(assetId); it != State().BuiltInById.end()) {
				if (std::filesystem::exists(Path::FromUtf8(it->second.Path))) {
					return it->second.Path;
				}
				return {};
			}

			EnsureUpToDate();
			auto it = State().IdToRecord.find(assetId);
			if (it == State().IdToRecord.end()) {
				// The asset and its .meta may have been written to disk (scripts, external
				// tools, a not-yet-delivered FileWatcher event) after the last sync. Force one
				// recovery scan before treating the GUID as missing.
				if (State().KnownMissingIds.contains(assetId)) {
					return {};
				}

				State().Dirty = true;
				EnsureUpToDate();
				it = State().IdToRecord.find(assetId);
				if (it == State().IdToRecord.end()) {
					State().KnownMissingIds.insert(assetId);
					return {};
				}
			}

			if (std::filesystem::exists(Path::FromUtf8(it->second.Path))) {
				return it->second.Path;
			}

			// Cache miss so a permanently-moved asset doesn't trigger an O(N) rescan every frame; cleared by MarkDirty().
			if (State().KnownMissingIds.contains(assetId)) {
				return {};
			}

			State().Dirty = true;
			EnsureUpToDate();
			it = State().IdToRecord.find(assetId);
			if (it == State().IdToRecord.end() || !std::filesystem::exists(Path::FromUtf8(it->second.Path))) {
				State().KnownMissingIds.insert(assetId);
				return {};
			}

			return it->second.Path;
		}

		static AssetKind GetKind(uint64_t assetId) {
			if (auto it = State().BuiltInById.find(assetId); it != State().BuiltInById.end()) {
				return it->second.Kind;
			}
			EnsureUpToDate();
			auto it = State().IdToRecord.find(assetId);
			if (it == State().IdToRecord.end() && !State().KnownMissingIds.contains(assetId)) {
				// Match ResolvePath's recovery scan for late-created assets.
				State().Dirty = true;
				EnsureUpToDate();
				it = State().IdToRecord.find(assetId);
				if (it == State().IdToRecord.end()) {
					State().KnownMissingIds.insert(assetId);
				}
			}
			return it != State().IdToRecord.end() ? it->second.Kind : AssetKind::Unknown;
		}

		static bool Exists(uint64_t assetId) {
			return !ResolvePath(assetId).empty();
		}

		static bool IsBuiltIn(uint64_t assetId) {
			return State().BuiltInById.find(assetId) != State().BuiltInById.end();
		}

		static size_t GetBuiltInCount() {
			return State().BuiltInById.size();
		}

		static bool IsTexture(uint64_t assetId) {
			return GetKind(assetId) == AssetKind::Texture;
		}

		static bool IsAudio(uint64_t assetId) {
			return GetKind(assetId) == AssetKind::Audio;
		}

		static bool IsFont(uint64_t assetId) {
			return GetKind(assetId) == AssetKind::Font;
		}

		static std::vector<Record> GetAssetsByKind(AssetKind kind) {
			EnsureUpToDate();

			std::vector<Record> records;
			records.reserve(State().IdToRecord.size() + State().BuiltInById.size());

			for (const auto& [id, record] : State().IdToRecord) {
				if (kind != AssetKind::Unknown && record.Kind != kind) {
					continue;
				}
				records.push_back(record);
			}
			// Built-ins surface alongside project assets so the picker
			// shows the engine's default font/etc. as selectable entries.
			for (const auto& [id, record] : State().BuiltInById) {
				if (kind != AssetKind::Unknown && record.Kind != kind) {
					continue;
				}
				records.push_back(record);
			}

			std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) {
				const std::string aName = Path::ToUtf8(Path::FromUtf8(a.Path).filename());
				const std::string bName = Path::ToUtf8(Path::FromUtf8(b.Path).filename());
				if (aName == bName) {
					return a.Path < b.Path;
				}

				return aName < bName;
			});

			return records;
		}

		static std::vector<Record> FindAll(AssetKind kind, const std::string& pathPrefix = {}) {
			EnsureUpToDate();

			std::string normalizedPrefix;
			if (!pathPrefix.empty()) {
				std::filesystem::path prefixPath = Path::FromUtf8(pathPrefix);
				if (!prefixPath.is_absolute()) {
					const std::string root = GetAssetsRoot();
					if (!root.empty()) {
						prefixPath = Path::FromUtf8(root) / prefixPath;
					}
				}
				normalizedPrefix = NormalizePath(Path::ToUtf8(prefixPath));
			}

			std::vector<Record> records;
			for (const auto& [id, record] : State().IdToRecord) {
				(void)id;
				if (kind != AssetKind::Unknown && record.Kind != kind) {
					continue;
				}
				if (!normalizedPrefix.empty()
					&& (record.Path.size() < normalizedPrefix.size()
						|| record.Path.compare(0, normalizedPrefix.size(), normalizedPrefix) != 0)) {
					continue;
				}
				records.push_back(record);
			}

			std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) {
				return a.Path < b.Path;
			});
			return records;
		}

		static std::string GetDisplayName(uint64_t assetId) {
			const std::string path = ResolvePath(assetId);
			if (path.empty()) {
				return {};
			}

			return Path::ToUtf8(Path::FromUtf8(path).filename());
		}

		static std::string GetMetaPath(const std::string& assetPath) {
			if (assetPath.empty()) {
				return {};
			}

			return assetPath + std::string(MetaExtension);
		}

		static void DeleteCompanionMetadata(const std::string& assetPath) {
			const std::string normalizedPath = NormalizePath(assetPath);
			if (normalizedPath.empty() || IsMetaFilePath(normalizedPath)) {
				return;
			}

			const std::string metaPath = GetMetaPath(normalizedPath);
			std::error_code ec;
			std::filesystem::remove(Path::FromUtf8(metaPath), ec);
			ForgetAssetPathIncremental(normalizedPath);
		}

		static void MoveCompanionMetadata(const std::string& from, const std::string& to) {
			const std::string normalizedFrom = NormalizePath(from);
			const std::string normalizedTo = NormalizePath(to);
			if (normalizedFrom.empty() || normalizedTo.empty() || IsMetaFilePath(normalizedFrom) || IsMetaFilePath(normalizedTo)) {
				return;
			}

			const std::string metaFrom = GetMetaPath(normalizedFrom);
			if (!File::Exists(metaFrom)) {
				MoveAssetPathIncremental(normalizedFrom, normalizedTo);
				return;
			}

			const std::string metaTo = GetMetaPath(normalizedTo);
			std::error_code ec;
			const std::filesystem::path source = Path::FromUtf8(metaFrom);
			const std::filesystem::path target = Path::FromUtf8(metaTo);
			if (target.has_parent_path()) {
				std::filesystem::create_directories(target.parent_path(), ec);
				ec.clear();
			}

			std::filesystem::rename(source, target, ec);
			if (ec) {
				ec.clear();
				std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
				if (!ec) {
					ec.clear();
					std::filesystem::remove(source, ec);
				}
			}

			MoveAssetPathIncremental(normalizedFrom, normalizedTo);
		}

	private:
		static std::string GetAssetsRoot(bool forceRefresh = false) {
			if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
				// Dirty notifications and explicit Sync recheck filesystem aliases; relative roots never match.
				if (!forceRefresh && !State().Dirty && project->AssetsDirectory == State().TrackedRoot) {
					return State().TrackedRoot;
				}
				return NormalizePath(project->AssetsDirectory);
			}

			const std::string fallback = Path::Combine(Path::ExecutableDir(), "Assets");
			if (!std::filesystem::exists(Path::FromUtf8(fallback))) {
				return {};
			}

			return NormalizePath(fallback);
		}

		static void EnsureUpToDate(bool forceRootRefresh = false) {
			auto& state = State();
			if (!forceRootRefresh && !state.Dirty) {
				if (const auto* project = ProjectManager::GetCurrentProject();
					project && project->AssetsDirectory == state.TrackedRoot) {
					return;
				}
			}
			const std::string root = GetAssetsRoot(forceRootRefresh);
			if (root != state.TrackedRoot) {
				state.TrackedRoot = root;
				state.Dirty = true;
				state.ImmutableTrustedLoaded = false;
				state.ImmutableFullScanned = false;
			}

			if (!state.Dirty) {
				return;
			}

			// Immutable assets can't change on disk, so once a full authoritative walk has run a
			// per-miss recovery (ResolvePath/GetKind set State().Dirty) is pointless — short-circuit it.
			if (state.AssetsImmutable && state.ImmutableFullScanned) {
				state.Dirty = false;
				return;
			}

			Rebuild();
		}

		static void Rebuild() {
			State().IdToRecord.clear();
			State().PathToId.clear();
			// New scan: previously-missing GUIDs may exist again.
			State().KnownMissingIds.clear();
			State().ColdMetaReads = 0;
			const auto rebuildStart = std::chrono::steady_clock::now();

			if (State().TrackedRoot.empty() || !std::filesystem::exists(Path::FromUtf8(State().TrackedRoot))) {
				State().Dirty = false;
				return;
			}

			// Immutable assets (standalone runtime): trust the shipped manifest and skip the whole
			// directory walk + per-.meta JSON parse on the first build. ResolvePath still existence-
			// checks each hit; if a referenced GUID is missing from a stale manifest, that miss forces
			// one real walk below (which then rewrites an accurate manifest and self-heals).
			if (State().AssetsImmutable && !State().ImmutableTrustedLoaded && !State().ImmutableFullScanned
				&& LoadSnapshotTrusted()) {
				State().Dirty = false;
				State().ImmutableTrustedLoaded = true;
				const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rebuildStart).count();
				FNK_CORE_INFO_TAG("AssetRegistry", "Project registry: trusted manifest, {} assets, no walk, in {} ms",
					State().IdToRecord.size(), static_cast<long long>(ms));
				return;
			}

			// Mutable/editor (or an immutable recovery walk): warm the .meta fingerprint cache from
			// the last snapshot so the walk skips File::ReadAllText + JSON parse for every .meta whose
			// (mtime,size) is unchanged.
			WarmMetaCacheFromSnapshot();

			std::error_code ec;
			for (std::filesystem::recursive_directory_iterator it(
				 Path::FromUtf8(State().TrackedRoot),
				 std::filesystem::directory_options::skip_permission_denied,
				 ec), end;
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

				// Skip .meta sidecars BEFORE NormalizePath's weakly_canonical — there are ~as
				// many .meta files as assets, and each would pay multi-syscall path resolution
				// only to be discarded here.
				const std::string rawPath = Path::ToUtf8(it->path());
				if (IsMetaFilePath(rawPath)) {
					continue;
				}

				const std::string assetPath = NormalizePath(rawPath);
				if (assetPath.empty()) {
					continue;
				}

				IndexAsset(assetPath);
			}

			State().Dirty = false;
			if (State().AssetsImmutable) {
				State().ImmutableFullScanned = true;  // authoritative on-disk scan done; manifest now accurate
			}
			SaveSnapshot();
			const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rebuildStart).count();
			FNK_CORE_INFO_TAG("AssetRegistry",
				"Project registry: walked + indexed {} assets in {} ms ({} cold .meta reads, {} cache hits)",
				State().IdToRecord.size(), static_cast<long long>(ms), State().ColdMetaReads,
				State().IdToRecord.size() >= State().ColdMetaReads ? State().IdToRecord.size() - State().ColdMetaReads : 0);
		}

		// ---- Persisted registry snapshot ------------------------------------------------
		// A flat <root-parent>/.funke/asset-registry.cache file lets a relaunch skip the cold
		// ~9k-.meta JSON-parse pass. Editors use it as a fingerprint cache (the walk still runs and
		// validates each .meta's mtime/size); immutable standalone runtimes trust it outright and
		// skip the walk. Paths are stored relative to the tracked root, so the same file is portable
		// from the project dir to a shipped build dir.
		struct SnapshotRecord {
			uint64_t Guid = 0;
			int Kind = 0;
			long long MetaMtime = 0;
			std::uintmax_t MetaSize = 0;
			std::string RelPath;
		};

		static std::string GetSnapshotPath() {
			if (State().TrackedRoot.empty()) {
				return {};
			}
			std::filesystem::path dir = Path::FromUtf8(State().TrackedRoot).parent_path() / ".funke";
			return Path::ToUtf8((dir / "asset-registry.cache").make_preferred());
		}

		// assetPath is under State().TrackedRoot (verified by caller); strip the root + separator, force '/'.
		static std::string MakeRelativeGeneric(const std::string& assetPath) {
			if (assetPath.size() <= State().TrackedRoot.size() + 1) {
				return {};
			}
			std::string rel = assetPath.substr(State().TrackedRoot.size() + 1);
			std::replace(rel.begin(), rel.end(), '\\', '/');
			return rel;
		}

		static std::string ReconstructAbsolute(const std::string& relGeneric) {
			return Path::ToUtf8((Path::FromUtf8(State().TrackedRoot) / Path::FromUtf8(relGeneric)).make_preferred());
		}

		static uint64_t SnapshotU64(std::string_view s) {
			uint64_t v = 0;
			std::from_chars(s.data(), s.data() + s.size(), v);
			return v;
		}

		static long long SnapshotI64(std::string_view s) {
			long long v = 0;
			std::from_chars(s.data(), s.data() + s.size(), v);
			return v;
		}

		static bool ReadSnapshot(std::vector<SnapshotRecord>& out) {
			const std::string snapshotPath = GetSnapshotPath();
			if (snapshotPath.empty() || !File::Exists(snapshotPath)) {
				return false;
			}

			const std::string text = File::ReadAllText(snapshotPath);
			if (text.size() < 7 || text.compare(0, 6, "FNKAR1") != 0) {
				return false;  // missing/old format header -> fall back to a cold scan
			}

			size_t pos = text.find('\n');
			if (pos == std::string::npos) {
				return false;
			}
			++pos;

			out.reserve(text.size() / 40 + 1);
			while (pos < text.size()) {
				size_t eol = text.find('\n', pos);
				if (eol == std::string::npos) {
					eol = text.size();
				}
				std::string_view line(text.data() + pos, eol - pos);
				pos = eol + 1;
				if (line.empty()) {
					continue;
				}

				// guid \t kind \t metaMtime \t metaSize \t relPath  (relPath last; may contain spaces, never tabs)
				size_t cursor = 0;
				std::string_view fields[4];
				bool ok = true;
				for (int i = 0; i < 4; ++i) {
					const size_t tab = line.find('\t', cursor);
					if (tab == std::string_view::npos) {
						ok = false;
						break;
					}
					fields[i] = line.substr(cursor, tab - cursor);
					cursor = tab + 1;
				}
				if (!ok) {
					continue;
				}

				SnapshotRecord rec;
				rec.Guid = SnapshotU64(fields[0]);
				rec.Kind = static_cast<int>(SnapshotI64(fields[1]));
				rec.MetaMtime = SnapshotI64(fields[2]);
				rec.MetaSize = static_cast<std::uintmax_t>(SnapshotU64(fields[3]));
				rec.RelPath = std::string(line.substr(cursor));
				if (rec.Guid == 0 || rec.RelPath.empty()) {
					continue;
				}
				out.push_back(std::move(rec));
			}

			return !out.empty();
		}

		static bool LoadSnapshotTrusted() {
			std::vector<SnapshotRecord> records;
			if (!ReadSnapshot(records)) {
				return false;
			}
			for (const SnapshotRecord& rec : records) {
				const std::string abs = ReconstructAbsolute(rec.RelPath);
				if (abs.empty()) {
					continue;
				}
				Register(abs, rec.Guid, static_cast<AssetKind>(rec.Kind));
			}
			return !State().IdToRecord.empty();
		}

		static void WarmMetaCacheFromSnapshot() {
			std::vector<SnapshotRecord> records;
			if (!ReadSnapshot(records)) {
				return;
			}
			for (const SnapshotRecord& rec : records) {
				if (rec.MetaMtime == 0 && rec.MetaSize == 0) {
					continue;  // no recorded fingerprint -> let ReadMetaId cold-read this one
				}
				const std::string abs = ReconstructAbsolute(rec.RelPath);
				if (abs.empty()) {
					continue;
				}
				MetaIdCacheEntry entry;
				entry.Mtime = std::filesystem::file_time_type(std::filesystem::file_time_type::duration(rec.MetaMtime));
				entry.Size = rec.MetaSize;
				entry.AssetGuid = rec.Guid;
				State().MetaIdCache[GetMetaPath(abs)] = entry;
			}
		}

		static void SaveSnapshot() {
			const std::string snapshotPath = GetSnapshotPath();
			if (snapshotPath.empty() || State().IdToRecord.empty()) {
				return;
			}

			std::error_code ec;
			std::filesystem::create_directories(Path::FromUtf8(snapshotPath).parent_path(), ec);

			std::string out;
			out.reserve(State().IdToRecord.size() * 48 + 8);
			out += "FNKAR1\n";
			for (const auto& [id, rec] : State().IdToRecord) {
				const std::string rel = MakeRelativeGeneric(rec.Path);
				if (rel.empty()) {
					continue;
				}
				long long mtime = 0;
				std::uintmax_t size = 0;
				if (const auto it = State().MetaIdCache.find(GetMetaPath(rec.Path)); it != State().MetaIdCache.end()) {
					mtime = static_cast<long long>(it->second.Mtime.time_since_epoch().count());
					size = it->second.Size;
				}
				out += std::to_string(id);
				out += '\t';
				out += std::to_string(static_cast<int>(rec.Kind));
				out += '\t';
				out += std::to_string(mtime);
				out += '\t';
				out += std::to_string(size);
				out += '\t';
				out += rel;
				out += '\n';
			}
			File::WriteAllText(snapshotPath, out);
		}

		// Rebuild's directory iterator already proved this is a regular file, so use the
		// path-only tracked check and skip a redundant exists()+is_regular_file() re-stat.
		static void IndexAsset(const std::string& assetPath) {
			if (!IsTrackedPath(assetPath)) {
				return;
			}

			AssetKind kind = Classify(assetPath);
			uint64_t id = ReadMetaId(assetPath);
			if (id == 0) {
				// Brand-new asset: mint an id and write its meta.
				id = static_cast<uint64_t>(UUID());
				WriteMeta(assetPath, id, kind);
			} else if (auto existing = State().IdToRecord.find(id); existing != State().IdToRecord.end()) {
				// Two assets claim the same GUID. Surface the collision so
				// the user knows references to one of them will silently
				// drift; the later-indexed asset gets a fresh GUID.
				FNK_CORE_WARN_TAG("AssetRegistry",
					"GUID collision while indexing '{}': id {} already owned by '{}' — regenerating new GUID for '{}'",
					assetPath, id, existing->second.Path, assetPath);
				id = static_cast<uint64_t>(UUID());
				WriteMeta(assetPath, id, kind);
			}
			Register(assetPath, id, kind);
		}

		static void Register(const std::string& assetPath, uint64_t id, AssetKind kind) {
			Record record;
			record.Id = id;
			record.Path = assetPath;
			record.Kind = kind;

			State().PathToId[assetPath] = id;
			State().IdToRecord[id] = std::move(record);
		}

		static std::string NormalizePath(const std::string& path) {
			if (path.empty()) {
				return {};
			}

			std::filesystem::path fsPath = Path::FromUtf8(path);
			if (!fsPath.is_absolute()) {
				if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
					const std::filesystem::path candidate = Path::FromUtf8(project->AssetsDirectory) / fsPath;
					if (std::filesystem::exists(candidate)) {
						fsPath = candidate;
					}
					else {
						fsPath = std::filesystem::absolute(fsPath);
					}
				}
				else {
					const std::filesystem::path fallback = Path::FromUtf8(Path::ExecutableDir()) / "Assets" / fsPath;
					if (std::filesystem::exists(fallback)) {
						fsPath = fallback;
					}
					else {
						fsPath = std::filesystem::absolute(fsPath);
					}
				}
			}

			std::error_code ec;
			std::filesystem::path normalized = std::filesystem::weakly_canonical(fsPath, ec);
			if (ec) {
				normalized = fsPath.lexically_normal();
			}

			return Path::ToUtf8(normalized.make_preferred());
		}

		// Prefix/containment check only — no filesystem stat. Use when the caller already
		// confirmed the file exists (e.g. Rebuild's directory iterator).
		static bool IsTrackedPath(const std::string& assetPath) {
			if (assetPath.empty() || State().TrackedRoot.empty()) {
				return false;
			}

			if (assetPath.size() < State().TrackedRoot.size() || assetPath.compare(0, State().TrackedRoot.size(), State().TrackedRoot) != 0) {
				return false;
			}

			if (assetPath.size() == State().TrackedRoot.size()) {
				return false;
			}

			const char separator = assetPath[State().TrackedRoot.size()];
			return separator == '\\' || separator == '/';
		}

		static bool IsTrackedAsset(const std::string& assetPath) {
			if (!IsTrackedPath(assetPath)) {
				return false;
			}

			const std::filesystem::path nativePath = Path::FromUtf8(assetPath);
			return std::filesystem::exists(nativePath) && std::filesystem::is_regular_file(nativePath);
		}

		static AssetKind Classify(const std::string& assetPath) {
			const std::string extension = ToLowerCopy(Path::ToUtf8(Path::FromUtf8(assetPath).extension()));
			if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tga") {
				return AssetKind::Texture;
			}
			if (extension == ".wav" || extension == ".mp3" || extension == ".ogg" || extension == ".flac") {
				return AssetKind::Audio;
			}
			if (extension == ".scene") {
				return AssetKind::Scene;
			}
			if (extension == ".prefab") {
				return AssetKind::Prefab;
			}
			if (extension == ".cs" || extension == ".cpp" || extension == ".cc" || extension == ".cxx" || extension == ".c"
				|| extension == ".h" || extension == ".hh" || extension == ".hpp" || extension == ".hxx"
				|| extension == ".inl" || extension == ".ipp") {
				return AssetKind::Script;
			}
			if (extension == ".ttf" || extension == ".otf") {
				return AssetKind::Font;
			}
			if (extension == ".wgsl" || extension == ".vs" || extension == ".fs" || extension == ".vert" || extension == ".frag" || extension == ".glsl") {
				return AssetKind::Shader;
			}
			if (extension == ".dataasset") {
				return AssetKind::DataAsset;
			}
			if (extension == ".anim") {
				return AssetKind::AnimationClip;
			}
			if (extension == ".animctrl") {
				return AssetKind::AnimatorController;
			}
			if (extension == ".material") {
				return AssetKind::Material;
			}
			if (extension == ".obj" || extension == ".gltf" || extension == ".glb") {
				return AssetKind::Mesh;
			}
			if (!extension.empty()) {
				return AssetKind::Other;
			}

			return AssetKind::Unknown;
		}

		static uint64_t ReadMetaId(const std::string& assetPath) {
			const std::string metaPath = GetMetaPath(assetPath);

			// Skip the file read + JSON parse when (mtime, size) match a prior call — avoids ~1000 parses per dirty cycle on large projects.
			std::error_code ec;
			std::filesystem::file_time_type mtime{};
			std::uintmax_t size = 0;
			bool fingerprintOk = false;
			const std::filesystem::path nativeMetaPath = Path::FromUtf8(metaPath);
			if (std::filesystem::exists(nativeMetaPath, ec) && !ec) {
				mtime = std::filesystem::last_write_time(nativeMetaPath, ec);
				if (!ec) {
					size = std::filesystem::file_size(nativeMetaPath, ec);
					fingerprintOk = !ec;
				}
			}
			if (!fingerprintOk) {
			}
			else {
				const auto cacheIt = State().MetaIdCache.find(metaPath);
				if (cacheIt != State().MetaIdCache.end()
					&& cacheIt->second.Mtime == mtime
					&& cacheIt->second.Size == size)
				{
					return cacheIt->second.AssetGuid;
				}
			}

			if (!File::Exists(metaPath)) {
				if (fingerprintOk) {
					State().MetaIdCache.erase(metaPath);
				}
				return 0;
			}

			// A real file open+read+parse (Defender-scans the open) rather than a cache hit — counted
			// so Rebuild can report how many of these the cold path actually paid.
			++State().ColdMetaReads;
			Json::Value meta;
			std::string parseError;
			if (!Json::TryParse(File::ReadAllText(metaPath), meta, &parseError) || !meta.IsObject()) {
				return 0;
			}

			const Json::Value* uuidValue = meta.FindMember("AssetGUID");
			if (!uuidValue) {
				uuidValue = meta.FindMember("uuid");
			}
			if (!uuidValue) {
				return 0;
			}

			uint64_t parsed = 0;
			if (uuidValue->IsString()) {
				try {
					parsed = static_cast<uint64_t>(std::stoull(uuidValue->AsStringOr()));
				}
				catch (...) {
					parsed = 0;
				}
			}
			else {
				parsed = uuidValue->AsUInt64Or(0);
			}

			if (fingerprintOk && parsed != 0) {
				State().MetaIdCache[metaPath] = MetaIdCacheEntry{ mtime, size, parsed };
			}
			return parsed;
		}

		// Insert OR overwrite; AddMember alone is append-only and would silently produce duplicate keys.
		static void SetOrAddMember(Json::Value& object, const std::string& key, Json::Value value) {
			if (Json::Value* existing = object.FindMember(key)) {
				*existing = std::move(value);
				return;
			}
			object.AddMember(key, std::move(value));
		}

		// Only for callers about to rewrite the meta: an unparsable one is backed up first, since the
		// rewrite drops its GUID and `import`/`sprites` blocks (often a hand edit with a typo).
		static Json::Value LoadMetaJson(const std::string& metaPath) {
			Json::Value root = Json::Value::MakeObject();
			if (!File::Exists(metaPath)) {
				return root;
			}
			const std::string text = File::ReadAllText(metaPath);
			Json::Value parsed;
			std::string parseError;
			const bool parsedOk = Json::TryParse(text, parsed, &parseError);
			if (parsedOk && parsed.IsObject()) {
				return parsed;
			}
			if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
				return root;
			}

			const std::string backupPath = metaPath + ".corrupt";
			const bool backedUp = File::WriteAllText(backupPath, text);
			FNK_CORE_ERROR_TAG("AssetRegistry",
				"Meta file '{}' is not a valid JSON object ({}); it is regenerated, original {}",
				metaPath, parsedOk ? std::string("root is not an object") : parseError,
				backedUp ? "saved as '" + backupPath + "'" : std::string("could NOT be backed up"));
			return root;
		}

		// Merge-write: load existing meta, overwrite identity fields only — leaves `import`, `sprites`, etc. intact.
		static void WriteMeta(const std::string& assetPath, uint64_t id, AssetKind kind) {
			const std::string metaPath = GetMetaPath(assetPath);
			Json::Value meta = LoadMetaJson(metaPath);
			if (!meta.IsObject()) {
				meta = Json::Value::MakeObject();
			}
			SetOrAddMember(meta, "AssetGUID", Json::Value(std::to_string(id)));
			SetOrAddMember(meta, "uuid",      Json::Value(std::to_string(id)));
			SetOrAddMember(meta, "kind",      Json::Value(ToString(kind)));
			if (File::WriteAllText(metaPath, Json::Stringify(meta, true))) {
				// Invalidate the (mtime,size) fingerprint cache — same-size/same-tick
				// rewrites (e.g. a GUID-collision regen) would otherwise let ReadMetaId
				// return the STALE pre-write id. Mirrors WriteTextureMeta.
				State().MetaIdCache.erase(metaPath);
			}
		}

	public:
		static TextureMeta ReadTextureMeta(const std::string& assetPath) {
			TextureMeta meta;
			const std::string metaPath = GetMetaPath(assetPath);
			if (!File::Exists(metaPath)) {
				return meta;
			}
			Json::Value root;
			std::string parseError;
			if (!Json::TryParse(File::ReadAllText(metaPath), root, &parseError) || !root.IsObject()) {
				return meta;
			}

			if (const Json::Value* importNode = root.FindMember("import"); importNode && importNode->IsObject()) {
				meta.HasImportBlock = true;
				if (const Json::Value* filterNode = importNode->FindMember("filter")) {
					if (filterNode->IsString()) {
						if (auto parsed = magic_enum::enum_cast<Filter>(filterNode->AsStringOr()); parsed.has_value()) {
							meta.Import.FilterMode = parsed.value();
						}
					}
					else if (filterNode->IsNumber()) {
						meta.Import.FilterMode = static_cast<Filter>(filterNode->AsIntOr(static_cast<int>(meta.Import.FilterMode)));
					}
				}
				auto readWrap = [&](const char* key, Wrap& outWrap) {
					if (const Json::Value* node = importNode->FindMember(key)) {
						if (node->IsString()) {
							// Funke::TryParse, not magic_enum::enum_cast - see Graphics/Wrap.hpp.
							Funke::TryParse(node->AsStringOr(), outWrap);
						}
						else if (node->IsNumber()) {
							outWrap = static_cast<Wrap>(node->AsInt64Or(static_cast<int64_t>(outWrap)));
						}
					}
				};
				readWrap("wrapU", meta.Import.WrapU);
				readWrap("wrapV", meta.Import.WrapV);
			}

			if (const Json::Value* spritesNode = root.FindMember("sprites"); spritesNode && spritesNode->IsArray()) {
				for (const Json::Value& spriteValue : spritesNode->GetArray()) {
					if (!spriteValue.IsObject()) continue;
					SpriteSlice slice;
					if (const Json::Value* n = spriteValue.FindMember("name"); n && n->IsString()) {
						slice.Name = n->AsStringOr();
					}
					if (slice.Name.empty()) continue;
					if (const Json::Value* r = spriteValue.FindMember("rect"); r && r->IsArray() && r->GetArray().size() >= 4) {
						const auto& a = r->GetArray();
						slice.X = a[0].AsIntOr(0);
						slice.Y = a[1].AsIntOr(0);
						slice.W = a[2].AsIntOr(0);
						slice.H = a[3].AsIntOr(0);
					}
					if (slice.W <= 0 || slice.H <= 0) continue;
					if (const Json::Value* p = spriteValue.FindMember("pivot"); p && p->IsArray() && p->GetArray().size() >= 2) {
						const auto& a = p->GetArray();
						slice.PivotX = static_cast<float>(a[0].AsDoubleOr(0.5));
						slice.PivotY = static_cast<float>(a[1].AsDoubleOr(0.5));
					}
					slice.Border = ReadSpriteBorder(spriteValue);
					meta.Sprites.push_back(std::move(slice));
				}
			}
			meta.Border = ReadSpriteBorder(root);

			return meta;
		}

		// "border": [left, bottom, right, top]; absent or malformed = no border.
		static SpriteBorder ReadSpriteBorder(const Json::Value& owner) {
			SpriteBorder border;
			const Json::Value* node = owner.FindMember("border");
			if (!node || !node->IsArray() || node->GetArray().size() < 4) return border;
			const auto& a = node->GetArray();
			border.Left = std::max(a[0].AsIntOr(0), 0);
			border.Bottom = std::max(a[1].AsIntOr(0), 0);
			border.Right = std::max(a[2].AsIntOr(0), 0);
			border.Top = std::max(a[3].AsIntOr(0), 0);
			return border;
		}

		static Json::Value MakeSpriteBorderValue(const SpriteBorder& border) {
			Json::Value value = Json::Value::MakeArray();
			value.Append(Json::Value(border.Left));
			value.Append(Json::Value(border.Bottom));
			value.Append(Json::Value(border.Right));
			value.Append(Json::Value(border.Top));
			return value;
		}

		// Merge-write `import` and `sprites` blocks; preserves all other meta fields already on disk.
		static bool WriteTextureMeta(const std::string& assetPath, const TextureMeta& meta) {
			const std::string metaPath = GetMetaPath(assetPath);
			Json::Value root = LoadMetaJson(metaPath);
			if (!root.IsObject()) {
				root = Json::Value::MakeObject();
			}

			if (meta.HasImportBlock) {
				Json::Value importValue = Json::Value::MakeObject();
				importValue.AddMember("filter", Json::Value(std::string(magic_enum::enum_name(meta.Import.FilterMode))));
				// Funke:: qualified: the member ToString(AssetKind) below would
				// otherwise hide the namespace-scope ToString(Wrap) by name lookup.
				importValue.AddMember("wrapU",  Json::Value(std::string(Funke::ToString(meta.Import.WrapU))));
				importValue.AddMember("wrapV",  Json::Value(std::string(Funke::ToString(meta.Import.WrapV))));
				SetOrAddMember(root, "import", std::move(importValue));
			}

			Json::Value spritesValue = Json::Value::MakeArray();
			for (const SpriteSlice& slice : meta.Sprites) {
				Json::Value spriteValue = Json::Value::MakeObject();
				spriteValue.AddMember("name", Json::Value(slice.Name));
				Json::Value rectValue = Json::Value::MakeArray();
				rectValue.Append(Json::Value(slice.X));
				rectValue.Append(Json::Value(slice.Y));
				rectValue.Append(Json::Value(slice.W));
				rectValue.Append(Json::Value(slice.H));
				spriteValue.AddMember("rect", std::move(rectValue));
				Json::Value pivotValue = Json::Value::MakeArray();
				pivotValue.Append(Json::Value(static_cast<double>(slice.PivotX)));
				pivotValue.Append(Json::Value(static_cast<double>(slice.PivotY)));
				spriteValue.AddMember("pivot", std::move(pivotValue));
				if (!slice.Border.IsZero()) {
					spriteValue.AddMember("border", MakeSpriteBorderValue(slice.Border));
				}
				spritesValue.Append(std::move(spriteValue));
			}
			SetOrAddMember(root, "sprites", std::move(spritesValue));
			if (!meta.Border.IsZero()) {
				SetOrAddMember(root, "border", MakeSpriteBorderValue(meta.Border));
			}
			else {
				std::erase_if(root.GetObject(), [](const auto& member) { return member.first == "border"; });
			}

			const bool wrote = File::WriteAllText(metaPath, Json::Stringify(root, true));
			if (wrote) {
				State().MetaIdCache.erase(metaPath);
			}
			return wrote;
		}

	private:
		static std::string ToString(AssetKind kind) {
			switch (kind) {
			case AssetKind::Texture: return "Texture";
			case AssetKind::Audio: return "Audio";
			case AssetKind::Scene: return "Scene";
			case AssetKind::Prefab: return "Prefab";
			case AssetKind::Script: return "Script";
			case AssetKind::Font: return "Font";
			case AssetKind::Shader: return "Shader";
			case AssetKind::DataAsset: return "DataAsset";
			case AssetKind::Material: return "Material";
			case AssetKind::Mesh: return "Mesh";
			case AssetKind::AnimationClip: return "AnimationClip";
			case AssetKind::AnimatorController: return "AnimatorController";
			case AssetKind::Other: return "Other";
			case AssetKind::Unknown:
			default:
				return "Unknown";
			}
		}

		static std::string ToLowerCopy(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return value;
		}

	private:
		struct MetaIdCacheEntry {
			std::filesystem::file_time_type Mtime{};
			std::uintmax_t Size = 0;
			uint64_t AssetGuid = 0;
		};

		// Single instance owned by Funke-Engine.dll (see AssetRegistry.cpp) so every module that
		// inlines these methods shares one registry instead of a per-binary copy.
		struct StateData {
			bool Dirty = true;
			// Set once by a standalone runtime (assets are read-only): trust the shipped manifest.
			// TrustedLoaded latches after the manifest is loaded (so a later miss forces one real walk
			// instead of re-loading the manifest); FullScanned latches after that authoritative walk, so
			// further per-miss recoveries short-circuit.
			bool AssetsImmutable = false;
			bool ImmutableTrustedLoaded = false;
			bool ImmutableFullScanned = false;
			// Diagnostic: cold .meta reads (file open+parse) during the current Rebuild; reset at its start.
			uint64_t ColdMetaReads = 0;
			uint64_t ChangeVersion = 1;
			std::string TrackedRoot;
			std::unordered_map<uint64_t, Record> IdToRecord;
			std::unordered_map<std::string, uint64_t> PathToId;
			std::unordered_map<std::string, MetaIdCacheEntry> MetaIdCache;
			// Cleared on MarkDirty()/Rebuild() so FileWatcher events let recovered assets re-resolve.
			std::unordered_set<uint64_t> KnownMissingIds;
			std::unordered_map<uint64_t, Record> BuiltInById;
			std::unordered_map<std::string, uint64_t> BuiltInPathToId;
		};

		FUNKE_API static StateData& State();
	};

}
