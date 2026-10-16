#pragma once

#include "Core/Export.hpp"
#include "Components/ComponentUtils.hpp"
#if FUNKE_WITH_2D
#include "Components/General/Transform2DComponent.hpp"
#endif
#include "Scene/Scene.hpp"
#include "Scripting/NativeEngineAPI.hpp"
#include "Scripting/NativeScriptRegistry.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace Funke {

	class NativeScript {
	public:
		virtual ~NativeScript() = default;

		virtual void Start() {}
		virtual void Update(float deltaTime) {}
		virtual void FixedUpdate(float fixedDeltaTime) {}
		virtual void OnDestroy() {}

		uint32_t GetEntityID() const { return m_EntityID; }
		EntityHandle GetEntityHandle() const { return m_Entity; }
		Scene* GetScene() const { return m_Scene; }
		entt::registry* GetRegistry() const { return m_Scene ? &m_Scene->GetRegistry() : nullptr; }

		template<typename TComponent, typename... Args>
			requires (!std::is_empty_v<TComponent>)
		TComponent& AddComponent(Args&&... args) {
			entt::registry& registry = RequireValidRegistry("Cannot add component to invalid entity");
			return ComponentUtils::AddComponent<TComponent>(registry, m_Entity, std::forward<Args>(args)...);
		}

		template<typename TTag>
			requires std::is_empty_v<TTag>
		void AddComponent() {
			entt::registry& registry = RequireValidRegistry("Cannot add tag component to invalid entity");
			ComponentUtils::AddComponent<TTag>(registry, m_Entity);
		}

		template<typename TComponent>
		bool HasComponent() const {
			entt::registry* registry = GetRegistry();
			return registry && m_Entity != entt::null && registry->valid(m_Entity)
				&& ComponentUtils::HasComponent<TComponent>(*registry, m_Entity);
		}

		template<typename TComponent>
		TComponent& GetComponent() {
			entt::registry& registry = RequireValidRegistry("Cannot get component from invalid entity");
			return ComponentUtils::GetComponent<TComponent>(registry, m_Entity);
		}

		template<typename TComponent>
		const TComponent& GetComponent() const {
			const entt::registry& registry = RequireValidRegistry("Cannot get component from invalid entity");
			return ComponentUtils::GetComponent<TComponent>(registry, m_Entity);
		}

		template<typename TComponent>
		bool TryGetComponent(TComponent*& out) {
			entt::registry* registry = GetRegistry();
			if (!registry || m_Entity == entt::null || !registry->valid(m_Entity)) {
				out = nullptr;
				return false;
			}

			out = ComponentUtils::TryGetComponent<TComponent>(*registry, m_Entity);
			return out != nullptr;
		}

		template<typename TComponent>
		void RemoveComponent() {
			entt::registry* registry = GetRegistry();
			if (!registry || m_Entity == entt::null || !registry->valid(m_Entity)) {
				return;
			}
			ComponentUtils::RemoveComponent<TComponent>(*registry, m_Entity);
		}

	private:
		entt::registry& RequireValidRegistry(const char* message) {
			entt::registry* registry = GetRegistry();
			if (!registry || m_Entity == entt::null || !registry->valid(m_Entity)) {
				FNK_THROW(FunkeErrorCode::InvalidHandle, message);
			}
			return *registry;
		}

		const entt::registry& RequireValidRegistry(const char* message) const {
			entt::registry* registry = GetRegistry();
			if (!registry || m_Entity == entt::null || !registry->valid(m_Entity)) {
				FNK_THROW(FunkeErrorCode::InvalidHandle, message);
			}
			return *registry;
		}

		friend class NativeScriptHost;
		uint32_t m_EntityID = 0;
		EntityHandle m_Entity = entt::null;
		Scene* m_Scene = nullptr;
	};

} // namespace Funke

#define FNK_NATIVE_LOG_INFO(msg)  do { if (Funke::g_EngineAPI && Funke::g_EngineAPI->LogInfo) Funke::g_EngineAPI->LogInfo(msg); } while(0)
#define FNK_NATIVE_LOG_WARN(msg)  do { if (Funke::g_EngineAPI && Funke::g_EngineAPI->LogWarn) Funke::g_EngineAPI->LogWarn(msg); } while(0)
#define FNK_NATIVE_LOG_ERROR(msg) do { if (Funke::g_EngineAPI && Funke::g_EngineAPI->LogError) Funke::g_EngineAPI->LogError(msg); } while(0)

#define REGISTER_SCRIPT(ClassName) \
	static struct ClassName##_AutoReg { \
		ClassName##_AutoReg() { \
			Funke::NativeScriptRegistry::Register(#ClassName, []() -> Funke::NativeScript* { return new ClassName(); }); \
			Funke::NativeScriptRegistry::RegisterType(typeid(ClassName), #ClassName); \
		} \
	} s_##ClassName##_autoreg;
