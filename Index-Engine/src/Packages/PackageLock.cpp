#include "pch.hpp"
#include "Packages/PackageLock.hpp"

#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <algorithm>

namespace Index {

	std::string PackageLock::GetPath(const std::string& projectRoot) {
		return Path::Combine(projectRoot, std::string(k_FileName));
	}

	bool PackageLock::TryLoad(const std::string& projectRoot, PackageLock& outLock, std::string* outError) {
		outLock = {};
		const std::string path = GetPath(projectRoot);
		if (!File::Exists(path)) return true;

		Json::Value root;
		std::string parseError;
		if (!Json::TryParse(File::ReadAllText(path), root, &parseError) || !root.IsObject()) {
			if (outError) *outError = path + ": " + (parseError.empty() ? std::string("root is not an object") : parseError);
			return false;
		}
		if (const Json::Value* format = root.FindMember("lockVersion"); format && format->AsIntOr(0) > k_FormatVersion) {
			if (outError) *outError = path + " was written by a newer Index (lockVersion " + std::to_string(format->AsIntOr(0)) + ")";
			return false;
		}
		if (const Json::Value* engine = root.FindMember("engineVersion")) outLock.EngineVersion = engine->AsStringOr();

		if (const Json::Value* packages = root.FindMember("packages"); packages && packages->IsObject()) {
			for (const auto& [name, node] : packages->GetObject()) {
				if (!node.IsObject() || !PackageManifest::IsValidName(name)) continue;
				PackageLockEntry entry;
				entry.Name = name;
				if (const Json::Value* v = node.FindMember("version")) entry.Version = v->AsStringOr();
				if (const Json::Value* v = node.FindMember("source")) entry.Source = v->AsStringOr("unknown");
				if (const Json::Value* v = node.FindMember("url")) entry.Url = v->AsStringOr();
				if (const Json::Value* v = node.FindMember("sha256")) entry.Sha256 = v->AsStringOr();
				if (const Json::Value* v = node.FindMember("dependencies"); v && v->IsArray()) {
					for (const Json::Value& dependency : v->GetArray()) entry.Dependencies.push_back(dependency.AsStringOr());
				}
				outLock.Packages.push_back(std::move(entry));
			}
		}
		std::sort(outLock.Packages.begin(), outLock.Packages.end(),
			[](const PackageLockEntry& a, const PackageLockEntry& b) { return a.Name < b.Name; });
		return true;
	}

	bool PackageLock::Save(const std::string& projectRoot) const {
		Json::Value root = Json::Value::MakeObject();
		root.AddMember("lockVersion", k_FormatVersion);
		root.AddMember("engineVersion", EngineVersion);
		Json::Value packages = Json::Value::MakeObject();
		for (const PackageLockEntry& entry : Packages) {
			Json::Value node = Json::Value::MakeObject();
			node.AddMember("version", entry.Version);
			node.AddMember("source", entry.Source);
			if (!entry.Url.empty()) node.AddMember("url", entry.Url);
			if (!entry.Sha256.empty()) node.AddMember("sha256", entry.Sha256);
			Json::Value dependencies = Json::Value::MakeArray();
			for (const std::string& dependency : entry.Dependencies) dependencies.Append(dependency);
			node.AddMember("dependencies", std::move(dependencies));
			packages.AddMember(entry.Name, std::move(node));
		}
		root.AddMember("packages", std::move(packages));
		return File::WriteAllText(GetPath(projectRoot), Json::Stringify(root, true) + "\n");
	}

	const PackageLockEntry* PackageLock::Find(std::string_view name) const {
		for (const PackageLockEntry& entry : Packages) {
			if (entry.Name == name) return &entry;
		}
		return nullptr;
	}

	void PackageLock::Upsert(PackageLockEntry entry) {
		Remove(entry.Name);
		const auto position = std::lower_bound(Packages.begin(), Packages.end(), entry.Name,
			[](const PackageLockEntry& existing, const std::string& name) { return existing.Name < name; });
		Packages.insert(position, std::move(entry));
	}

	void PackageLock::Remove(std::string_view name) {
		std::erase_if(Packages, [&](const PackageLockEntry& entry) { return entry.Name == name; });
	}

	PackageLock PackageLock::Build(const std::vector<std::string>& resolvedNames, const std::vector<PackageManifest>& manifests,
		const PackageLock& previous, std::string engineVersion) {
		PackageLock lock;
		lock.EngineVersion = std::move(engineVersion);
		for (const std::string& name : resolvedNames) {
			const auto manifest = std::find_if(manifests.begin(), manifests.end(),
				[&](const PackageManifest& candidate) { return candidate.Name == name; });
			if (manifest == manifests.end()) continue;

			PackageLockEntry entry;
			entry.Name = name;
			entry.Version = manifest->VersionText;
			if (const PackageLockEntry* old = previous.Find(name); old && old->Version == entry.Version) {
				entry.Source = old->Source;
				entry.Url = old->Url;
				entry.Sha256 = old->Sha256;
			}
			for (const PackageDependency& dependency : manifest->Dependencies) {
				entry.Dependencies.push_back(dependency.Range == "*" ? dependency.Name : dependency.Name + "@" + dependency.Range);
			}
			lock.Upsert(std::move(entry));
		}
		return lock;
	}

}
