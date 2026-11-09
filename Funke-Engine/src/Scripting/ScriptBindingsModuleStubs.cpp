#include "pch.hpp"
#include "Scripting/ScriptBindingModuleSlots.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptGlue.hpp"

#include <atomic>
#include <algorithm>
#include <cstring>
#include <type_traits>

// Signature-matched stand-ins for the bindings of an inactive engine module. A generic void() stub
// would work on x64 but traps on wasm, where call_indirect checks the exact signature.
namespace Funke {
	void Populate2DBindings(NativeBindings& b);
	void PopulatePhysics2DBindings(NativeBindings& b);
	void Populate3DBindings(NativeBindings& b);
	void PopulatePhysics3DBindings(NativeBindings& b);

	namespace {
		std::atomic<bool> s_Reported[std::size(k_AllEngineModules)];

		void ReportInactiveModuleCall(EngineModule module) {
			if (s_Reported[static_cast<size_t>(module)].exchange(true)) return;
			const std::string_view id = EngineModules::GetId(module);
			if (EngineModules::IsCompiled(module)) {
				FNK_CORE_ERROR_TAG("Scripting", "A script used the {} API, but the project does not enable the {} module "
					"(Project Settings > Modules). The call does nothing.", id, id);
			}
			else {
				FNK_CORE_ERROR_TAG("Scripting", "A script used the {} API, but this build was compiled without the {} module. "
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
#define FNK_ASSIGN_STUB(slot) AssignStub<EngineModule::TwoD>(b.slot);
			FNK_FOR_EACH_2D_BINDING(FNK_ASSIGN_STUB)
#undef FNK_ASSIGN_STUB
		}

		void Stub3D(NativeBindings& b) {
#define FNK_ASSIGN_STUB(slot) AssignStub<EngineModule::ThreeD>(b.slot);
			FNK_FOR_EACH_3D_BINDING(FNK_ASSIGN_STUB)
#undef FNK_ASSIGN_STUB
		}

		void StubPhysics2D(NativeBindings& b) {
#define FNK_ASSIGN_STUB(slot) AssignStub<EngineModule::Physics2D>(b.slot);
			FNK_FOR_EACH_PHYSICS2D_BINDING(FNK_ASSIGN_STUB)
#undef FNK_ASSIGN_STUB
			b.Rigidbody2D_GetLinearVelocity = [](uint64_t, float* outX, float* outY) {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (outX) *outX = {};
				if (outY) *outY = {};
			};
			b.BoxCollider2D_GetScale = [](uint64_t, float* outX, float* outY) {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (outX) *outX = {};
				if (outY) *outY = {};
			};
			b.BoxCollider2D_GetCenter = [](uint64_t, float* outX, float* outY) {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (outX) *outX = {};
				if (outY) *outY = {};
			};
			b.CircleCollider2D_GetCenter = [](uint64_t, float* outX, float* outY) {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (outX) *outX = {};
				if (outY) *outY = {};
			};
			b.PolygonCollider2D_GetWorldPoints = [](uint64_t, float* outPoints, int maxOut) -> int {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (outPoints && maxOut > 0) std::fill_n(outPoints, maxOut, 0.0f);
				return {};
			};
			b.PolygonCollider2D_GetCenter = [](uint64_t, float* outX, float* outY) {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (outX) *outX = {};
				if (outY) *outY = {};
			};
			b.PolygonCollider2D_GetSize = [](uint64_t, float* outX, float* outY) {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (outX) *outX = {};
				if (outY) *outY = {};
			};
			b.Physics2D_Raycast = [](float, float, float, float, float, uint64_t* hitEntityID, float* hitX, float* hitY, float* hitNormalX, float* hitNormalY, float* hitDistance) -> int {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (hitEntityID) *hitEntityID = {};
				if (hitX) *hitX = {};
				if (hitY) *hitY = {};
				if (hitNormalX) *hitNormalX = {};
				if (hitNormalY) *hitNormalY = {};
				if (hitDistance) *hitDistance = {};
				return {};
			};
			b.Physics2D_OverlapCircle = [](float, float, float, int, uint64_t* entityID) -> int {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (entityID) *entityID = {};
				return {};
			};
			b.Physics2D_OverlapBox = [](float, float, float, float, float, int, uint64_t* entityID) -> int {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (entityID) *entityID = {};
				return {};
			};
			b.Physics2D_OverlapPolygon = [](float, float, const float*, int, int, uint64_t* entityID) -> int {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (entityID) *entityID = {};
				return {};
			};
			b.Physics2D_OverlapCircleAll = [](float, float, float, uint64_t* outEntityIDs, int maxOut) -> int {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (outEntityIDs && maxOut > 0) std::fill_n(outEntityIDs, maxOut, uint64_t{});
				return {};
			};
			b.Physics2D_OverlapBoxAll = [](float, float, float, float, float, uint64_t* outEntityIDs, int maxOut) -> int {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (outEntityIDs && maxOut > 0) std::fill_n(outEntityIDs, maxOut, uint64_t{});
				return {};
			};
			b.Physics2D_OverlapPolygonAll = [](float, float, const float*, int, uint64_t* outEntityIDs, int maxOut) -> int {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (outEntityIDs && maxOut > 0) std::fill_n(outEntityIDs, maxOut, uint64_t{});
				return {};
			};
			b.Physics2D_ContainsPoint = [](float, float, int, uint64_t* entityID) -> int {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (entityID) *entityID = {};
				return {};
			};
			b.Physics2D_ContainsPointAll = [](float, float, uint64_t* outEntityIDs, int maxOut) -> int {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (outEntityIDs && maxOut > 0) std::fill_n(outEntityIDs, maxOut, uint64_t{});
				return {};
			};
			b.Joint2D_Get = [](uint64_t, int, float* values, int count) -> int {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (values && count > 0) std::fill_n(values, count, 0.0f);
				return {};
			};
			b.Joint2D_GetState = [](uint64_t, int, float* values, int count) {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (values && count > 0) std::fill_n(values, count, 0.0f);
			};
			b.PolygonCollider2D_GetPath = [](uint64_t, int, float* outPoints, int maxPoints) -> int {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (outPoints && maxPoints > 0) std::fill_n(outPoints, static_cast<size_t>(maxPoints) * 2, 0.0f);
				return {};
			};
			b.Physics2D_GetGravity = [](float* outX, float* outY) {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (outX) *outX = {};
				if (outY) *outY = {};
			};
			b.Rigidbody2D_GetDynamics = [](uint64_t, float* values, int count) {
				ReportInactiveModuleCall(EngineModule::Physics2D);
				if (values && count > 0) std::fill_n(values, count, 0.0f);
			};
		}

		void StubPhysics3D(NativeBindings& b) {
#define FNK_ASSIGN_STUB(slot) AssignStub<EngineModule::Physics3D>(b.slot);
			FNK_FOR_EACH_PHYSICS3D_BINDING(FNK_ASSIGN_STUB)
#undef FNK_ASSIGN_STUB
			b.Rigidbody3D_Get = [](uint64_t, float* values, int count) {
				ReportInactiveModuleCall(EngineModule::Physics3D);
				if (values && count > 0) std::fill_n(values, count, 0.0f);
			};
			b.Rigidbody3D_GetVelocity = [](uint64_t, int, float* x, float* y, float* z) {
				ReportInactiveModuleCall(EngineModule::Physics3D);
				if (x) *x = {};
				if (y) *y = {};
				if (z) *z = {};
			};
			b.Collider3D_Get = [](uint64_t, int, float* values, int count) {
				ReportInactiveModuleCall(EngineModule::Physics3D);
				if (values && count > 0) std::fill_n(values, count, 0.0f);
			};
			b.Physics3D_Raycast = [](float, float, float, float, float, float, float, uint64_t* hitEntityID, float* hitValues, int count) -> int {
				ReportInactiveModuleCall(EngineModule::Physics3D);
				if (hitEntityID) *hitEntityID = {};
				if (hitValues && count > 0) std::fill_n(hitValues, count, 0.0f);
				return {};
			};
			b.Physics3D_GetGravity = [](float* x, float* y, float* z) {
				ReportInactiveModuleCall(EngineModule::Physics3D);
				if (x) *x = {};
				if (y) *y = {};
				if (z) *z = {};
			};
		}

		constexpr size_t k_SlotCount = sizeof(NativeBindings) / sizeof(void*);
		static_assert(sizeof(NativeBindings) % sizeof(void*) == 0, "NativeBindings must hold only function pointers");
	}

	void ScriptBindings::PopulateModuleBindings(NativeBindings& b) {
#if FUNKE_WITH_2D
		if (EngineModules::IsActive(EngineModule::TwoD)) Populate2DBindings(b);
		else Stub2D(b);
#else
		Stub2D(b);
#endif
#if FUNKE_WITH_3D
		if (EngineModules::IsActive(EngineModule::ThreeD)) Populate3DBindings(b);
		else Stub3D(b);
#else
		Stub3D(b);
#endif
#if FUNKE_WITH_PHYSICS2D
		if (EngineModules::IsActive(EngineModule::Physics2D)) PopulatePhysics2DBindings(b);
		else StubPhysics2D(b);
#else
		StubPhysics2D(b);
#endif
#if FUNKE_WITH_PHYSICS3D
		if (EngineModules::IsActive(EngineModule::Physics3D)) PopulatePhysics3DBindings(b);
		else StubPhysics3D(b);
#else
		StubPhysics3D(b);
#endif
	}

	std::vector<size_t> ScriptBindings::FindUnstubbedModuleSlots(EngineModule module) {
		std::vector<size_t> missing;
		if (!EngineModules::IsCompiled(module)) return missing;
		NativeBindings populated{};
		NativeBindings stubbed{};
		switch (module) {
			case EngineModule::TwoD:
#if FUNKE_WITH_2D
				Populate2DBindings(populated);
#endif
				Stub2D(stubbed);
				break;
			case EngineModule::ThreeD:
#if FUNKE_WITH_3D
				Populate3DBindings(populated);
#endif
				Stub3D(stubbed);
				break;
			case EngineModule::Physics2D:
#if FUNKE_WITH_PHYSICS2D
				PopulatePhysics2DBindings(populated);
#endif
				StubPhysics2D(stubbed);
				break;
			case EngineModule::Physics3D:
#if FUNKE_WITH_PHYSICS3D
				PopulatePhysics3DBindings(populated);
#endif
				StubPhysics3D(stubbed);
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
