#pragma once

#include "Core/Export.hpp"
#include "Components/ComponentUtils.hpp"
#if FUNKE_WITH_2D
#include "Components/General/Transform2DComponent.hpp"
#endif
#include "Scene/Scene.hpp"
#include "Scripting/NativeEngineAPI.hpp"
#include "Scripting/NativeScriptRegistry.hpp"
#include "Scripting/NativeScriptField.hpp"
#include <Utils/StringHelper.hpp>

#include <cstdint>
#include <string>
#include <source_location>
#include <type_traits>
#include <utility>

namespace Funke {

	struct NativePrintValue {
		std::string Text;
		std::source_location Location;
		template <typename T>
		NativePrintValue(T&& value, std::source_location location = std::source_location::current())
			: Text(StringHelper::ToString(std::forward<T>(value))), Location(location) {}
	};

	class NativeScript {
	public:
		virtual ~NativeScript() = default;

		virtual void Start() {}
		virtual void Update() {}
		virtual void FixedUpdate() {}
		virtual void OnDestroy() {}
		virtual const NativeScriptField* GetFields(uint32_t* count) const noexcept {
			if (count) *count = 0;
			return nullptr;
		}

		void Print(std::source_location location = std::source_location::current()) const {
			if (g_EngineAPI && g_EngineAPI->LogInfo) g_EngineAPI->LogInfo("", location.file_name(), static_cast<int>(location.line()));
		}

		template <typename... Args>
		void Print(NativePrintValue first, Args&&... args) const {
			if (g_EngineAPI && g_EngineAPI->LogInfo) g_EngineAPI->LogInfo(StringHelper::ToString(first.Text, std::forward<Args>(args)...).c_str(), first.Location.file_name(), static_cast<int>(first.Location.line()));
		}

		EntityHandle GetEntityHandle() const { return m_Entity; }
		Scene* GetScene() const { return m_Scene; }
		entt::registry* GetRegistry() const { return m_Scene ? &m_Scene->GetRegistry() : nullptr; }

		template<typename TComponent>
		ComponentRef<TComponent> GetComponentRef() {
			const entt::registry* registry = GetRegistry();
			if (!registry || m_Entity == entt::null || !registry->valid(m_Entity)) return {};
			return m_Scene->GetEntity(m_Entity).GetComponentRef<TComponent>();
		}

		template<typename TComponent>
		ComponentRef<const TComponent> GetComponentRef() const {
			const entt::registry* registry = GetRegistry();
			if (!registry || m_Entity == entt::null || !registry->valid(m_Entity)) return {};
			const Entity entity = m_Scene->GetEntity(m_Entity);
			return entity.GetComponentRef<TComponent>();
		}

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
		EntityHandle m_Entity = entt::null;
		Scene* m_Scene = nullptr;
	};

} // namespace Funke

#define FNK_NATIVE_LOG_INFO(msg)  do { if (Funke::g_EngineAPI && Funke::g_EngineAPI->LogInfo) Funke::g_EngineAPI->LogInfo(msg, __FILE__, __LINE__); } while(0)
#define FNK_NATIVE_LOG_WARN(msg)  do { if (Funke::g_EngineAPI && Funke::g_EngineAPI->LogWarn) Funke::g_EngineAPI->LogWarn(msg, __FILE__, __LINE__); } while(0)
#define FNK_NATIVE_LOG_ERROR(msg) do { if (Funke::g_EngineAPI && Funke::g_EngineAPI->LogError) Funke::g_EngineAPI->LogError(msg, __FILE__, __LINE__); } while(0)

#define FNK_NATIVE_JOIN_IMPL(a, b) a##b
#define FNK_NATIVE_JOIN(a, b) FNK_NATIVE_JOIN_IMPL(a, b)
#define FNK_REGISTER_SCRIPT_IMPL(ClassName, Id) \
	namespace { struct FNK_NATIVE_JOIN(NativeAutoReg_, Id) { \
		FNK_NATIVE_JOIN(NativeAutoReg_, Id)() { \
			Funke::NativeScriptRegistry::Register(#ClassName, []() -> Funke::NativeScript* { return new ClassName(); }); \
			Funke::NativeScriptRegistry::RegisterType(typeid(ClassName), #ClassName); \
		} \
	} FNK_NATIVE_JOIN(s_NativeAutoReg_, Id); }
#define REGISTER_SCRIPT(ClassName) FNK_REGISTER_SCRIPT_IMPL(ClassName, __COUNTER__)
