#pragma once

#include "Scene/Entity.hpp"
#include "Core/Exceptions.hpp"

#include <type_traits>
#include <utility>

namespace Funke {

	// Entity-and-type lookup; no component address survives an access. Use on the scene's owning thread.
	template<typename TComponent>
	class ComponentRef {
		static_assert(std::is_object_v<TComponent> && !std::is_volatile_v<TComponent>,
			"ComponentRef requires a non-volatile component object type.");

	public:
		ComponentRef() = default;
		explicit ComponentRef(Entity entity) : m_Entity(std::move(entity)) {}

		// The returned pointer is borrowed until the next structural change; retain the ComponentRef instead.
		[[nodiscard]] TComponent* TryGet() const {
			using Value = std::remove_const_t<TComponent>;
			static_assert(!std::is_empty_v<Value>, "ComponentRef cannot refer to an empty tag; use HasComponent instead.");
			static_assert(!std::is_base_of_v<NativeScript, Value>, "ComponentRef refers to ECS components, not native script instances.");
			Value* component = nullptr;
			m_Entity.TryGetComponent<Value>(component);
			return component;
		}

		[[nodiscard]] TComponent& Get() const {
			if (auto* component = TryGet()) return *component;
			FNK_THROW(FunkeErrorCode::InvalidHandle, "ComponentRef target is unavailable: the component, entity or scene no longer exists.");
		}

		[[nodiscard]] TComponent* operator->() const { return &Get(); }
		[[nodiscard]] TComponent& operator*() const { return Get(); }
		[[nodiscard]] bool IsValid() const { return TryGet() != nullptr; }
		explicit operator bool() const { return IsValid(); }

		void Reset() { m_Entity = Entity::Null; }
		[[nodiscard]] Entity GetEntity() const { return m_Entity; }

	private:
		// Const handles retain pointer-like mutability; ComponentRef<const T> provides read-only access.
		mutable Entity m_Entity = Entity::Null;
	};

	template<typename TComponent>
	ComponentRef<TComponent> Entity::GetComponentRef() {
		return ComponentRef<TComponent>(*this);
	}

	template<typename TComponent>
	ComponentRef<const TComponent> Entity::GetComponentRef() const {
		return ComponentRef<const TComponent>(*this);
	}

}
