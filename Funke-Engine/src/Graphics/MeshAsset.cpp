#include "pch.hpp"
#include "Graphics/MeshAsset.hpp"

#include "Assets/AssetRegistry.hpp"
#include "Assets/AssetLoader.hpp"
#include "Graphics/GltfImporter.hpp"
#include "Graphics/SkeletalAnimation.hpp"
#include "Serialization/File.hpp"
#include "Serialization/FileStamp.hpp"
#include "Serialization/Path.hpp"
#include "Utils/CharConv.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Funke {
	namespace {
		bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

		std::string_view NextToken(std::string_view& line) {
			size_t begin = 0;
			while (begin < line.size() && IsSpace(line[begin])) ++begin;
			size_t end = begin;
			while (end < line.size() && !IsSpace(line[end])) ++end;
			const std::string_view token = line.substr(begin, end - begin);
			line.remove_prefix(end);
			return token;
		}

		bool ParseFloat(std::string_view token, float& out) {
			if (!token.empty() && token.front() == '+') token.remove_prefix(1);
			const auto result = CharConv::ParseFloat(token.data(), token.data() + token.size(), out);
			return result.ec == std::errc() && result.ptr == token.data() + token.size();
		}

		constexpr int64_t k_NoIndex = -1;
		constexpr int64_t k_BadIndex = -2;

		// 1-based or negative (relative to the current end) OBJ index -> 0-based.
		int64_t ResolveIndex(std::string_view token, size_t count) {
			if (token.empty()) return k_NoIndex;
			int64_t value = 0;
			const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
			if (result.ec != std::errc() || result.ptr != token.data() + token.size() || value == 0) return k_BadIndex;
			const int64_t resolved = value > 0 ? value - 1 : static_cast<int64_t>(count) + value;
			return resolved >= 0 && resolved < static_cast<int64_t>(count) ? resolved : k_BadIndex;
		}

		struct Corner {
			int64_t Position = k_NoIndex;
			int64_t Uv = k_NoIndex;
			int64_t Normal = k_NoIndex;
			bool operator==(const Corner&) const = default;
		};

		struct CornerHash {
			size_t operator()(const Corner& c) const noexcept {
				size_t hash = std::hash<int64_t>{}(c.Position);
				hash ^= std::hash<int64_t>{}(c.Uv) + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
				hash ^= std::hash<int64_t>{}(c.Normal) + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
				return hash;
			}
		};

		struct Entry {
			MeshAsset Value;
			std::string Path;
			std::unordered_map<std::string, FileStamp> Dependencies;
			bool Valid = false;
			bool Runtime = false;
			std::chrono::steady_clock::time_point LastAttempt{};
			std::chrono::steady_clock::time_point LastUsed{};
			bool UsedSincePurge = false;
		};

		bool LoadMeshAsset(const std::string& path, MeshAsset& outAsset, std::unordered_map<std::string, FileStamp>* dependencies);

		std::unordered_map<uint64_t, Entry>& Entries() {
			static std::unordered_map<uint64_t, Entry> s_Entries;
			return s_Entries;
		}

		uint32_t NextRevision() {
			static uint32_t s_Revision = 0;
			return ++s_Revision;
		}

		void UpdateBounds(MeshAsset& asset) {
			if (asset.Data.Vertices.empty()) {
				asset.BoundsMin = asset.BoundsMax = glm::vec3(0.0f);
				return;
			}
			glm::vec3 minPoint(std::numeric_limits<float>::max());
			glm::vec3 maxPoint(std::numeric_limits<float>::lowest());
			for (const MeshVertex& v : asset.Data.Vertices) {
				const glm::vec3 p(v.Position[0], v.Position[1], v.Position[2]);
				minPoint = glm::min(minPoint, p);
				maxPoint = glm::max(maxPoint, p);
			}
			asset.BoundsMin = minPoint;
			asset.BoundsMax = maxPoint;
		}

		bool LoadEntry(uint64_t guid, Entry& entry) {
			entry.LastAttempt = std::chrono::steady_clock::now();
			entry.Path = AssetRegistry::ResolvePath(guid);
			entry.Value = MeshAsset{};
			entry.Dependencies.clear();
			entry.Valid = !entry.Path.empty() && LoadMeshAsset(entry.Path, entry.Value, &entry.Dependencies);
			if (!entry.Valid) return false;

			UpdateBounds(entry.Value);
			entry.Value.Revision = NextRevision();
			return true;
		}
	}

	bool ParseObjMesh(std::string_view text, PrimitiveMeshData& outMesh, std::string* outError) {
		outMesh = {};
		std::vector<glm::vec3> positions;
		std::vector<glm::vec2> uvs;
		std::vector<glm::vec3> normals;
		std::unordered_map<Corner, uint32_t, CornerHash> vertexByCorner;
		// Per output vertex: its position index (generated normals are shared per position) and whether it lacks a normal.
		std::vector<int64_t> vertexPosition;
		std::vector<uint8_t> vertexNeedsNormal;
		bool anyMissingNormal = false;
		std::vector<uint32_t> polygon;
		size_t lineNumber = 0;

		auto fail = [&](const std::string& message) {
			if (outError) *outError = "line " + std::to_string(lineNumber) + ": " + message;
			outMesh = {};
			return false;
		};

		while (!text.empty()) {
			++lineNumber;
			const size_t eol = text.find('\n');
			std::string_view line = text.substr(0, eol);
			text.remove_prefix(eol == std::string_view::npos ? text.size() : eol + 1);
			if (const size_t comment = line.find('#'); comment != std::string_view::npos) line = line.substr(0, comment);

			const std::string_view keyword = NextToken(line);
			if (keyword == "v" || keyword == "vn") {
				glm::vec3 value(0.0f);
				for (int i = 0; i < 3; ++i) {
					if (!ParseFloat(NextToken(line), value[i])) return fail("expected three numbers after '" + std::string(keyword) + "'");
				}
				(keyword == "v" ? positions : normals).push_back(value);
			}
			else if (keyword == "vt") {
				glm::vec2 value(0.0f);
				if (!ParseFloat(NextToken(line), value.x)) return fail("expected a number after 'vt'");
				if (const std::string_view v = NextToken(line); !v.empty() && !ParseFloat(v, value.y)) return fail("invalid 'vt' value");
				uvs.push_back(value);
			}
			else if (keyword == "f") {
				polygon.clear();
				for (std::string_view token = NextToken(line); !token.empty(); token = NextToken(line)) {
					const size_t slash = token.find('/');
					std::string_view uvToken;
					std::string_view normalToken;
					if (slash != std::string_view::npos) {
						const std::string_view rest = token.substr(slash + 1);
						const size_t secondSlash = rest.find('/');
						uvToken = rest.substr(0, secondSlash);
						if (secondSlash != std::string_view::npos) normalToken = rest.substr(secondSlash + 1);
					}
					const Corner corner{ ResolveIndex(token.substr(0, slash), positions.size()),
						ResolveIndex(uvToken, uvs.size()), ResolveIndex(normalToken, normals.size()) };
					if (corner.Position < 0 || corner.Uv == k_BadIndex || corner.Normal == k_BadIndex) {
						return fail("face index '" + std::string(token) + "' is out of range");
					}

					auto [it, inserted] = vertexByCorner.try_emplace(corner, static_cast<uint32_t>(outMesh.Vertices.size()));
					if (inserted) {
						if (outMesh.Vertices.size() >= std::numeric_limits<uint32_t>::max()) return fail("too many vertices");
						const glm::vec3& p = positions[static_cast<size_t>(corner.Position)];
						const glm::vec3 n = corner.Normal >= 0 ? normals[static_cast<size_t>(corner.Normal)] : glm::vec3(0.0f);
						const glm::vec2 uv = corner.Uv >= 0 ? uvs[static_cast<size_t>(corner.Uv)] : glm::vec2(0.0f);
						outMesh.Vertices.push_back(MeshVertex{ { p.x, p.y, p.z }, { n.x, n.y, n.z }, { uv.x, 1.0f - uv.y } });
						vertexPosition.push_back(corner.Position);
						vertexNeedsNormal.push_back(corner.Normal < 0 ? 1 : 0);
						anyMissingNormal |= corner.Normal < 0;
					}
					polygon.push_back(it->second);
				}
				if (polygon.size() < 3) return fail("a face needs at least three vertices");
				for (size_t i = 1; i + 1 < polygon.size(); ++i) {
					outMesh.Indices.insert(outMesh.Indices.end(), { polygon[0], polygon[i], polygon[i + 1] });
				}
			}
			// o, g, s, usemtl, mtllib, l, p, ... don't change the triangle geometry.
		}

		if (outMesh.Indices.empty()) {
			if (outError) *outError = "the file contains no faces";
			outMesh = {};
			return false;
		}

		if (anyMissingNormal) {
			std::vector<glm::vec3> accumulated(positions.size(), glm::vec3(0.0f));
			for (size_t i = 0; i + 2 < outMesh.Indices.size(); i += 3) {
				const uint32_t corners[3] = { outMesh.Indices[i], outMesh.Indices[i + 1], outMesh.Indices[i + 2] };
				glm::vec3 p[3];
				for (int k = 0; k < 3; ++k) {
					p[k] = positions[static_cast<size_t>(vertexPosition[corners[k]])];
				}
				// Unnormalized cross product: larger faces weigh more.
				const glm::vec3 faceNormal = glm::cross(p[1] - p[0], p[2] - p[0]);
				for (uint32_t corner : corners) accumulated[static_cast<size_t>(vertexPosition[corner])] += faceNormal;
			}
			for (size_t v = 0; v < outMesh.Vertices.size(); ++v) {
				if (!vertexNeedsNormal[v]) continue;
				const glm::vec3 sum = accumulated[static_cast<size_t>(vertexPosition[v])];
				const float length = glm::length(sum);
				const glm::vec3 n = length > 1e-12f ? sum / length : glm::vec3(0.0f, 1.0f, 0.0f);
				MeshVertex& vertex = outMesh.Vertices[v];
				vertex.Normal[0] = n.x;
				vertex.Normal[1] = n.y;
				vertex.Normal[2] = n.z;
			}
		}
		return true;
	}

	void RecalculateNormals(PrimitiveMeshData& mesh) {
		std::vector<glm::vec3> accumulated(mesh.Vertices.size(), glm::vec3(0.0f));
		for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3) {
			const uint32_t corners[3] = { mesh.Indices[i], mesh.Indices[i + 1], mesh.Indices[i + 2] };
			if (corners[0] >= mesh.Vertices.size() || corners[1] >= mesh.Vertices.size() || corners[2] >= mesh.Vertices.size()) continue;
			glm::vec3 p[3];
			for (int k = 0; k < 3; ++k) {
				const MeshVertex& v = mesh.Vertices[corners[k]];
				p[k] = glm::vec3(v.Position[0], v.Position[1], v.Position[2]);
			}
			// Unnormalized cross product: larger faces weigh more.
			const glm::vec3 faceNormal = glm::cross(p[1] - p[0], p[2] - p[0]);
			for (uint32_t corner : corners) accumulated[corner] += faceNormal;
		}
		for (size_t v = 0; v < mesh.Vertices.size(); ++v) {
			const float length = glm::length(accumulated[v]);
			const glm::vec3 n = length > 1e-12f ? accumulated[v] / length : glm::vec3(0.0f, 1.0f, 0.0f);
			mesh.Vertices[v].Normal[0] = n.x;
			mesh.Vertices[v].Normal[1] = n.y;
			mesh.Vertices[v].Normal[2] = n.z;
		}
	}

	const MeshAsset* MeshLibrary::Get(uint64_t guid) {
		if (guid == 0) return nullptr;
		auto& entries = Entries();
		auto it = entries.find(guid);
		if (it != entries.end() && it->second.Runtime) {
			return it->second.Valid ? &it->second.Value : nullptr;
		}
		if (IsRuntimeMeshGuid(guid)) return nullptr;
		if (it == entries.end()) {
			it = entries.emplace(guid, Entry{}).first;
			LoadEntry(guid, it->second);
		}
		else if (!it->second.Valid && std::chrono::steady_clock::now() - it->second.LastAttempt > std::chrono::seconds(1)) {
			LoadEntry(guid, it->second);
		}
		it->second.LastUsed = std::chrono::steady_clock::now();
		it->second.UsedSincePurge = true;
		return it->second.Valid ? &it->second.Value : nullptr;
	}

	const MeshAsset* MeshLibrary::FindLoaded(uint64_t guid) {
		const auto it = Entries().find(guid);
		if (it == Entries().end() || !it->second.Valid) return nullptr;
		it->second.LastUsed = std::chrono::steady_clock::now();
		it->second.UsedSincePurge = true;
		return &it->second.Value;
	}

	const MeshAsset* MeshLibrary::StoreLoadedAsset(uint64_t guid, const std::string& path, MeshAsset&& asset) {
		if (guid == 0 || IsRuntimeMeshGuid(guid) || path.empty()) return nullptr;
		if (const MeshAsset* existing = FindLoaded(guid)) return existing;
		if (asset.Data.Vertices.empty() || asset.Data.Indices.empty()) return nullptr;
		for (uint32_t index : asset.Data.Indices) {
			if (index >= asset.Data.Vertices.size()) return nullptr;
		}

		Entry& entry = Entries()[guid];
		entry = Entry{};
		entry.Path = path;
		entry.Dependencies = std::move(asset.SourceDependencies);
		if (entry.Dependencies.empty()) entry.Dependencies[path] = FileStamp::Read(path);
		entry.Value = std::move(asset);
		UpdateBounds(entry.Value);
		entry.Value.Revision = NextRevision();
		entry.Valid = true;
		entry.LastAttempt = entry.LastUsed = std::chrono::steady_clock::now();
		entry.UsedSincePurge = true;
		return &entry.Value;
	}

	bool IsMeshFileExtension(std::string_view extension) {
		return extension == k_ObjMeshExtension || extension == k_GltfMeshExtension || extension == k_GlbMeshExtension;
	}

	bool MeshLibrary::LoadFromFile(const std::string& path, PrimitiveMeshData& outMesh) {
		MeshAsset asset;
		if (!LoadAssetFromFile(path, asset)) return false;
		outMesh = std::move(asset.Data);
		return true;
	}

	bool MeshLibrary::LoadAssetFromFile(const std::string& path, MeshAsset& outAsset) {
		std::unordered_map<std::string, FileStamp> dependencies;
		const bool loaded = LoadMeshAsset(path, outAsset, &dependencies);
		outAsset.SourceDependencies = std::move(dependencies);
		return loaded;
	}

	namespace {
		bool LoadMeshAsset(const std::string& path, MeshAsset& outAsset, std::unordered_map<std::string, FileStamp>* dependencies) {
			outAsset = MeshAsset{};
			if (dependencies) (*dependencies)[path] = FileStamp::Read(path);
			if (!File::Exists(path)) return false;
			// Recorded even when parsing fails, so fixing the file on disk triggers a reload.
			KnownFileStamps::Record(path);
			std::string error;
			const std::filesystem::path nativePath = Path::FromUtf8(path);
			std::string extension = Path::ToUtf8(nativePath.extension());
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			bool loaded = false;
			if (extension == k_GltfMeshExtension || extension == k_GlbMeshExtension) {
				const std::vector<uint8_t> bytes = File::ReadAllBytes(path);
				const std::filesystem::path directory = nativePath.parent_path();
				loaded = ParseGltf(bytes, [&](const std::string& uri, std::vector<uint8_t>& out) {
					const std::string external = Path::ToUtf8((directory / Path::FromUtf8(uri)).lexically_normal());
					if (dependencies) (*dependencies)[external] = FileStamp::Read(external);
					if (!File::Exists(external)) return false;
					out = File::ReadAllBytes(external);
					KnownFileStamps::Record(external);
					return true;
				}, outAsset, &error);
			}
			else {
				loaded = ParseObjMesh(File::ReadAllText(path), outAsset.Data, &error);
			}
			if (!loaded) {
				FNK_CORE_WARN_TAG("Mesh", "Could not load mesh '{}': {}", path, error);
				return false;
			}
			return true;
		}
	}

	void MeshLibrary::Invalidate(uint64_t guid) {
		if (IsRuntimeMeshGuid(guid)) return;
		Entries().erase(guid);
	}

	size_t MeshLibrary::ReloadChangedFromDisk() {
		size_t reloaded = 0;
		for (auto& [guid, entry] : Entries()) {
			if (entry.Path.empty() || !std::any_of(entry.Dependencies.begin(), entry.Dependencies.end(), [](const auto& dependency) {
				return FileStamp::Read(dependency.first) != dependency.second;
			})) continue;
			LoadEntry(guid, entry);
			++reloaded;
		}
		return reloaded;
	}

	size_t MeshLibrary::PurgeUnused(std::chrono::milliseconds maxIdle) {
		const auto now = std::chrono::steady_clock::now();
		auto& entries = Entries();
		const size_t before = entries.size();
		for (auto it = entries.begin(); it != entries.end();) {
			Entry& entry = it->second;
			const bool used = std::exchange(entry.UsedSincePurge, false);
			if (!entry.Runtime && !used && now - entry.LastUsed >= maxIdle) {
				it = entries.erase(it);
			}
			else {
				++it;
			}
		}
		return before - entries.size();
	}

	void MeshLibrary::Clear() {
		AssetLoader::Invalidate(AssetKind::Mesh);
		std::erase_if(Entries(), [](const auto& pair) { return !pair.second.Runtime; });
	}

	uint64_t MeshLibrary::CreateRuntime() {
		static uint64_t s_NextRuntimeId = 0;
		const uint64_t guid = k_RuntimeMeshGuidBase | (++s_NextRuntimeId & 0x0000FFFFFFFFFFFFULL);
		Entry& entry = Entries()[guid];
		entry = Entry{};
		entry.Runtime = true;
		entry.Value.Revision = NextRevision();
		return guid;
	}

	bool MeshLibrary::SetRuntimeData(uint64_t guid, PrimitiveMeshData&& data) {
		auto it = Entries().find(guid);
		if (it == Entries().end() || !it->second.Runtime) return false;
		const size_t vertexCount = data.Vertices.size();
		for (uint32_t index : data.Indices) {
			if (index >= vertexCount) return false;
		}
		Entry& entry = it->second;
		entry.Value.Data = std::move(data);
		entry.Value.Data.Indices.resize(entry.Value.Data.Indices.size() / 3 * 3);
		UpdateBounds(entry.Value);
		entry.Value.Revision = NextRevision();
		entry.Valid = !entry.Value.Data.Indices.empty();
		return true;
	}

	bool MeshLibrary::DestroyRuntime(uint64_t guid) {
		auto it = Entries().find(guid);
		if (it == Entries().end() || !it->second.Runtime) return false;
		Entries().erase(it);
		return true;
	}

	void MeshLibrary::DestroyAllRuntime() {
		for (auto it = Entries().begin(); it != Entries().end();) {
			it = it->second.Runtime ? Entries().erase(it) : std::next(it);
		}
	}

	bool MeshLibrary::IsLiveRuntime(uint64_t guid) {
		auto it = Entries().find(guid);
		return it != Entries().end() && it->second.Runtime;
	}

	size_t MeshLibrary::RuntimeCount() {
		return static_cast<size_t>(std::count_if(Entries().begin(), Entries().end(), [](const auto& pair) { return pair.second.Runtime; }));
	}

}
