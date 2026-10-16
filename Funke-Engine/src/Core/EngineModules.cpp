#include "pch.hpp"
#include "Core/EngineModules.hpp"

#include <atomic>

namespace Funke {

	namespace {
		std::atomic<bool> s_Active[std::size(k_AllEngineModules)] = { FUNKE_WITH_2D != 0, FUNKE_WITH_3D != 0 };

		size_t IndexOf(EngineModule module) {
			return static_cast<size_t>(module);
		}
	}

	std::string_view EngineModules::GetId(EngineModule module) {
		switch (module) {
			case EngineModule::TwoD: return k_Id2D;
			case EngineModule::ThreeD: return k_Id3D;
		}
		return {};
	}

	std::string_view EngineModules::GetOption(EngineModule module) {
		switch (module) {
			case EngineModule::TwoD: return "2d";
			case EngineModule::ThreeD: return "3d";
		}
		return {};
	}

	std::string_view EngineModules::GetDescription(EngineModule module) {
		switch (module) {
			case EngineModule::TwoD:
				return "2D worlds: Transform 2D, sprites and sprite animation, 2D cameras, particles and post-processing, "
					"2D physics (Box2D).";
			case EngineModule::ThreeD:
				return "3D worlds: Transform 3D, meshes and materials, lights and sky, 3D particles, 3D physics (Jolt).";
		}
		return {};
	}

	std::optional<EngineModule> EngineModules::FromId(std::string_view id) {
		for (EngineModule module : k_AllEngineModules) {
			if (GetId(module) == id) return module;
		}
		return std::nullopt;
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
		for (EngineModule module : k_AllEngineModules) {
			bool listed = false;
			for (const std::string& id : moduleIds) {
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
		for (const std::string& id : moduleIds) {
			const std::optional<EngineModule> module = FromId(id);
			if (module && !IsCompiled(*module)) missing.push_back(id);
		}
		return missing;
	}

}
