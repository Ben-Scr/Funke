#pragma once

#include "Core/EngineModules.hpp"
#include "Inspector/PropertyDescriptor.hpp"
#include "Scene/ComponentCategory.hpp"
#include "Scene/Entity.hpp"
#include "Serialization/Json.hpp"
#include "Utils/InlineFunction.hpp"

#include <entt/core/type_info.hpp>
#include <entt/entity/registry.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

namespace Funke::Serialization { class IArchive; }

namespace Funke {

	class DynamicComponentStorage;
	class ComponentRegistry;
	namespace Codegen {
		template <typename T> void RegisterPrefixByteEmplacer(ComponentRegistry&, std::size_t);
	}

	template <typename Signature>
	class NativeComponentCallback;

	// Certification follows the callable, so replacing a registered emplacer disables bulk bypasses.
	template <typename TResult, typename... TArgs>
	class NativeComponentCallback<TResult(TArgs...)> {
	public:
		NativeComponentCallback() noexcept = default;
		NativeComponentCallback(std::nullptr_t) noexcept {}
		NativeComponentCallback(const NativeComponentCallback&) = default;
		NativeComponentCallback& operator=(const NativeComponentCallback&) = default;
		NativeComponentCallback(NativeComponentCallback&& other) noexcept
			: m_Callback(std::move(other.m_Callback)),
			  m_NativeType(std::exchange(other.m_NativeType, typeid(void))),
			  m_NativePrefixSize(std::exchange(other.m_NativePrefixSize, 0)) {}
		NativeComponentCallback& operator=(NativeComponentCallback&& other) noexcept {
			if (this != &other) {
				m_Callback = std::move(other.m_Callback);
				m_NativeType = std::exchange(other.m_NativeType, typeid(void));
				m_NativePrefixSize = std::exchange(other.m_NativePrefixSize, 0);
			}
			return *this;
		}
		template <typename F>
			requires (!std::is_same_v<std::decay_t<F>, NativeComponentCallback>
				&& std::is_invocable_r_v<TResult, std::decay_t<F>&, TArgs...>)
		NativeComponentCallback(F&& callback) : m_Callback(std::forward<F>(callback)) {}
		template <typename F>
			requires (!std::is_same_v<std::decay_t<F>, NativeComponentCallback>
				&& std::is_invocable_r_v<TResult, std::decay_t<F>&, TArgs...>)
		NativeComponentCallback& operator=(F&& callback) {
			m_NativeType = typeid(void);
			m_NativePrefixSize = 0;
			m_Callback = std::forward<F>(callback);
			return *this;
		}
		NativeComponentCallback& operator=(std::nullptr_t) noexcept {
			m_Callback = nullptr;
			m_NativeType = typeid(void);
			m_NativePrefixSize = 0;
			return *this;
		}
		TResult operator()(TArgs... args) const { return m_Callback(std::forward<TArgs>(args)...); }
		explicit operator bool() const noexcept { return static_cast<bool>(m_Callback); }
		friend bool operator==(const NativeComponentCallback& callback, std::nullptr_t) noexcept { return !callback; }
		std::type_index NativeType() const noexcept { return m_Callback ? m_NativeType : typeid(void); }
		std::size_t NativePrefixSize() const noexcept { return m_Callback ? m_NativePrefixSize : 0; }
		template <typename T> bool IsNative() const noexcept { return m_Callback && m_NativeType == typeid(T); }
		template <typename T> bool IsNative(std::size_t prefixSize) const noexcept {
			return IsNative<T>() && m_NativePrefixSize == prefixSize;
		}

	private:
		friend class ComponentRegistry;
		template <typename T> friend void Codegen::RegisterPrefixByteEmplacer(ComponentRegistry&, std::size_t);
		template <typename T, typename F>
		void SetNative(F&& callback, std::size_t prefixSize = 0) {
			m_Callback = std::forward<F>(callback);
			m_NativeType = typeid(T);
			m_NativePrefixSize = prefixSize;
		}
		InlineFunction<TResult(TArgs...)> m_Callback;
		std::type_index m_NativeType{ typeid(void) };
		std::size_t m_NativePrefixSize = 0;
	};

	// Ordinary callback replacement disables only that operation's registry fast path.
	template <typename TResult, typename TRegistry = entt::registry>
	class ComponentCallback {
	public:
		using RegistryCallbackType = TResult (*)(TRegistry&, EntityHandle);

		ComponentCallback() noexcept = default;
		ComponentCallback(std::nullptr_t) noexcept {}
		ComponentCallback(const ComponentCallback&) = default;
		ComponentCallback& operator=(const ComponentCallback&) = default;

		ComponentCallback(ComponentCallback&& other) noexcept
			: m_Callback(std::move(other.m_Callback)),
			  m_RegistryCallback(std::exchange(other.m_RegistryCallback, nullptr)),
			  m_NativeType(std::exchange(other.m_NativeType, typeid(void))) {}

		ComponentCallback& operator=(ComponentCallback&& other) noexcept {
			if (this != &other) {
				m_Callback = std::move(other.m_Callback);
				m_RegistryCallback = std::exchange(other.m_RegistryCallback, nullptr);
				m_NativeType = std::exchange(other.m_NativeType, typeid(void));
			}
			return *this;
		}

		template <typename F>
			requires (!std::is_same_v<std::decay_t<F>, ComponentCallback>
				&& std::is_invocable_r_v<TResult, std::decay_t<F>&, Entity>)
		ComponentCallback(F&& callback) : m_Callback(std::forward<F>(callback)) {}

		template <typename F>
			requires (!std::is_same_v<std::decay_t<F>, ComponentCallback>
				&& std::is_invocable_r_v<TResult, std::decay_t<F>&, Entity>)
		ComponentCallback& operator=(F&& callback) {
			m_RegistryCallback = nullptr;
			m_NativeType = typeid(void);
			m_Callback = std::forward<F>(callback);
			return *this;
		}

		ComponentCallback& operator=(std::nullptr_t) noexcept {
			m_RegistryCallback = nullptr;
			m_NativeType = typeid(void);
			m_Callback = nullptr;
			return *this;
		}

		TResult operator()(Entity entity) const { return m_Callback(std::move(entity)); }
		explicit operator bool() const noexcept { return static_cast<bool>(m_Callback); }
		friend bool operator==(const ComponentCallback& callback, std::nullptr_t) noexcept { return !callback; }

		// Requires a live registry and valid handle; add/remove remain safe for duplicate/missing components.
		RegistryCallbackType RegistryCallback() const noexcept { return m_Callback ? m_RegistryCallback : nullptr; }
		std::type_index NativeType() const noexcept { return m_Callback ? m_NativeType : typeid(void); }
		template <typename T> bool IsNative() const noexcept { return m_Callback && m_NativeType == typeid(T); }

	private:
		friend class ComponentRegistry;
		template <typename T, typename F>
		void SetNative(F&& callback, RegistryCallbackType registryCallback) {
			m_Callback = std::forward<F>(callback);
			m_RegistryCallback = registryCallback;
			m_NativeType = typeid(T);
		}

		InlineFunction<TResult(Entity)> m_Callback;
		RegistryCallbackType m_RegistryCallback = nullptr;
		std::type_index m_NativeType{ typeid(void) };
	};

	struct ComponentInfo {
		std::string displayName;
		std::string serializedName;
		std::string subcategory;
		ComponentCategory category;

		ComponentInfo() = default;
		ComponentInfo(const std::string& displayName, ComponentCategory category)
			: displayName(displayName), category(category) {
		}
		ComponentInfo(const std::string& displayName, const std::string& subcategory, ComponentCategory category)
			: displayName(displayName), subcategory(subcategory), category(category) {
		}

		// Set by ComponentRegistry::Register; lets ComponentInfo* holders re-enter typed APIs without re-iterating the registry.
		std::type_index typeId{ typeid(void) };

		// FNV-1a 64-bit hash of serializedName; stable across builds so it can be embedded in binary assets. Zero = not yet hashed.
		uint64_t serializedNameHash = 0;

		// Dynamic registrations capture their storage; native registrations also supply direct registry callbacks.
		ComponentCallback<bool, const entt::registry> has;
		ComponentCallback<void> add;
		ComponentCallback<void> remove;
		InlineFunction<void(Entity src, Entity dst)> copyTo;

		// Raw component pointer for the ScriptCore ref API (C# reads/writes fields via void* with no P/Invoke). Invalidated by the next add/remove on the pool — do not cache across frames.
		InlineFunction<void*(Entity)> getRaw;

		// Bulk pointer fill for IJobQuery; returns total count so the managed caller can retry with a larger buffer if count > maxRows.
		InlineFunction<int(entt::registry& registry, void** outPointers, int maxRows, int enableFilter)> fillRawPointers;

		// sizeof(T); mismatching the C# struct mirror hard-fails assembly load to prevent silent memory corruption.
		size_t rawSize = 0;

		// Monotonic u32 ID for the ECB wire format (4-byte component name). NOT persisted — scenes key on serializedName. Zero = unassigned.
		uint32_t typeIdU32 = 0;

		// entt::type_hash<T> for registry.storage(id) lookup; enables O(component count) iteration in script bindings, matching typed views.
		entt::id_type storageHash = 0;

		// Non-null for RegisterDynamic components (C# native). registry.storage(storageHash) returns nullptr for them; IJobQuery uses this pointer for O(1) access (~8x faster on 40k entities). Lifetime owned by ComponentRegistry::m_dynamicStorages.
		DynamicComponentStorage* dynamicStorage = nullptr;

		// ECB playback writes the raw payload in one call. Components with scene-bound runtime state (e.g. ParticleSystem2DComponent) must override to rebind, same as copyTo.
		NativeComponentCallback<void(entt::registry& registry, EntityHandle entity,
			const void* bytes, size_t size)> emplaceFromBytes;
		// Exact payload size for preflight validation; zero leaves sizing to a custom emplacer.
		size_t bytePayloadSize = 0;

		// Default-constructs via registry.emplace<T>, firing C++ default-initializers (e.g. Scale={1,1}). MUST NOT route through emplaceFromBytes — managed default(T) is all-zero and would clobber those defaults.
		NativeComponentCallback<void(entt::registry& registry, EntityHandle entity)> defaultEmplace;

		// Appends bytes to `out`; used by PrefabTemplateCache to bake a memcpy-ready blob.
		// InlineFunction (not a raw pointer) so helpers like RegisterPrefixByteEmplacer can capture a
		// prefix size. Components owning non-trivial heap state (std::vector, non-SSO std::string) MUST
		// override — the default memcpy aliases freed memory after the source entity is destroyed.
		InlineFunction<bool(const entt::registry& registry, EntityHandle entity,
			std::vector<uint8_t>& out)> writeBytes;

		// Fired by AddWithDependencies AFTER add; not called from raw add/deserialize/scene-load (those don't want smart defaults).
		void (*onAdd)(Entity) = nullptr;
		// Inspector draw for all selected entities. Prefer populating `properties` (auto-drawer); set this directly only for custom UX. drawInspector wins if both are set.
		void (*drawInspector)(std::span<const Entity>) = nullptr;

		// Per-component viewport gizmo called inside the Editor View gizmo pass. Must be safe to call when sibling components are absent.
		void (*drawEditorGizmo)(Entity) = nullptr;
		// When true, drawEditorGizmo runs for EVERY entity that has the component while
		// editor gizmos are enabled (always-visible, like Camera), not just the selected
		// ones. When false (default) the gizmo is drawn only for selected entities.
		bool drawEditorGizmoAlways = false;

		// Editor icon name from FunkeAssets/Textures/Editor/General, without extension (e.g. "audio_source"). Empty = guessed from displayName.
		std::string icon;

		std::vector<PropertyDescriptor> properties;

		// Implicitly symmetric — declaring on either side is enough; HasConflict checks both directions.
		std::vector<std::type_index> conflictsWith;

		// Directed (not symmetric), walked transitively by AddWithDependencies. Conflict violations are skipped with a warning, never fail the parent add.
		std::vector<std::type_index> dependsOn;

		// Legacy JSON-tree path. SCHEDULED FOR REMOVAL — new components must use serializeArchive instead.
		// Package components must set both to round-trip in .scene/.prefab files.
		InlineFunction<Json::Value(Entity)> serialize;
		InlineFunction<void(Entity, const Json::Value&)> deserialize;
		// Written and read before every other component of the entity, so construct hooks of later ones see it (transforms).
		bool serializeFirst = false;

		// Unified binary+JSON path; wins over the legacy pair when both are set. Bump serializationVersion only for non-additive schema changes.
		void (*serializeArchive)(Entity, Funke::Serialization::IArchive&) = nullptr;
		std::uint16_t serializationVersion = 1;

		// True for RegisterDynamic components (C# user assemblies); UnregisterAllDynamic drops all flagged entries on unload.
		bool isDynamic = false;

		// Non-zero for native package registrations; ComponentRegistry::UnregisterOwner drops them before the package module unloads.
		uint32_t ownerId = 0;

		// Engine feature module that owns the component; the editor hides it while the module is inactive.
		std::optional<EngineModule> engineModule;
		bool IsModuleActive() const { return !engineModule || EngineModules::IsActive(*engineModule); }
	};

} // namespace Funke
