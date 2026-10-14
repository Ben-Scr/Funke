#include "pch.hpp"
#include "Scripting/ScriptBindingModuleSlots.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptGlue.hpp"

#include <atomic>
#include <cstring>
#include <type_traits>

// Signature-matched stand-ins for the bindings of an inactive engine module. A generic void() stub
// would work on x64 but traps on wasm, where call_indirect checks the exact signature.
namespace Index {
	void Populate2DBindings(NativeBindings& b);
	void Populate3DBindings(NativeBindings& b);
	void PopulatePhysics3DBindings(NativeBindings& b);

	namespace {
		std::atomic<bool> s_Reported[std::size(k_AllEngineModules)];

		void ReportInactiveModuleCall(EngineModule module) {
			if (s_Reported[static_cast<size_t>(module)].exchange(true)) return;
			const std::string_view id = EngineModules::GetId(module);
			if (EngineModules::IsCompiled(module)) {
				IDX_CORE_ERROR_TAG("Scripting", "A script used the {} API, but the project does not enable the {} module "
					"(Project Settings > Modules). The call does nothing.", id, id);
			}
			else {
				IDX_CORE_ERROR_TAG("Scripting", "A script used the {} API, but this build was compiled without the {} module. "
					"The call does nothing.", id, id);
			}
		}

		template <EngineModule Module, typename R, typename... Args>
		R ModuleStub(Args...) {
			ReportInactiveModuleCall(Module);
			if constexpr (!std::is_void_v<R>) {
				return R{};
			}
		}

		template <EngineModule Module, typename R, typename... Args>
		void AssignStub(R (*&slot)(Args...)) {
			slot = &ModuleStub<Module, R, Args...>;
		}

		void Stub2D(NativeBindings& b) {
#define IDX_ASSIGN_STUB(slot) AssignStub<EngineModule::TwoD>(b.slot);
			IDX_FOR_EACH_2D_BINDING(IDX_ASSIGN_STUB)
#undef IDX_ASSIGN_STUB
		}

		void Stub3D(NativeBindings& b) {
#define IDX_ASSIGN_STUB(slot) AssignStub<EngineModule::ThreeD>(b.slot);
			IDX_FOR_EACH_3D_BINDING(IDX_ASSIGN_STUB)
#undef IDX_ASSIGN_STUB
		}

		constexpr size_t k_SlotCount = sizeof(NativeBindings) / sizeof(void*);
		static_assert(sizeof(NativeBindings) % sizeof(void*) == 0, "NativeBindings must hold only function pointers");
	}

	void ScriptBindings::PopulateModuleBindings(NativeBindings& b) {
#if INDEX_WITH_2D
		if (EngineModules::IsActive(EngineModule::TwoD)) {
			Populate2DBindings(b);
		}
		else {
			Stub2D(b);
		}
#else
		Stub2D(b);
#endif
#if INDEX_WITH_3D
		if (EngineModules::IsActive(EngineModule::ThreeD)) {
			Populate3DBindings(b);
			PopulatePhysics3DBindings(b);
		}
		else {
			Stub3D(b);
		}
#else
		Stub3D(b);
#endif
	}

	std::vector<size_t> ScriptBindings::FindUnstubbedModuleSlots(EngineModule module) {
		std::vector<size_t> missing;
		NativeBindings populated{};
		NativeBindings stubbed{};
		switch (module) {
			case EngineModule::ThreeD:
#if INDEX_WITH_3D
				Populate3DBindings(populated);
				PopulatePhysics3DBindings(populated);
#endif
				Stub3D(stubbed);
				break;
			case EngineModule::TwoD:
#if INDEX_WITH_2D
				Populate2DBindings(populated);
#endif
				Stub2D(stubbed);
				break;
		}

		void* populatedSlots[k_SlotCount];
		void* stubbedSlots[k_SlotCount];
		std::memcpy(populatedSlots, &populated, sizeof(NativeBindings));
		std::memcpy(stubbedSlots, &stubbed, sizeof(NativeBindings));
		for (size_t slot = 0; slot < k_SlotCount; ++slot) {
			if ((populatedSlots[slot] != nullptr) != (stubbedSlots[slot] != nullptr)) {
				missing.push_back(slot);
			}
		}
		return missing;
	}

}
