#include "Editor/AnimationPreview.hpp"

#include "Animation/AnimationClip.hpp"
#include "Components/General/AnimatorComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Inspector/PropertyDescriptor.hpp"
#include "Scene/ComponentInfo.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

#include <algorithm>
#include <string_view>

namespace Index {
	namespace {
		bool IsExcludedComponent(std::string_view serializedName) {
			// Identity and the Animator itself are not animation targets.
			return serializedName.empty() || serializedName == "name" || serializedName == "Animator" || serializedName == "UUID"
				|| serializedName == "Script" || serializedName == "PrefabInstance" || serializedName == "EntityMetaData";
		}

		bool SameTarget(const AnimationTarget& a, const AnimationTarget& b) {
			if (a.EntityId != b.EntityId || a.TargetKind != b.TargetKind) return false;
			if (a.TargetKind == AnimationTarget::Kind::Property) return a.Descriptor == b.Descriptor;
			return a.ScriptClass == b.ScriptClass && a.Property == b.Property;
		}

		void CollectRecursive(Scene& scene, const ComponentRegistry& registry, EntityHandle entity, const std::string& path,
			std::vector<AnimatableProperty>& out) {
			Entity e = scene.GetEntity(entity);
			registry.ForEachComponentInfoInRegistrationOrder([&](const ComponentInfo& info) {
				if (info.category != ComponentCategory::Component || IsExcludedComponent(info.serializedName)) return;
				if (info.properties.empty() || !info.has || !info.has(e)) return;
				for (const PropertyDescriptor& descriptor : info.properties) {
					if (!descriptor.Get || !descriptor.Set || !IsAnimatableType(descriptor.Type)) continue;
					out.push_back({ entity, path, &info, &descriptor });
				}
			});
			const HierarchyComponent* hierarchy = scene.GetRegistry().try_get<HierarchyComponent>(entity);
			if (!hierarchy) return;
			for (EntityHandle child : hierarchy->Children) {
				if (!scene.GetRegistry().valid(child)) continue;
				const std::string name = scene.GetEntity(child).GetName();
				CollectRecursive(scene, registry, child, path.empty() ? name : path + "/" + name, out);
			}
		}
	}

	bool IsAnimatableType(PropertyType type) {
		if (IsCurveAnimatable(type)) return true;
		switch (type) {
		case PropertyType::String:
		case PropertyType::Enum:
		case PropertyType::TextureRef:
		case PropertyType::AudioRef:
		case PropertyType::FontRef:
		case PropertyType::AssetRef:
			return true;
		default:
			return false;
		}
	}

	std::vector<AnimatableProperty> CollectAnimatableProperties(Scene& scene, EntityHandle root) {
		std::vector<AnimatableProperty> out;
		if (!scene.GetRegistry().valid(root)) return out;
		CollectRecursive(scene, SceneManager::GetComponentRegistry(), root, {}, out);
		return out;
	}

	void AnimationPreview::SetClipPose(Scene* scene, EntityHandle root, uint64_t clipGuid, float time) {
		m_ClipScene = scene;
		m_ClipRoot = root;
		m_ClipGuid = clipGuid;
		m_ClipTime = time;
		m_ClipRequested = scene != nullptr && clipGuid != 0;
	}

	void AnimationPreview::Reset() {
		m_EditModeRuntimes.clear();
		m_ClipRuntime.reset();
		m_ClipRuntimeGuid = 0;
		m_Written.clear();
		m_Watched.clear();
		m_Scene = nullptr;
	}

	void AnimationPreview::CaptureTargets(Scene& scene, const AnimationTargetSet& targets) {
		AnimationTargetSet reader;
		for (const AnimationTarget& target : targets.GetTargets()) {
			const bool known = std::any_of(m_Written.begin(), m_Written.end(), [&](const Written& w) { return SameTarget(w.Target, target); });
			if (known) continue;
			reader.Clear();
			const int index = reader.Add(target);
			PropertyValue authored;
			if (!reader.Read(scene, index, authored)) continue;
			m_Written.push_back({ target, authored, {} });
		}
	}

	void AnimationPreview::ApplyEditModeAnimators(Scene& scene) {
		auto& registry = scene.GetRegistry();
		const float dt = Time::GetDeltaTimeUnscaled();
		std::vector<EntityHandle> active;
		auto view = registry.view<AnimatorComponent>(entt::exclude<DisabledTag>);
		for (EntityHandle entity : view) {
			const AnimatorComponent& component = view.get<AnimatorComponent>(entity);
			if (!component.ApplyInEditMode) continue;
			active.push_back(entity);
			std::unique_ptr<AnimatorRuntime>& runtime = m_EditModeRuntimes[entity];
			if (!runtime) runtime = std::make_unique<AnimatorRuntime>();
			if (!runtime->Sync(component)) continue;
			Entity owner = scene.GetEntity(entity);
			runtime->EnsureBindings(scene, owner);
			CaptureTargets(scene, runtime->GetTargets());
			runtime->Update(scene, owner, dt * component.Speed, false);
		}
		std::erase_if(m_EditModeRuntimes, [&](const auto& entry) {
			return std::find(active.begin(), active.end(), entry.first) == active.end();
		});
	}

	void AnimationPreview::ApplyClipPose(Scene& scene) {
		if (!scene.GetRegistry().valid(m_ClipRoot)) return;
		if (!m_ClipRuntime || m_ClipRuntimeGuid != m_ClipGuid) {
			m_ClipRuntime = std::make_unique<AnimatorRuntime>();
			m_ClipRuntimeGuid = m_ClipGuid;
		}
		AnimatorComponent single;
		single.Clip = m_ClipGuid;
		if (!m_ClipRuntime->Sync(single)) return;
		Entity root = scene.GetEntity(m_ClipRoot);
		m_ClipRuntime->EnsureBindings(scene, root);
		m_ClipErrors = m_ClipRuntime->GetBindingErrors();
		CaptureTargets(scene, m_ClipRuntime->GetTargets());
		m_ClipRuntime->ApplyStatePose(scene, root, 0, m_ClipTime);
	}

	void AnimationPreview::BeginFrame(Scene* scene) {
		m_Written.clear();
		m_Watched.clear();
		const bool clipRequested = m_ClipRequested;
		m_ClipRequested = false;
		if (!scene || Application::GetIsPlaying()) {
			if (m_Scene || !m_EditModeRuntimes.empty()) Reset();
			return;
		}
		if (m_Scene != scene) {
			m_EditModeRuntimes.clear();
			m_ClipRuntime.reset();
		}
		m_Scene = scene;

		ApplyEditModeAnimators(*scene);
		if (clipRequested && m_ClipScene == scene) {
			ApplyClipPose(*scene);
		}
		else {
			m_ClipErrors.clear();
		}

		AnimationTargetSet reader;
		for (Written& w : m_Written) {
			reader.Clear();
			reader.Read(*scene, reader.Add(w.Target), w.Posed);
		}

		if (m_Recording && clipRequested && m_ClipScene == scene && scene->GetRegistry().valid(m_ClipRoot)) {
			for (AnimatableProperty& property : CollectAnimatableProperties(*scene, m_ClipRoot)) {
				Entity entity = scene->GetEntity(property.EntityId);
				Watched watched{ property, property.Descriptor->Get(entity) };
				m_Watched.push_back(std::move(watched));
			}
		}
	}

	void AnimationPreview::EndFrame() {
		if (!m_Scene) return;
		Scene& scene = *m_Scene;
		auto& registry = scene.GetRegistry();

		// Values a recorded property must return to: the authored value for animated ones, the pre-edit value otherwise.
		struct Restore {
			EntityHandle EntityId;
			const ComponentInfo* Info;
			const PropertyDescriptor* Descriptor;
			PropertyValue Value;
		};
		std::vector<Restore> restores;

		for (const Watched& watched : m_Watched) {
			if (!registry.valid(watched.Property.EntityId)) continue;
			Entity entity = scene.GetEntity(watched.Property.EntityId);
			if (!watched.Property.Info->has(entity)) continue;
			const PropertyValue current = watched.Property.Descriptor->Get(entity);
			if (current == watched.Value) continue;

			RecordedChange change;
			change.Path = watched.Property.Path;
			change.Component = watched.Property.Info->serializedName;
			change.Property = watched.Property.Descriptor->Name;
			change.Type = watched.Property.Descriptor->Type;
			change.Before = watched.Value;
			change.After = current;
			m_Recorded.push_back(std::move(change));

			const auto written = std::find_if(m_Written.begin(), m_Written.end(), [&](const Written& w) {
				return w.Target.TargetKind == AnimationTarget::Kind::Property && w.Target.EntityId == watched.Property.EntityId
					&& w.Target.Descriptor == watched.Property.Descriptor;
			});
			if (written == m_Written.end()) {
				restores.push_back({ watched.Property.EntityId, watched.Property.Info, watched.Property.Descriptor, watched.Value });
			}
		}

		AnimationTargetSet writer;
		for (const Written& w : m_Written) {
			writer.Clear();
			const int index = writer.Add(w.Target);
			if (!m_Recording && w.Target.TargetKind == AnimationTarget::Kind::Property) {
				PropertyValue current;
				// Edited while not recording: the new value is the authored one now.
				if (writer.Read(scene, index, current) && current != w.Posed) continue;
			}
			writer.Write(scene, index, w.Authored);
		}
		for (const Restore& restore : restores) {
			Entity entity = scene.GetEntity(restore.EntityId);
			restore.Descriptor->Set(entity, restore.Value);
		}

		m_Written.clear();
		m_Watched.clear();
	}

}
