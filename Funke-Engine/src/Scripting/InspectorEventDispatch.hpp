#pragma once

#include "Components/UI/InspectorEventBinding.hpp"
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"

#include <string>
#include <vector>

namespace Funke {

	class Scene;

	namespace InspectorEvents {

		FUNKE_API bool Fire(Scene& scene, EntityHandle ownerEntity,
			const InspectorEventBinding& binding);

		FUNKE_API int FireAll(Scene& scene, EntityHandle ownerEntity,
			const std::vector<InspectorEventBinding>& bindings);

		// Dynamic-arg overload for widgets (Slider, InputField) that supply a live value at dispatch time.
		// Kind-matching bindings get the live value; mismatched bindings keep their authored static value.
		struct DynamicArg {
			InspectorEventArgKind Kind = InspectorEventArgKind::Void;
			std::string Encoded; // same wire format as InspectorEventBinding::ArgumentValue
		};

		FUNKE_API int FireAllWithDynamicArg(Scene& scene, EntityHandle ownerEntity,
			const std::vector<InspectorEventBinding>& bindings,
			const DynamicArg& dynamicArg);

		// Cleared whenever the user assembly reloads — stale class /
		// method names from the previous load shouldn't suppress fresh
		// warnings after the reload.
		FUNKE_API void ResetMissingMethodLog();

		FUNKE_API std::vector<std::string> GetInvokableMethods(const std::string& className);

	}

}
