#include "pch.hpp"
#include "Core/EngineModules.hpp"

#include <algorithm>
#include <atomic>

namespace Funke {

	namespace {
		std::atomic<bool> s_Active[std::size(k_AllEngineModules)] = { FUNKE_WITH_2D != 0, FUNKE_WITH_3D != 0, FUNKE_WITH_PHYSICS2D != 0, FUNKE_WITH_PHYSICS3D != 0 };

		size_t IndexOf(EngineModule module) {
			return static_cast<size_t>(module);
		}
	}

	std::string_view EngineModules::GetId(EngineModule module) {
		switch (module) {
			case EngineModule::TwoD: return k_Id2D;
			case EngineModule::ThreeD: return k_Id3D;
			case EngineModule::Physics2D: return k_IdPhysics2D;
			case EngineModule::Physics3D: return k_IdPhysics3D;
		}
		return {};
	}

	std::string_view EngineModules::GetOption(EngineModule module) {
		switch (module) {
			case EngineModule::TwoD: return "2d";
			case EngineModule::ThreeD: return "3d";
			case EngineModule::Physics2D: return "physics2d";
			case EngineModule::Physics3D: return "physics3d";
		}
		return {};
	}

	std::string_view EngineModules::GetDescription(EngineModule module) {
		switch (module) {
			case EngineModule::TwoD:
				return "2D worlds: Transform 2D, sprites and sprite animation, 2D cameras, particles and post-processing.";
			case EngineModule::ThreeD:
				return "3D worlds: Transform 3D, meshes and materials, lights and sky, 3D particles.";
			case EngineModule::Physics2D: return "2D physics (Box2D): bodies, colliders, joints and queries. Requires Funke.2D.";
			case EngineModule::Physics3D: return "3D physics (Jolt): bodies, colliders and queries. Requires Funke.3D.";
		}
		return {};
	}

	std::optional<EngineModule> EngineModules::FromId(std::string_view id) {
		for (EngineModule module : k_AllEngineModules) {
			if (GetId(module) == id) return module;
		}
		return std::nullopt;
	}

	std::optional<EngineModule> EngineModules::GetSerializedPhysicsModule(std::string_view component) {
		for (const auto name : { "Rigidbody2D", "BoxCollider2D", "CircleCollider2D", "PolygonCollider2D",
			"RevoluteJoint2D", "PrismaticJoint2D", "WeldJoint2D" }) {
			if (component == name) return EngineModule::Physics2D;
		}
		for (const auto name : { "Rigidbody3D", "BoxCollider3D", "SphereCollider3D", "CapsuleCollider3D", "MeshCollider3D" }) {
			if (component == name) return EngineModule::Physics3D;
		}
		return std::nullopt;
	}

	std::optional<EngineModule> EngineModules::GetDependency(EngineModule module) {
		if (module == EngineModule::Physics2D) return EngineModule::TwoD;
		if (module == EngineModule::Physics3D) return EngineModule::ThreeD;
		return std::nullopt;
	}

	EngineModule EngineModules::GetDimension(EngineModule module) {
		return GetDependency(module).value_or(module);
	}

	std::vector<std::string> EngineModules::Resolve(const std::vector<std::string>& moduleIds) {
		bool selected[std::size(k_AllEngineModules)]{};
		std::vector<std::string> unknown;
		for (const auto& id : moduleIds) {
			if (const auto module = FromId(id)) {
				selected[IndexOf(*module)] = true;
				if (const auto dependency = GetDependency(*module)) selected[IndexOf(*dependency)] = true;
			}
			else if (std::find(unknown.begin(), unknown.end(), id) == unknown.end()) unknown.push_back(id);
		}
		std::vector<std::string> result;
		for (const auto module : k_AllEngineModules) {
			if (selected[IndexOf(module)]) result.emplace_back(GetId(module));
		}
		result.insert(result.end(), unknown.begin(), unknown.end());
		return result;
	}

	std::vector<std::string> EngineModules::ExpandLegacySelection(const std::vector<std::string>& moduleIds) {
		auto result = moduleIds;
		for (const auto& id : moduleIds) {
			if (id == k_Id2D) result.emplace_back(k_IdPhysics2D);
			if (id == k_Id3D) result.emplace_back(k_IdPhysics3D);
		}
		return Resolve(result);
	}

	EngineModules::Status EngineModules::GetStatus(std::string_view id, const std::vector<std::string>& moduleIds) {
		const std::optional<EngineModule> module = FromId(id);
		if (!module) return Status::Unknown;
		if (!IsCompiled(*module)) return Status::NotCompiled;
		const auto effective = Resolve(moduleIds);
		return std::find(effective.begin(), effective.end(), id) == effective.end() ? Status::Disabled : Status::Enabled;
	}

	std::string_view EngineModules::GetStatusName(Status status) {
		switch (status) {
			case Status::Unknown: return "unknown";
			case Status::NotCompiled: return "not_compiled";
			case Status::Disabled: return "disabled";
			case Status::Enabled: return "enabled";
		}
		return "unknown";
	}

	std::string EngineModules::GetStatusMessage(std::string_view id, Status status) {
		const std::string name(id);
		switch (status) {
			case Status::Unknown:
				return "Unknown engine module '" + name + "'; correct the ID (known: Funke.2D, Funke.3D, Funke.Physics2D, Funke.Physics3D).";
			case Status::NotCompiled:
				return "Engine module '" + name + "' is not compiled into this engine; use a build containing it, "
					"or remove it from project.json modules and dependent content/packages.";
			case Status::Disabled:
				return "Engine module '" + name + "' is disabled in project.json; enable it in Package Manager > Engine Modules "
					"or with 'funke package add " + name + "' if needed.";
			case Status::Enabled:
				return "Engine module '" + name + "' is compiled and enabled in project.json.";
		}
		return {};
	}

	bool EngineModules::IsActive(EngineModule module) {
		return IsCompiled(module) && s_Active[IndexOf(module)].load(std::memory_order_relaxed);
	}

	std::vector<std::string> EngineModules::GetActiveIds() {
		std::vector<std::string> ids;
		for (EngineModule module : k_AllEngineModules) {
			if (IsActive(module)) ids.emplace_back(GetId(module));
		}
		return ids;
	}

	void EngineModules::Configure(const std::vector<std::string>& moduleIds) {
		const auto effective = Resolve(moduleIds);
		for (EngineModule module : k_AllEngineModules) {
			bool listed = false;
			for (const std::string& id : effective) {
				if (id == GetId(module)) {
					listed = true;
					break;
				}
			}
			s_Active[IndexOf(module)].store(IsCompiled(module) && listed, std::memory_order_relaxed);
		}
	}

	void EngineModules::ActivateAllCompiled() {
		for (EngineModule module : k_AllEngineModules) {
			s_Active[IndexOf(module)].store(IsCompiled(module), std::memory_order_relaxed);
		}
	}

	std::vector<std::string> EngineModules::GetMissingModules(const std::vector<std::string>& moduleIds) {
		std::vector<std::string> missing;
		for (const std::string& id : Resolve(moduleIds)) {
			const std::optional<EngineModule> module = FromId(id);
			if (module && !IsCompiled(*module)) missing.push_back(id);
		}
		return missing;
	}

}
