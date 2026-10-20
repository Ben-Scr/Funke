#pragma once

// Internal to the BuiltInComponentRegistration*.cpp translation units; not part of the
// engine's public surface. See BuiltInComponentRegistration.cpp for why registration is
// split across one TU per category, and why the call order there must not change.

#include "Collections/Color.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/UI/InspectorEventBinding.hpp"
#include "Core/UUID.hpp"
#include "Inspector/PropertyRegistration.hpp"
#include "Scene/ComponentRegistrationHelpers.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Json.hpp"

#include <cstdint>
#include <string>

namespace Funke {
	class ComponentRegistry;
}

namespace Funke::BuiltInComponentRegistrationInternal {

	// Tags everything registered while alive with an engine feature module (ComponentInfo::engineModule).
	class ModuleRegistrationScope {
	public:
		ModuleRegistrationScope(ComponentRegistry& registry, EngineModule module)
			: m_Registry(registry), m_Previous(registry.GetRegistrationModule()) {
			registry.SetRegistrationModule(module);
		}
		~ModuleRegistrationScope() { m_Registry.SetRegistrationModule(m_Previous); }
		ModuleRegistrationScope(const ModuleRegistrationScope&) = delete;
		ModuleRegistrationScope& operator=(const ModuleRegistrationScope&) = delete;

	private:
		ComponentRegistry& m_Registry;
		std::optional<EngineModule> m_Previous;
	};

	// One per category, each defined in BuiltInComponentRegistration<Category>.cpp.
	// RegisterBuiltInComponents() calls them in exactly this order.
	void RegisterGeneral(ComponentRegistry& registry);
	void RegisterRendering(ComponentRegistry& registry);
	void RegisterPhysics(ComponentRegistry& registry);
	void RegisterAudio(ComponentRegistry& registry);
	void RegisterUIWidgets(ComponentRegistry& registry);
	void RegisterUILayout(ComponentRegistry& registry);
	// Adds serialization to components the slices registered; assigns no type ids.
	void Attach2DSerialization(ComponentRegistry& registry);
	void Register3D(ComponentRegistry& registry);
	void RegisterPhysics3D(ComponentRegistry& registry);
	void RegisterParticles3D(ComponentRegistry& registry);
	void RegisterAnimation(ComponentRegistry& registry);
	void RegisterSkinnedMesh3D(ComponentRegistry& registry);
	void RegisterLocalization(ComponentRegistry& registry);

	// Inspector labels for an enum property's values in value order ("Always Animate" instead of "AlwaysAnimate").
	inline PropertyDescriptor WithEnumLabels(PropertyDescriptor desc, std::initializer_list<const char*> labels) {
		if (!desc.Metadata.Enum) return desc;
		auto labelled = std::make_shared<EnumDescriptor>(*desc.Metadata.Enum);
		size_t index = 0;
		for (const char* label : labels) {
			if (index < labelled->Options.size()) labelled->Options[index++].Name = label;
		}
		desc.Metadata.Enum = std::move(labelled);
		return desc;
	}

	// JSON codecs and reference-field helpers shared by the Rendering and both UI slices.
	inline Json::Value UIColorToJson(const Color& c) {
		Json::Value v = Json::Value::MakeObject();
		v.AddMember("r", Json::Value(c.r));
		v.AddMember("g", Json::Value(c.g));
		v.AddMember("b", Json::Value(c.b));
		v.AddMember("a", Json::Value(c.a));
		return v;
	}
	inline Color UIColorFromJson(const Json::Value& v, const Color& fallback) {
		Color c = fallback;
		if (!v.IsObject()) return c;
		if (const Json::Value* x = v.FindMember("r")) c.r = static_cast<float>(x->AsDoubleOr(c.r));
		if (const Json::Value* x = v.FindMember("g")) c.g = static_cast<float>(x->AsDoubleOr(c.g));
		if (const Json::Value* x = v.FindMember("b")) c.b = static_cast<float>(x->AsDoubleOr(c.b));
		if (const Json::Value* x = v.FindMember("a")) c.a = static_cast<float>(x->AsDoubleOr(c.a));
		return c;
	}

	// UUIDs encoded as strings to avoid JSON double precision loss.
	inline Json::Value UIUuidToJson(UUID uuid) {
		return Json::Value(std::to_string(static_cast<uint64_t>(uuid)));
	}
	inline UUID UIUuidFromJson(const Json::Value& v, UUID fallback) {
		if (!v.IsString()) return fallback;
		const std::string s = v.AsStringOr("");
		if (s.empty()) return fallback;
		try { return UUID(std::stoull(s)); } catch (...) { return fallback; }
	}

	inline Json::Value UIEntityHandleToJson(const Entity& owner, EntityHandle h) {
		uint64_t id = 0;
		if (h != entt::null) {
			if (const Scene* s = owner.GetScene()) {
				id = s->GetEntityPersistentID(h);
			}
		}
		return UIUuidToJson(UUID(id));
	}
	inline void UIEntityHandleFromJson(Entity& owner, const Json::Value& v, EntityHandle& outRef) {
		const UUID id = UIUuidFromJson(v, UUID(0));
		if (static_cast<uint64_t>(id) == 0) {
			outRef = entt::null;
			return;
		}
		Scene* s = owner.GetScene();
		if (!s) return;

		const uint64_t persistentId = static_cast<uint64_t>(id);

		// During a clone/paste batch the original target still exists, so an immediate resolve would
		// (wrongly) bind to it. Force deferral so resolution runs after the whole subtree — and thus the
		// full original->clone remap — is built; TryResolveEntityRef then retargets to the clone.
		if (!s->IsEntityRefRemapActive()) {
			EntityHandle resolved = entt::null;
			if (s->TryResolveEntityRef(persistentId, resolved)) {
				outRef = resolved;
				return;
			}
		}

		// Forward ref (or forced-deferred clone ref): defer to end of load batch; SceneSerializer drains the queue before returning, by which point all UUIDComponents are registered (and the remap is complete).
		outRef = entt::null;
		EntityHandle* refSlot = &outRef;
		s->DeferEntityRefFixup([s, refSlot, persistentId]() {
			EntityHandle r = entt::null;
			if (s->TryResolveEntityRef(persistentId, r)) {
				*refSlot = r;
			}
		});
	}

	inline Json::Value UIEventListToJson(const InspectorEventList& list) {
		Json::Value array = Json::Value::MakeArray();
		for (const InspectorEventBinding& b : list.Bindings) {
			Json::Value row = Json::Value::MakeObject();
			row.AddMember("entity",  UIUuidToJson(UUID(b.TargetEntityUUID)));
			row.AddMember("class",   Json::Value(b.ScriptClassName));
			row.AddMember("method",  Json::Value(b.MethodName));
			row.AddMember("enabled", Json::Value(b.Enabled));
			row.AddMember("argKind", Json::Value(static_cast<int>(b.ArgumentKind)));
			row.AddMember("argValue", Json::Value(b.ArgumentValue));
			array.Append(std::move(row));
		}
		return array;
	}
	inline void UIEventListFromJson(Entity& owner, const Json::Value& v, InspectorEventList& outList) {
		outList.Bindings.clear();
		if (!v.IsArray()) return;
		for (const Json::Value& row : v.GetArray()) {
			if (!row.IsObject()) continue;
			InspectorEventBinding b;
			if (const Json::Value* x = row.FindMember("entity"))
				b.TargetEntityUUID = static_cast<uint64_t>(UIUuidFromJson(*x, UUID(0)));
			if (const Json::Value* x = row.FindMember("class"))    b.ScriptClassName = x->AsStringOr("");
			if (const Json::Value* x = row.FindMember("method"))   b.MethodName      = x->AsStringOr("");
			if (const Json::Value* x = row.FindMember("enabled"))  b.Enabled         = x->AsBoolOr(true);
			if (const Json::Value* x = row.FindMember("argKind")) {
				int k = x->AsIntOr(0);
				if (k < 0 || k > static_cast<int>(InspectorEventArgKind::EntityRef)) k = 0;
				b.ArgumentKind = static_cast<InspectorEventArgKind>(k);
			}
			if (const Json::Value* x = row.FindMember("argValue")) b.ArgumentValue   = x->AsStringOr("");
			if (!b.ScriptClassName.empty() || !b.MethodName.empty()) {
				outList.Bindings.push_back(std::move(b));
			}
		}

		// Clone/paste: event targets are stored as raw ids and resolved lazily at runtime dispatch (when
		// the remap is gone), so they aren't covered by the UI-ref deferral. Translate the stored ids now,
		// deferred so the full subtree remap exists first. The list lives on a pointer-stable component.
		Scene* s = owner.GetScene();
		if (s && s->IsEntityRefRemapActive()) {
			InspectorEventList* listPtr = &outList;
			s->DeferEntityRefFixup([s, listPtr]() {
				for (InspectorEventBinding& b : listPtr->Bindings) {
					if (b.TargetEntityUUID != 0) {
						b.TargetEntityUUID = s->TranslateEntityRef(b.TargetEntityUUID);
					}
					if (b.ArgumentKind == InspectorEventArgKind::EntityRef && !b.ArgumentValue.empty()) {
						try {
							const uint64_t argId = std::stoull(b.ArgumentValue);
							if (argId != 0) b.ArgumentValue = std::to_string(s->TranslateEntityRef(argId));
						} catch (...) {}
					}
				}
			});
		}
	}

	// Copies existing ImageComponent.Color into NormalColor so adding a widget to an already-styled image doesn't silently stamp white over it.
	template <typename TComponent>
	void InheritImageColorIntoNormal(Entity e) {
		if (!e.HasComponent<ImageComponent>() || !e.HasComponent<TComponent>()) {
			return;
		}
		const Color imageColor = e.GetComponent<ImageComponent>().Color;
		e.GetComponent<TComponent>().NormalColor = imageColor;
	}

	template <typename T>
	PropertyDescriptor MakeUiEntityRef(const std::string& name, const std::string& displayName,
		EntityHandle T::* memberPtr)
	{
		return Properties::MakeEntityRef(name, displayName,
			[memberPtr](const Entity& e) -> uint64_t {
				EntityHandle h = e.GetComponent<T>().*memberPtr;
				if (h == entt::null) return 0;
				const Scene* s = e.GetScene();
				if (!s) return 0;
				return s->GetEntityPersistentID(h);
			},
			[memberPtr](Entity& e, uint64_t persistentId) {
				auto& ref = e.GetComponent<T>().*memberPtr;
				if (persistentId == 0) {
					ref = entt::null;
					return;
				}
				if (Scene* s = e.GetScene()) {
					EntityHandle resolved = entt::null;
					if (s->TryResolveEntityRef(persistentId, resolved)) {
						ref = resolved;
					}
				}
			});
	}

	template <typename T>
	PropertyDescriptor MakeUiSpriteRef(const std::string& name, const std::string& displayName,
		UUID T::* memberPtr)
	{
		return Properties::MakeTextureRef(name, displayName,
			[memberPtr](const Entity& e) -> uint64_t {
				return static_cast<uint64_t>(e.GetComponent<T>().*memberPtr);
			},
			[memberPtr](Entity& e, uint64_t uuid) {
				e.GetComponent<T>().*memberPtr = UUID(uuid);
			});
	}


} // namespace Funke::BuiltInComponentRegistrationInternal
