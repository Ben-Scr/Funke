#include "Systems/ImGuiEditorLayer.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/General/General.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/UI/UI.hpp"
#include "Core/EngineModules.hpp"
#include "Graphics/TextureManager.hpp"
#include "Gui/EditorMenu.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/EntityHelper.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Systems/TransformHierarchySystem.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <unordered_set>

namespace Funke {
	namespace {
		bool IsAsciiDigits(std::string_view value)
		{
			if (value.empty()) return false;
			for (char c : value) {
				if (c < '0' || c > '9') return false;
			}
			return true;
		}

		std::string StripEditorNumericSuffix(std::string_view name)
		{
			std::string base(name);
			if (base.empty()) return "Entity";

			if (base.size() > 3 && base.back() == ')') {
				const std::size_t open = base.rfind(" (");
				if (open != std::string::npos && open + 2 < base.size() - 1
					&& IsAsciiDigits(std::string_view(base).substr(open + 2, base.size() - open - 3))) {
					base.erase(open);
					return base.empty() ? "Entity" : base;
				}
			}

			auto stripDelimitedDigits = [&](char delimiter) -> bool {
				const std::size_t pos = base.rfind(delimiter);
				if (pos == std::string::npos || pos + 1 >= base.size()) return false;
				if (!IsAsciiDigits(std::string_view(base).substr(pos + 1))) return false;
				base.erase(pos);
				return true;
			};

			if (stripDelimitedDigits('-') || stripDelimitedDigits('_') || stripDelimitedDigits(' ')) {
				return base.empty() ? "Entity" : base;
			}

			return base;
		}

		std::string FormatEditorEntitySuffix(
			const std::string& base,
			int index,
			FunkeProject::EditorEntityNameSuffixStyle style)
		{
			switch (style) {
			case FunkeProject::EditorEntityNameSuffixStyle::SpaceNumber:
				return base + " " + std::to_string(index);
			case FunkeProject::EditorEntityNameSuffixStyle::HyphenNumber:
				return base + "-" + std::to_string(index);
			case FunkeProject::EditorEntityNameSuffixStyle::UnderscoreNumber:
				return base + "_" + std::to_string(index);
			case FunkeProject::EditorEntityNameSuffixStyle::ParenthesizedNumber:
			default:
				return base + " (" + std::to_string(index) + ")";
			}
		}
	}

	EntityHandle ImGuiEditorLayer::SpawnAssetEntityAt(Scene& scene, const std::string& assetPath, const Vec2& worldPos) {
		auto& hierarchyState = SceneHierarchyState::Get(scene);
		std::string ext = std::filesystem::path(assetPath).extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		EntityHandle created = entt::null;

		if (ext == ".prefab") {
			const EntityHandle loaded = SceneSerializer::LoadEntityFromFile(scene, assetPath);
			if (loaded != entt::null) {
				Transform2DComponent* transform = nullptr;
				if (scene.TryGetComponent<Transform2DComponent>(loaded, transform) && transform) {
					transform->SetPosition(worldPos); // override the prefab's authored root position
				}
				EnsureEditorUniqueEntityNames(scene, { loaded });
				created = loaded;
			}
		}
		else if ((ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
			&& !EngineModules::IsActive(EngineModule::TwoD)) {
			// A dropped image becomes a Sprite Renderer on a Transform2D entity.
			FNK_WARN_TAG("Editor", "Dropping an image creates a sprite, which needs the Funke.2D module (Project Settings > Build > Engine Modules).");
		}
		else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
			const std::string entityName = std::filesystem::path(assetPath).stem().string();
			Entity newEntity = scene.CreateEntity(entityName);
			auto& transform = newEntity.GetComponent<Transform2DComponent>();
			transform.SetPosition(worldPos);

			auto& sr = newEntity.AddComponent<SpriteRendererComponent>();
			// Asset id drives serialization + the editor's precise selection outline;
			// the handle is what the renderer samples this session.
			sr.TextureAssetId = AssetRegistry::GetOrCreateAssetUUID(assetPath);
			sr.TextureHandle = TextureManager::LoadTexture(assetPath);

			Texture2D* tex = TextureManager::GetTexture(sr.TextureHandle);
			if (tex && tex->IsValid() && tex->GetHeight() > 0) {
				const float aspect = static_cast<float>(tex->GetWidth()) / static_cast<float>(tex->GetHeight());
				transform.SetScale({ aspect, 1.0f });
			}

			EnsureEditorUniqueEntityNames(scene, { newEntity.GetHandle() });
			created = newEntity.GetHandle();
		}

		if (created != entt::null) {
			TransformHierarchySystem::Propagate(scene); // root world transform now matches the drop point this frame
			SelectEntity(created);
			scene.MarkDirty();
			hierarchyState.EntityOrder.clear();
			hierarchyState.Dirty = true;
			++m_SelectionVersion;
			RecordCreatedEntities(scene, std::span(&created, 1), ext == ".prefab" ? "Instantiate Prefab" : "Create Sprite");
		}
		return created;
	}

	std::string ImGuiEditorLayer::MakeEditorUniqueEntityName(
		Scene& scene,
		std::string_view baseName,
		EntityHandle ignoreEntity) const
	{
		FunkeProject* project = ProjectManager::GetCurrentProject();
		std::string original = baseName.empty() ? std::string("Entity") : std::string(baseName);
		if (!project || !project->EditorEnsureUniqueEntityNames) {
			return original;
		}

		const auto style = project->EditorEntityNameSuffix;
		std::unordered_set<std::string> existingNames;
		auto view = scene.GetRegistry().view<NameComponent>();
		for (auto [entity, name] : view.each()) {
			if (entity == ignoreEntity) {
				continue;
			}
			existingNames.insert(name.Name);
		}

		if (!existingNames.contains(original)) {
			return original;
		}

		std::string base = StripEditorNumericSuffix(original);
		if (base.empty()) {
			base = "Entity";
		}
		const bool originalHadSuffix = base != original;
		if (!originalHadSuffix && !existingNames.contains(base)) {
			return base;
		}

		for (int suffix = 1; suffix < std::numeric_limits<int>::max(); ++suffix) {
			std::string candidate = FormatEditorEntitySuffix(base, suffix, style);
			if (!existingNames.contains(candidate)) {
				return candidate;
			}
		}

		return base;
	}

	void ImGuiEditorLayer::EnsureEditorUniqueEntityName(Scene& scene, EntityHandle entity) {
		if (entity == entt::null || !scene.IsValid(entity) || !scene.HasComponent<NameComponent>(entity)) {
			return;
		}

		NameComponent& name = scene.GetComponent<NameComponent>(entity);
		name.Name = MakeEditorUniqueEntityName(scene, name.Name, entity);
	}

	void ImGuiEditorLayer::EnsureEditorUniqueEntityNames(Scene& scene, const std::vector<EntityHandle>& roots) {
		if (roots.empty()) {
			return;
		}

		std::unordered_set<uint32_t> visited;
		std::function<void(EntityHandle)> visit = [&](EntityHandle entity) {
			if (entity == entt::null || !scene.IsValid(entity)) {
				return;
			}
			if (!visited.insert(static_cast<uint32_t>(entity)).second) {
				return;
			}

			EnsureEditorUniqueEntityName(scene, entity);
			if (scene.HasComponent<HierarchyComponent>(entity)) {
				for (EntityHandle child : scene.GetComponent<HierarchyComponent>(entity).Children) {
					visit(child);
				}
			}
		};

		for (EntityHandle root : roots) {
			visit(root);
		}
	}

	EntityHandle ImGuiEditorLayer::FinishCreatedEditorEntity(Scene& scene, Entity parent, Entity created) {
		auto& hierarchyState = SceneHierarchyState::Get(scene);
		if (!created.IsValid()) {
			return entt::null;
		}
		// Prefab mode: entities created without a parent must be forced under the prefab root; siblings-of-root are silently dropped on save.
		Entity effectiveParent = parent;
		if (!effectiveParent.IsValid()
			&& IsInPrefabEditMode()
			&& m_PrefabEditScene
			&& m_PrefabEditRootEntity != entt::null
			&& m_PrefabEditScene->IsValid(m_PrefabEditRootEntity)
			&& &scene == m_PrefabEditScene.get())
		{
			effectiveParent = m_PrefabEditScene->GetEntity(m_PrefabEditRootEntity);
		}

		if (effectiveParent.IsValid()) {
			created.SetParent(effectiveParent);
			hierarchyState.CollapsedEntities.erase(static_cast<uint32_t>(effectiveParent.GetHandle()));
		}

		const EntityHandle createdHandle = created.GetHandle();
		EnsureEditorUniqueEntityNames(scene, { createdHandle });
		SelectEntity(createdHandle);
		scene.MarkDirty();
		hierarchyState.EntityOrder.clear();
		hierarchyState.Dirty = true;
		RecordCreatedEntities(scene, std::span(&createdHandle, 1), "Create " + created.GetName());
		return createdHandle;
	}

	EntityHandle ImGuiEditorLayer::RenderCreateEntityMenu(Scene& scene, Entity parent) {
		using Icon = EditorMenu::Icon;
		EntityHandle createdHandle = entt::null;

		auto finishCreated = [&](Entity created) {
			createdHandle = FinishCreatedEditorEntity(scene, parent, created);
		};

		if (EditorMenu::Item(Icon::Add, "Create (Empty)", "Shift + C"))
		{
			finishCreated(scene.CreateEmptyEntity());
		}
		if (EditorMenu::Item(Icon::Scene, "Create (Entity)"))
		{
			finishCreated(CreateDefaultEntity(scene, "Entity"));
		}

		auto renderPackagePresets = [&](auto&& self, std::string_view menuPath) -> void {
			bool renderedAny = false;
			std::vector<std::string> submenus;
			const std::string prefix = menuPath.empty() ? "" : std::string(menuPath) + "/";
			for (const SceneManager::EntityPresetInfo& preset : SceneManager::GetEntityPresets()) {
				if (preset.MenuPath.empty() || !preset.IsModuleActive()
					|| (preset.MenuPath != menuPath && !preset.MenuPath.starts_with(prefix))) {
					continue;
				}
				if (preset.MenuPath != menuPath) {
					const std::string_view remainder = std::string_view(preset.MenuPath).substr(prefix.size());
					const std::string submenu(remainder.substr(0, remainder.find('/')));
					if (submenu.empty() || (menuPath.empty() && (submenu == "2D Entity" || submenu == "3D Entity" || submenu == "UI"))) {
						continue;
					}
					if (std::find(submenus.begin(), submenus.end(), submenu) == submenus.end()) {
						submenus.push_back(submenu);
					}
					continue;
				}
				if (!renderedAny) {
					ImGui::Separator();
					renderedAny = true;
				}
				if (EditorMenu::Item(Icon::Scene, preset.Label.c_str())) {
					finishCreated(preset.Create(scene));
				}
			}
			if (!renderedAny && !submenus.empty()) {
				ImGui::Separator();
			}
			for (const std::string& submenu : submenus) {
				if (EditorMenu::Begin(Icon::Package, submenu.c_str())) {
					self(self, prefix + submenu);
					ImGui::EndMenu();
				}
			}
		};

		ImGui::Separator();

		if (EngineModules::IsActive(EngineModule::TwoD) && EditorMenu::Begin(Icon::Entity2D, "2D Entity"))
		{
			if (EditorMenu::Begin(Icon::Sprite, "Sprite"))
			{
				if (ImGui::MenuItem("Square"))
				{
					Entity created = scene.CreateEntity("Square");
					created.AddComponent<SpriteRendererComponent>();
					finishCreated(created);
				}
				if (ImGui::MenuItem("Circle"))
				{
					Entity created = scene.CreateEntity("Circle");
					auto& sprite = created.AddComponent<SpriteRendererComponent>();
					sprite.TextureHandle = TextureManager::GetDefaultTexture(DefaultTexture::Circle);
					finishCreated(created);
				}
				if (ImGui::MenuItem("9Sliced"))
				{
					Entity created = scene.CreateEntity("9Sliced");
					auto& sprite = created.AddComponent<SpriteRendererComponent>();
					sprite.TextureHandle = TextureManager::GetDefaultTexture(DefaultTexture::_9Sliced);
					finishCreated(created);
				}
				if (ImGui::MenuItem("Pixel"))
				{
					Entity created = scene.CreateEntity("Pixel");
					auto& sprite = created.AddComponent<SpriteRendererComponent>();
					sprite.TextureHandle = TextureManager::GetDefaultTexture(DefaultTexture::Pixel);
					finishCreated(created);
				}
				if (ImGui::MenuItem("Icon"))
				{
					Entity created = scene.CreateEntity("Icon");
					auto& sprite = created.AddComponent<SpriteRendererComponent>();
					sprite.TextureHandle = TextureManager::LoadTexture("icon.png");
					finishCreated(created);
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Physics")) {
				if (ImGui::MenuItem("Dynamic Body"))
				{
					Entity created = scene.CreateEntity("Dynamic Body");
					created.AddComponent<SpriteRendererComponent>();
					created.AddComponent<Rigidbody2DComponent>();
					created.AddComponent<BoxCollider2DComponent>();
					finishCreated(created);
				}
				if (ImGui::MenuItem("Kinematic Body"))
				{
					Entity created = scene.CreateEntity("Kinematic Body");
					created.AddComponent<SpriteRendererComponent>();
					created.AddComponent<Rigidbody2DComponent>().SetBodyType(BodyType::Kinematic);
					created.AddComponent<BoxCollider2DComponent>();
					finishCreated(created);
				}
				if (ImGui::MenuItem("Static Body"))
				{
					Entity created = scene.CreateEntity("Static Body");
					created.AddComponent<SpriteRendererComponent>();
					created.AddComponent<Rigidbody2DComponent>().SetBodyType(BodyType::Static);
					created.AddComponent<BoxCollider2DComponent>();
					finishCreated(created);
				}

				ImGui::EndMenu();
			}

			if (ImGui::MenuItem("Particle System"))
			{
				Entity created = scene.CreateEntity("Particle System");
				created.AddComponent<ParticleSystem2DComponent>();
				finishCreated(created);
			}

			// Package-registered presets can extend this menu without editor hardcoding.
			renderPackagePresets(renderPackagePresets, "2D Entity");

			ImGui::EndMenu();
		}

		const auto& entityPresets = SceneManager::GetEntityPresets();
		const bool has3DPresets = std::any_of(entityPresets.begin(), entityPresets.end(), [](const auto& preset) {
			return preset.IsModuleActive()
				&& (preset.MenuPath == "3D Entity" || preset.MenuPath.starts_with("3D Entity/"));
		});
		if (has3DPresets && EditorMenu::Begin(Icon::Scene, "3D Entity")) {
			renderPackagePresets(renderPackagePresets, "3D Entity");
			ImGui::EndMenu();
		}

		if (EditorMenu::Begin(Icon::UI, "UI"))
		{
			if (ImGui::MenuItem("Panel")) {
				finishCreated(EntityHelper::CreateUIPanel(scene));
			}
			if (ImGui::MenuItem("Image")) {
				Entity created = EntityHelper::CreateImageEntity(scene);
				if (!created.HasComponent<NameComponent>()) {
					created.AddComponent<NameComponent>(NameComponent("Image"));
				}
				finishCreated(created);
			}
			if (ImGui::MenuItem("Text")) {
				// Use CreateWith (not CreateEntity): avoids seeding Transform2DComponent which conflicts with RectTransform2D.
				Entity created = EntityHelper::CreateWith<RectTransform2DComponent, TextRendererComponent>(scene);
				created.GetComponent<RectTransform2DComponent>().SizeDelta = Vec2{ 100.0f, 100.0f };
				auto& createdText = created.GetComponent<TextRendererComponent>();
				createdText.HAlign = TextAlignment::Center;
				createdText.FontSize = 32.0f;
				if (!created.HasComponent<NameComponent>()) {
					created.AddComponent<NameComponent>(NameComponent("Text"));
				}
				finishCreated(created);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Button")) {
				finishCreated(EntityHelper::CreateUIButton(scene));
			}
			if (ImGui::MenuItem("Slider")) {
				finishCreated(EntityHelper::CreateUISlider(scene));
			}
			if (ImGui::MenuItem("Progress Bar")) {
				finishCreated(EntityHelper::CreateUIProgressBar(scene));
			}
			if (ImGui::MenuItem("Circular Slider")) {
				finishCreated(EntityHelper::CreateUICircularSlider(scene));
			}
			if (ImGui::MenuItem("Scrollbar")) {
				finishCreated(EntityHelper::CreateUIScrollbar(scene));
			}
			if (ImGui::MenuItem("Scroll View")) {
				finishCreated(EntityHelper::CreateUIScrollRect(scene));
			}
			if (ImGui::MenuItem("Input Field")) {
				finishCreated(EntityHelper::CreateUIInputField(scene));
			}
			if (ImGui::MenuItem("Dropdown")) {
				finishCreated(EntityHelper::CreateUIDropdown(scene));
			}
			if (ImGui::MenuItem("Toggle")) {
				finishCreated(EntityHelper::CreateUIToggle(scene));
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Horizontal Layout")) {
				finishCreated(EntityHelper::CreateUIHorizontalLayout(scene));
			}
			if (ImGui::MenuItem("Vertical Layout")) {
				finishCreated(EntityHelper::CreateUIVerticalLayout(scene));
			}
			if (ImGui::MenuItem("Grid Layout")) {
				finishCreated(EntityHelper::CreateUIGridLayout(scene));
			}
			renderPackagePresets(renderPackagePresets, "UI");
			ImGui::EndMenu();
		}

		renderPackagePresets(renderPackagePresets, "");

		ImGui::Separator();

		if (EditorMenu::Item(Icon::Camera, "Camera"))
		{
			Entity created = CreateDefaultEntity(scene, "Camera");
			if (m_EditorView3D) {
				created.AddComponent<Camera3DComponent>();
			}
			else {
				created.AddComponent<Camera2DComponent>();
			}
			finishCreated(created);
		}

		if (EditorMenu::Item(Icon::Audio, "Audio Source"))
		{
			Entity created = scene.CreateEntity("Audio Source");
			created.AddComponent<AudioSourceComponent>();
			finishCreated(created);
		}

		return createdHandle;
	}
}
