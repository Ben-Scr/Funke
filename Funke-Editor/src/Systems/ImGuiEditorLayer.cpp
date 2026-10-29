#include <pch.hpp>
#include "ImGuiEditorLayer.hpp"
#include "Gui/EditorShortcuts.hpp"
#include "Core/EngineModules.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>
// glad include retired with the legacy GL backend (2026-05); the editor
// no longer makes raw GL calls, all drawing routes through engine renderers.

#include <algorithm>
#include <cstdio>

#include "Components/Forward.hpp"
#include "Components/General/General.hpp"
#include "Components/UI/UI.hpp"
#include "Components/Tags.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Audio/AudioStreams.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/Graphics/DirectionalLightComponent.hpp"
#include "Components/Graphics/PointLightComponent.hpp"
#include "Components/Graphics/SpotLightComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Core/Window.hpp"
#include "DevTools/RunAutomation.hpp"
#include "Events/EventDispatcher.hpp"
#include "Events/SceneEvents.hpp"
#include "Events/WindowFocusEvent.hpp"
#include "Events/WindowLostFocusEvent.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Graphics/RenderSettings.hpp"
#include "Graphics/GizmoRenderer.hpp"
#include "Profiling/Profiler.hpp"
#include "Graphics/Gizmo.hpp"
#include "Gui/CustomTitlebar.hpp"
#include "Gui/GuiRenderer.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/ComponentOrder.hpp"
#include "Components/General/ComponentOrderComponent.hpp"
#include <Scene/EntityHelper.hpp>

#include "Graphics/TextureManager.hpp"
#include "Graphics/SkyAmbient.hpp"
#include "Graphics/SpriteUVResolver.hpp"
#include "Graphics/Text/FontManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Serialization/Path.hpp"
#include "Localization/GameLocalization.hpp"
#include "Project/BuildPlatformSupport.hpp"
#include "Project/FunkeBuildProfile.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/SceneSerializerShared.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/File.hpp"
#include "Serialization/FileStamp.hpp"
#include "Scripting/DataAssetManager.hpp"
#include "Utils/Process.hpp"
#include "Packages/NuGetSource.hpp"
#include "Packages/GitHubSource.hpp"
#include "Editor/ApplicationEditorAccess.hpp"
#include "Editor/EditorComponentRegistration.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Editor/EditorRestart.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Inspector/ReferencePicker.hpp"
#include "Inspector/PropertyDrawer.hpp"
#include "Gui/AddComponentPopup.hpp"
#include "Gui/EditorDockLayout.hpp"
#include "Gui/EditorIcons.hpp"
#include "Gui/Icons.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiContextLayer.hpp"
#include "Gui/HierarchyDragData.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Serialization/PrefabTemplateCache.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Scripting/ScriptDiscovery.hpp"
#include "Scripting/ScriptComponentInspector.hpp"
#include "Systems/TransformHierarchySystem.hpp"
#include "Systems/UILayoutSystem.hpp"
#include "Undo/InspectorUndoRecorder.hpp"
#include "Math/VectorMath.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <unordered_set>

namespace Funke {
	namespace {
		// Transforms first, scripts last, the rest in registration order: the registry's map order is a hash order
		// that put e.g. Box Collider 3D above Transform 3D.
		template <typename F>
		void ForEachComponentInInspectorOrder(const ComponentRegistry& registry, F&& fn) {
			std::vector<const ComponentInfo*> infos;
			registry.ForEachComponentInfoInRegistrationOrder([&](const ComponentInfo& info) { infos.push_back(&info); });
			const auto rank = [](const ComponentInfo& info) {
				if (info.typeId == typeid(Transform2DComponent) || info.typeId == typeid(RectTransform2DComponent)
					|| info.typeId == typeid(Transform3DComponent)) return 0;
				return info.isDynamic || info.displayName == "Scripts" ? 2 : 1;
			};
			std::stable_sort(infos.begin(), infos.end(),
				[&](const ComponentInfo* a, const ComponentInfo* b) { return rank(*a) < rank(*b); });
			for (const ComponentInfo* info : infos) {
				fn(info->isDynamic ? std::type_index(typeid(void)) : info->typeId, *info);
			}
		}

		// One Inspector section: a built-in component, or one C# component / script of the ScriptComponent.
		struct InspectorSection {
			std::string Key;      // ComponentOrderComponent key
			std::string RefName;  // name in COMPONENT_REF payloads: display name, or the class
			const ComponentInfo* Info = nullptr;
			std::type_index TypeId = typeid(void);
			int ScriptKind = 0;   // 1 = ScriptComponent::ManagedComponents, 2 = ScriptComponent::Scripts
			std::size_t ScriptIndex = 0;
			bool WholeScriptBlock = false;  // multi-selection with different scripts: drawn as one block, not reorderable
		};

		struct InspectorSectionList {
			std::vector<InspectorSection> Items;
			std::size_t Pinned = 0;  // leading transforms; they stay on top
		};

		bool IsInspectorTransform(const ComponentInfo& info) {
			return info.typeId == typeid(Transform2DComponent) || info.typeId == typeid(RectTransform2DComponent)
				|| info.typeId == typeid(Transform3DComponent);
		}

		// has picks the components to list (the selection's common ones, or one entity's own); scripts non-null
		// lists each C# component and script as its own section. saved is the hand-made order to apply.
		template <typename HasFn>
		InspectorSectionList CollectInspectorSections(const ComponentRegistry& registry, const ScriptComponent* scripts,
			const std::vector<std::string>* saved, HasFn&& has) {
			InspectorSectionList list;
			ForEachComponentInInspectorOrder(registry, [&](const std::type_index& typeId, const ComponentInfo& info) {
				if (info.category != ComponentCategory::Component) return;
				if (info.displayName == "Name") return; // Shown in entity header
				if (!has(typeId, info)) return;
				if (info.displayName == "Scripts") {
					if (!scripts) {
						list.Items.push_back({ info.displayName, info.displayName, &info, typeId, 0, 0, true });
						return;
					}
					for (std::size_t i = 0; i < scripts->ManagedComponents.size(); ++i) {
						const std::string& className = scripts->ManagedComponents[i];
						list.Items.push_back({ ComponentOrder::ManagedComponentKey(className), className, &info, typeId, 1, i });
					}
					for (std::size_t i = 0; i < scripts->Scripts.size(); ++i) {
						const std::string& className = scripts->Scripts[i].GetClassName();
						list.Items.push_back({ ComponentOrder::ScriptKey(className), className, &info, typeId, 2, i });
					}
					return;
				}
				// Dynamic components with no drawInspector/properties render as empty sections; their fields appear in the paired ScriptComponent.Scripts entry.
				if (info.isDynamic && !info.drawInspector && info.properties.empty()) return;
				if (IsInspectorTransform(info)) ++list.Pinned;
				list.Items.push_back({ info.serializedName.empty() ? "~" + info.displayName : info.serializedName,
					info.displayName, &info, typeId });
			});
			if (saved && !saved->empty()) {
				std::vector<std::string> keys;
				keys.reserve(list.Items.size());
				for (const InspectorSection& item : list.Items) keys.push_back(item.Key);
				std::vector<InspectorSection> arranged;
				arranged.reserve(list.Items.size());
				for (const std::string& key : ComponentOrder::Arrange(keys, *saved, list.Pinned)) {
					const auto it = std::find_if(list.Items.begin(), list.Items.end(),
						[&](const InspectorSection& item) { return item.Key == key; });
					if (it != list.Items.end()) arranged.push_back(std::move(*it));
				}
				list.Items = std::move(arranged);
			}
			return list;
		}

		// Local "HH:MM:SS" stamp for a console entry, captured at log-receipt time.
		std::string FormatLogClockTimestamp() {
			const std::time_t now = std::time(nullptr);
			std::tm local{};
#if defined(_WIN32)
			if (localtime_s(&local, &now) != 0) return "--:--:--";
#else
			if (!localtime_r(&now, &local)) return "--:--:--";
#endif
			char buffer[16];
			std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d",
				local.tm_hour, local.tm_min, local.tm_sec);
			return std::string(buffer);
		}

		// Awakens entities that already exist when play begins; runtime-spawned ones are
		// handled at instantiation (SceneSerializer::InstantiatePrefab). Both share the
		// per-component PlayOnAwakeIfEnabled() rule.
		void StartPlayOnAwakeComponents(Scene& scene)
		{
			for (auto [ent, audio] : scene.GetRegistry().view<AudioSourceComponent>(entt::exclude<DisabledTag>).each())
				audio.PlayOnAwakeIfEnabled();

			for (auto [ent, particleSystem] : scene.GetRegistry().view<ParticleSystem2DComponent>(entt::exclude<DisabledTag>).each())
				particleSystem.PlayOnAwakeIfEnabled();

			for (auto [ent, particleSystem] : scene.GetRegistry().view<ParticleSystem3DComponent>(entt::exclude<DisabledTag>).each())
				particleSystem.PlayOnAwakeIfEnabled();
		}

		bool HasSelectedAncestor(const Scene& scene, EntityHandle entity, const std::unordered_set<uint32_t>& selectedEntities)
		{
			const auto& registry = scene.GetRegistry();
			EntityHandle current = entity;
			while (current != entt::null && registry.valid(current) && registry.all_of<HierarchyComponent>(current)) {
				const EntityHandle parent = registry.get<HierarchyComponent>(current).Parent;
				if (parent == entt::null || !registry.valid(parent)) {
					return false;
				}

				if (selectedEntities.contains(static_cast<uint32_t>(parent))) {
					return true;
				}

				current = parent;
			}

			return false;
		}

		std::vector<EntityHandle> FilterSelectedHierarchyRoots(const Scene& scene, const std::vector<EntityHandle>& selectedEntities)
		{
			std::unordered_set<uint32_t> selectedSet;
			selectedSet.reserve(selectedEntities.size());
			for (EntityHandle entity : selectedEntities) {
				if (entity != entt::null && scene.IsValid(entity)) {
					selectedSet.insert(static_cast<uint32_t>(entity));
				}
			}

			std::vector<EntityHandle> roots;
			roots.reserve(selectedEntities.size());
			for (EntityHandle entity : selectedEntities) {
				if (entity == entt::null || !scene.IsValid(entity)) {
					continue;
				}

				if (!HasSelectedAncestor(scene, entity, selectedSet)) {
					roots.push_back(entity);
				}
			}

			return roots;
		}

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

		std::string BuildScriptMenuLabel(const EditorScriptDiscovery::ScriptEntry& scriptEntry)
		{
			return EditorScriptDiscovery::MenuLabel(scriptEntry);
		}

		bool AttachScriptToEntity(Entity entity, Scene& scene, const EditorScriptDiscovery::ScriptEntry& scriptEntry)
		{
			if (scriptEntry.ClassName.empty() || scriptEntry.Type == ScriptType::Unknown) {
				return false;
			}

			if (!entity.HasComponent<ScriptComponent>()) {
				entity.AddComponent<ScriptComponent>();
			}

			auto& scriptComponent = entity.GetComponent<ScriptComponent>();
			const bool alreadyHadScript = scriptComponent.HasScript(scriptEntry.ClassName, scriptEntry.Type);

			if (!alreadyHadScript) {
				scriptComponent.AddScript(scriptEntry.ClassName, scriptEntry.Type);
			}

			// Also populate DynamicComponentStorage so HasNativeComponent<T>()/GetRef<T>() in scripts can find it; done unconditionally so re-attach after failed registration also seeds the storage.
			bool storageAdded = false;
			if (scriptEntry.IsNativeComponent) {
				auto& componentRegistry = SceneManager::GetComponentRegistry();
				if (const ComponentInfo* info = componentRegistry.FindBySerializedName(scriptEntry.ClassName)) {
					if (info->isDynamic && info->add && (!info->has || !info->has(entity))) {
						info->add(entity);
						storageAdded = true;
					}
				}
			}

			if (alreadyHadScript && !storageAdded) {
				return false;
			}

			scene.MarkDirty();
			return true;
		}

		bool AttachManagedComponentToEntity(Entity entity, Scene& scene, const EditorScriptDiscovery::ScriptEntry& scriptEntry)
		{
			if (scriptEntry.ClassName.empty() || !scriptEntry.IsManagedComponent) {
				return false;
			}

			if (!entity.HasComponent<ScriptComponent>()) {
				entity.AddComponent<ScriptComponent>();
			}

			auto& scriptComponent = entity.GetComponent<ScriptComponent>();
			if (scriptComponent.HasManagedComponent(scriptEntry.ClassName)) {
				return false;
			}

			scriptComponent.AddManagedComponent(scriptEntry.ClassName);
			scene.MarkDirty();
			return true;
		}

		bool IsLeftMouseDragPastClickThreshold()
		{
			const ImGuiIO& io = ImGui::GetIO();
			const ImVec2 delta(io.MousePos.x - io.MouseClickedPos[ImGuiMouseButton_Left].x,
				io.MousePos.y - io.MouseClickedPos[ImGuiMouseButton_Left].y);
			return (delta.x * delta.x + delta.y * delta.y) > (io.MouseDragThreshold * io.MouseDragThreshold);
		}

		struct ComponentClipboardPayload {
			std::string SerializedName;
			Json::Value Data;
		};

		bool TryReadComponentClipboard(const std::string& clipboardJson, ComponentClipboardPayload& outPayload)
		{
			if (clipboardJson.empty()) {
				return false;
			}

			Json::Value root;
			std::string parseError;
			if (!Json::TryParse(clipboardJson, root, &parseError) || !root.IsObject()) {
				return false;
			}

			const Json::Value* componentValue = root.FindMember("component");
			const Json::Value* dataValue = root.FindMember("data");
			if (!componentValue || !componentValue->IsString() || !dataValue || dataValue->IsNull()) {
				return false;
			}

			outPayload.SerializedName = componentValue->AsStringOr();
			outPayload.Data = *dataValue;
			return !outPayload.SerializedName.empty();
		}

		struct LogSourceLocation {
			std::string FilePath;
			std::string LinkText;
			int Line = 0;
			// Byte offsets of the "<path>:N" link substring in the message; console renderer styles that span inline as the clickable link.
			size_t MessageStart = 0;
			size_t MessageEnd = 0;
		};

		bool IsLineBoundary(std::string_view text, size_t pos) {
			return pos == std::string_view::npos
				|| pos >= text.size()
				|| text[pos] == '\n'
				|| text[pos] == '\r';
		}

		bool IsExtensionBoundary(std::string_view text, size_t pos) {
			if (pos >= text.size()) return true;
			const unsigned char c = static_cast<unsigned char>(text[pos]);
			return !std::isalnum(c) && text[pos] != '_';
		}

		bool StartsWithIgnoreCase(std::string_view text, size_t pos, std::string_view prefix) {
			if (pos + prefix.size() > text.size()) return false;
			for (size_t i = 0; i < prefix.size(); i++) {
				const unsigned char a = static_cast<unsigned char>(text[pos + i]);
				const unsigned char b = static_cast<unsigned char>(prefix[i]);
				if (std::tolower(a) != std::tolower(b)) {
					return false;
				}
			}
			return true;
		}

		void SkipWhitespace(std::string_view text, size_t& pos) {
			while (pos < text.size() && text[pos] != '\n' && text[pos] != '\r'
				&& std::isspace(static_cast<unsigned char>(text[pos]))) {
				pos++;
			}
		}

		bool ParsePositiveInt(std::string_view text, size_t& pos, int& outValue) {
			SkipWhitespace(text, pos);
			if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) {
				return false;
			}

			int value = 0;
			while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
				value = value * 10 + (text[pos] - '0');
				pos++;
			}

			outValue = value;
			return value > 0;
		}

		bool TryParseLineNumber(std::string_view text, size_t pos, int& outLine, size_t* outEnd = nullptr) {
			SkipWhitespace(text, pos);
			if (pos >= text.size() || IsLineBoundary(text, pos)) {
				return false;
			}

			if (text[pos] == '(') {
				pos++;
				if (!ParsePositiveInt(text, pos, outLine)) return false;
				if (pos < text.size() && text[pos] == ')') pos++;
				if (outEnd) *outEnd = pos;
				return true;
			}

			if (text[pos] == ':' || text[pos] == ',') {
				pos++;
				SkipWhitespace(text, pos);
			}

			if (StartsWithIgnoreCase(text, pos, "line")) {
				pos += 4;
				SkipWhitespace(text, pos);
			}

			if (!ParsePositiveInt(text, pos, outLine)) return false;
			if (outEnd) *outEnd = pos;
			return true;
		}

		std::string ToLowerCopy(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}

		std::string CanonicalFilePath(const std::filesystem::path& path) {
			std::error_code ec;
			if (!std::filesystem::is_regular_file(path, ec) || ec) {
				return {};
			}
			std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
			return (ec ? path : canonical).lexically_normal().make_preferred().string();
		}

		std::string FindFileByName(const std::filesystem::path& root, const std::string& fileNameLower) {
			std::error_code ec;
			if (!std::filesystem::is_directory(root, ec) || ec) {
				return {};
			}

			std::filesystem::recursive_directory_iterator it(
				root,
				std::filesystem::directory_options::skip_permission_denied,
				ec);
			const std::filesystem::recursive_directory_iterator end;
			for (; it != end && !ec; it.increment(ec)) {
				const std::filesystem::directory_entry& entry = *it;
				std::error_code entryEc;
				if (!entry.is_regular_file(entryEc) || entryEc) continue;
				if (ToLowerCopy(entry.path().filename().string()) == fileNameLower) {
					return CanonicalFilePath(entry.path());
				}
			}
			return {};
		}

		std::string ResolveLogSourceFile(std::string rawPath) {
			if (rawPath.rfind("file:///", 0) == 0) {
				rawPath.erase(0, 8);
			}
			if (rawPath.empty()) {
				return {};
			}

			const std::filesystem::path sourcePath(rawPath);
			std::vector<std::filesystem::path> candidates;
			if (sourcePath.is_absolute()) {
				candidates.push_back(sourcePath);
			}
			else {
				candidates.push_back(std::filesystem::current_path() / sourcePath);
				if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
					if (!project->RootDirectory.empty()) candidates.emplace_back(std::filesystem::path(project->RootDirectory) / sourcePath);
					if (!project->ScriptsDirectory.empty()) candidates.emplace_back(std::filesystem::path(project->ScriptsDirectory) / sourcePath);
					if (!project->AssetsDirectory.empty()) candidates.emplace_back(std::filesystem::path(project->AssetsDirectory) / sourcePath);
				}
			}

			for (const std::filesystem::path& candidate : candidates) {
				if (std::string resolved = CanonicalFilePath(candidate); !resolved.empty()) {
					return resolved;
				}
			}

			if (!sourcePath.has_filename()) {
				return {};
			}

			FunkeProject* project = ProjectManager::GetCurrentProject();
			if (!project) {
				return {};
			}

			const std::string fileNameLower = ToLowerCopy(sourcePath.filename().string());
			const std::array<std::filesystem::path, 3> roots = {
				std::filesystem::path(project->ScriptsDirectory),
				std::filesystem::path(project->AssetsDirectory),
				std::filesystem::path(project->RootDirectory)
			};
			for (const std::filesystem::path& root : roots) {
				if (std::string resolved = FindFileByName(root, fileNameLower); !resolved.empty()) {
					return resolved;
				}
			}

			return {};
		}

		std::string ExtractRawSourcePath(std::string_view message, size_t extStart, size_t extEnd, size_t* outPathStart = nullptr) {
			size_t lineStart = message.rfind('\n', extStart);
			lineStart = (lineStart == std::string_view::npos) ? 0 : lineStart + 1;

			size_t start = std::string_view::npos;
			const size_t doubleQuote = message.rfind('"', extStart);
			const size_t singleQuote = message.rfind('\'', extStart);
			if (doubleQuote != std::string_view::npos && doubleQuote >= lineStart) {
				start = doubleQuote + 1;
			}
			if (singleQuote != std::string_view::npos && singleQuote >= lineStart
				&& (start == std::string_view::npos || singleQuote + 1 > start)) {
				start = singleQuote + 1;
			}

			const size_t inToken = message.rfind(" in ", extStart);
			if (start == std::string_view::npos && inToken != std::string_view::npos && inToken >= lineStart) {
				start = inToken + 4;
			}

			if (start == std::string_view::npos) {
				start = extStart;
				while (start > lineStart) {
					const char prev = message[start - 1];
					if (std::isspace(static_cast<unsigned char>(prev))
						|| prev == '"' || prev == '\'' || prev == '<' || prev == '>'
						|| prev == '[' || prev == ']') {
						break;
					}
					start--;
				}
			}

			while (start < extStart && std::isspace(static_cast<unsigned char>(message[start]))) {
				start++;
			}

			if (outPathStart) *outPathStart = start;
			return std::string(message.substr(start, extEnd - start));
		}

		std::optional<LogSourceLocation> FindLogSourceLocation(std::string_view message) {
			static constexpr std::array<std::string_view, 14> kScriptExtensions = {
				".cs", ".cpp", ".hpp", ".h", ".c", ".lua", ".py",
				".js", ".ts", ".json", ".xml", ".yaml", ".yml", ".shader"
			};

			for (size_t pos = 0; pos < message.size(); pos++) {
				for (std::string_view ext : kScriptExtensions) {
					if (!StartsWithIgnoreCase(message, pos, ext)) {
						continue;
					}
					const size_t extEnd = pos + ext.size();
					if (!IsExtensionBoundary(message, extEnd)) {
						continue;
					}

					int line = 0;
					size_t lineEndPos = 0;
					if (!TryParseLineNumber(message, extEnd, line, &lineEndPos)) {
						continue;
					}

					size_t pathStartPos = 0;
					std::string rawPath = ExtractRawSourcePath(message, pos, extEnd, &pathStartPos);
					if (rawPath.empty()) {
						continue;
					}

					std::filesystem::path rawExtPath(rawPath);
					std::string loweredExt = rawExtPath.extension().string();
					std::transform(loweredExt.begin(), loweredExt.end(), loweredExt.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (!ExternalEditor::IsScriptExtension(loweredExt)) {
						continue;
					}

					std::string resolved = ResolveLogSourceFile(rawPath);
					if (resolved.empty()) {
						continue;
					}

					LogSourceLocation location;
					location.FilePath = resolved;
					location.Line = line;
					location.LinkText = std::filesystem::path(resolved).filename().string()
						+ ":" + std::to_string(line);
					location.MessageStart = pathStartPos;
					location.MessageEnd = lineEndPos;
					return location;
				}
			}

			return std::nullopt;
		}

		const ComponentInfo* FindComponentInfoBySerializedName(const ComponentRegistry& registry, const std::string& serializedName)
		{
			const ComponentInfo* found = nullptr;
			registry.ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& info) {
				if (!found && info.category == ComponentCategory::Component && info.serializedName == serializedName) {
					found = &info;
				}
			});
			return found;
		}

		float DivideOrKeep(float numerator, float denominator, float fallback)
		{
			constexpr float k_MinScaleAxis = 0.0001f;
			return std::abs(denominator) > k_MinScaleAxis ? numerator / denominator : fallback;
		}

		void SetLocalTransformFromWorld(
			Transform2DComponent& child,
			const Transform2DComponent* parent,
			const Vec2& worldPosition,
			float worldRotation,
			const Vec2& worldScale)
		{
			if (parent) {
				Vec2 parentRelative = worldPosition - parent->Position;
				parentRelative = Rotate(parentRelative, -parent->Rotation);
				child.LocalPosition = {
					DivideOrKeep(parentRelative.x, parent->Scale.x, child.LocalPosition.x),
					DivideOrKeep(parentRelative.y, parent->Scale.y, child.LocalPosition.y)
				};
				child.LocalRotation = worldRotation - parent->Rotation;
				child.LocalScale = {
					DivideOrKeep(worldScale.x, parent->Scale.x, child.LocalScale.x),
					DivideOrKeep(worldScale.y, parent->Scale.y, child.LocalScale.y)
				};
			}
			else {
				child.LocalPosition = worldPosition;
				child.LocalRotation = worldRotation;
				child.LocalScale = worldScale;
			}

			child.Position = worldPosition;
			child.Rotation = worldRotation;
			child.Scale = worldScale;
			child.MarkDirty();
		}
	}

	void ImGuiEditorLayer::ApplyGizmoManipulation(Scene& scene, const Vec2& newWorldPos,
		float newWorldRotation, const Vec2& newWorldScale) {
		if (m_SelectedEntity == entt::null || !scene.IsValid(m_SelectedEntity)) return;

		TransformHierarchySystem::Propagate(scene);
		Transform2DComponent* primary = nullptr;
		if (!scene.TryGetComponent<Transform2DComponent>(m_SelectedEntity, primary) || !primary) return;

		const Vec2 worldDelta{ newWorldPos.x - primary->Position.x, newWorldPos.y - primary->Position.y };

		const std::vector<EntityHandle> roots = FilterSelectedHierarchyRoots(scene, GetSelectedEntities(scene));
		bool primaryIsRoot = false;
		for (EntityHandle handle : roots) {
			if (handle == m_SelectedEntity) { primaryIsRoot = true; break; }
		}

		// Full transform on the gizmo's entity (the primary). When the primary is a
		// descendant of another selected root, that root's move already carries it,
		// so keep its world position and apply only rotation/scale here — otherwise
		// it would translate twice (explicitly + via the ancestor).
		{
			Transform2DComponent* parentTransform = nullptr;
			const Entity parent = scene.GetEntity(m_SelectedEntity).GetParent();
			if (parent.IsValid()) {
				scene.TryGetComponent<Transform2DComponent>(parent.GetHandle(), parentTransform);
			}
			const Vec2 primaryTarget = primaryIsRoot
				? newWorldPos
				: Vec2{ primary->Position.x, primary->Position.y };
			SetLocalTransformFromWorld(*primary, parentTransform, primaryTarget, newWorldRotation, newWorldScale);
		}

		// Translation delta carries the rest of the selection (rotate/scale stay on
		// the primary). Move only roots; descendants follow through the hierarchy.
		if (worldDelta.x != 0.0f || worldDelta.y != 0.0f) {
			for (EntityHandle handle : roots) {
				if (handle == m_SelectedEntity) continue;
				Transform2DComponent* transform = nullptr;
				if (!scene.TryGetComponent<Transform2DComponent>(handle, transform) || !transform) continue;
				Transform2DComponent* parentTransform = nullptr;
				const Entity parent = scene.GetEntity(handle).GetParent();
				if (parent.IsValid()) {
					scene.TryGetComponent<Transform2DComponent>(parent.GetHandle(), parentTransform);
				}
				const Vec2 target{ transform->Position.x + worldDelta.x, transform->Position.y + worldDelta.y };
				SetLocalTransformFromWorld(*transform, parentTransform, target, transform->Rotation, transform->Scale);
			}
		}

		TransformHierarchySystem::Propagate(scene);
		scene.MarkDirty();
	}

	void ImGuiEditorLayer::ApplyGroupRotationScale(Scene& scene, const Vec2& pivot,
		float deltaAngle, const Vec2& scaleFactor) {
		const bool hasScale = (scaleFactor.x != 1.0f || scaleFactor.y != 1.0f);
		const bool hasRotation = (deltaAngle != 0.0f);
		if (!hasScale && !hasRotation) return;

		TransformHierarchySystem::Propagate(scene);

		// Transform every selected root about the pivot; descendants follow rigidly
		// via the hierarchy, so a selected child of a selected parent isn't moved
		// twice. Scale is along world axes — exact for unrotated entities and for
		// uniform scale; rotated entities get a reasonable approximation.
		for (EntityHandle handle : FilterSelectedHierarchyRoots(scene, GetSelectedEntities(scene))) {
			Transform2DComponent* transform = nullptr;
			if (!scene.TryGetComponent<Transform2DComponent>(handle, transform) || !transform) continue;

			Vec2 worldPos = transform->Position;
			float worldRot = transform->Rotation;
			Vec2 worldScale = transform->Scale;

			if (hasScale) {
				worldPos = { pivot.x + (worldPos.x - pivot.x) * scaleFactor.x,
							 pivot.y + (worldPos.y - pivot.y) * scaleFactor.y };
				worldScale = { worldScale.x * scaleFactor.x, worldScale.y * scaleFactor.y };
			}
			if (hasRotation) {
				worldPos = pivot + Rotate(worldPos - pivot, deltaAngle);
				worldRot += deltaAngle;
			}

			Transform2DComponent* parentTransform = nullptr;
			const Entity parent = scene.GetEntity(handle).GetParent();
			if (parent.IsValid()) {
				scene.TryGetComponent<Transform2DComponent>(parent.GetHandle(), parentTransform);
			}
			SetLocalTransformFromWorld(*transform, parentTransform, worldPos, worldRot, worldScale);
		}

		TransformHierarchySystem::Propagate(scene);
		scene.MarkDirty();
	}

	void ImGuiEditorLayer::ApplyUIGroupTranslate(Scene& scene, const Vec2& deltaCanvas) {
		if (deltaCanvas.x == 0.0f && deltaCanvas.y == 0.0f) return;

		// Move every selected UI root by the same canvas-space delta; descendants
		// follow through the layout. AnchoredPosition lives in the parent's space, so
		// divide the shared delta by each root's parentScale (world / local scale).
		for (EntityHandle handle : FilterSelectedHierarchyRoots(scene, GetSelectedEntities(scene))) {
			RectTransform2DComponent* rect = nullptr;
			if (!scene.TryGetComponent<RectTransform2DComponent>(handle, rect) || !rect) continue;
			if (scene.HasComponent<Transform2DComponent>(handle)) continue;

			const Vec2 parentScale{
				rect->LocalScale.x != 0.0f ? rect->Scale.x / rect->LocalScale.x : rect->Scale.x,
				rect->LocalScale.y != 0.0f ? rect->Scale.y / rect->LocalScale.y : rect->Scale.y
			};
			if (parentScale.x != 0.0f) rect->AnchoredPosition.x += deltaCanvas.x / parentScale.x;
			if (parentScale.y != 0.0f) rect->AnchoredPosition.y += deltaCanvas.y / parentScale.y;
		}
		scene.MarkDirty();
	}

	void ImGuiEditorLayer::ApplyUIGroupRotationScale(Scene& scene, const Vec2& pivotWorld,
		float deltaAngle, const Vec2& scaleFactor, float worldScale) {
		const bool hasScale = (scaleFactor.x != 1.0f || scaleFactor.y != 1.0f);
		const bool hasRotation = (deltaAngle != 0.0f);
		if ((!hasScale && !hasRotation) || worldScale == 0.0f) return;

		// Transform every selected UI root about the group pivot (world space). Each
		// root's pivot is orbited/scaled about `pivotWorld`; the resulting world
		// displacement folds back into AnchoredPosition, and the increment lands on
		// LocalRotation / LocalScale. For a single selection the pivot is the entity's
		// own, so the displacement is zero and only the authored field changes.
		for (EntityHandle handle : FilterSelectedHierarchyRoots(scene, GetSelectedEntities(scene))) {
			RectTransform2DComponent* rect = nullptr;
			if (!scene.TryGetComponent<RectTransform2DComponent>(handle, rect) || !rect) continue;
			if (scene.HasComponent<Transform2DComponent>(handle)) continue;

			const Vec2 bl = rect->GetBottomLeft();
			const Vec2 tr = rect->GetTopRight();
			const Vec2 pivotCanvas = rect->ResolvedValid
				? rect->ResolvedPivot
				: Vec2{ (bl.x + tr.x) * 0.5f, (bl.y + tr.y) * 0.5f };
			const Vec2 origWorld{ pivotCanvas.x * worldScale, pivotCanvas.y * worldScale };

			Vec2 p = origWorld;
			if (hasScale) {
				p = { pivotWorld.x + (p.x - pivotWorld.x) * scaleFactor.x,
					  pivotWorld.y + (p.y - pivotWorld.y) * scaleFactor.y };
			}
			if (hasRotation) {
				p = pivotWorld + Rotate(p - pivotWorld, deltaAngle);
			}

			const Vec2 deltaCanvas{ (p.x - origWorld.x) / worldScale, (p.y - origWorld.y) / worldScale };
			const Vec2 parentScale{
				rect->LocalScale.x != 0.0f ? rect->Scale.x / rect->LocalScale.x : rect->Scale.x,
				rect->LocalScale.y != 0.0f ? rect->Scale.y / rect->LocalScale.y : rect->Scale.y
			};
			if (parentScale.x != 0.0f) rect->AnchoredPosition.x += deltaCanvas.x / parentScale.x;
			if (parentScale.y != 0.0f) rect->AnchoredPosition.y += deltaCanvas.y / parentScale.y;

			if (hasRotation) rect->LocalRotation += deltaAngle;
			if (hasScale) {
				rect->LocalScale.x *= scaleFactor.x;
				rect->LocalScale.y *= scaleFactor.y;
			}
		}
		scene.MarkDirty();
	}

	void ImGuiEditorLayer::BeginGizmoTransformDrag(Scene& scene) {
		// New drag — clear any alignment-snap offset left over from the previous one.
		m_AlignSnapDebt = Vec2{ 0.0f, 0.0f };

		// Recording during play is pointless — those edits are discarded on Stop.
		if (Application::GetIsPlaying()) return;

		m_GizmoDragActive = true;
		m_GizmoDragBefore.clear();
		for (EntityHandle handle : GetSelectedEntities(scene)) {
			const uint64_t id = scene.GetEntityPersistentID(handle);
			if (id == 0) continue;
			EntityTransformSnapshot snap = CaptureEntityTransform(scene, handle);
			if (!snap.Valid) continue;
			m_GizmoDragBefore.push_back({ id, snap });
		}
	}

	void ImGuiEditorLayer::CommitGizmoTransformDrag(Scene& scene) {
		m_GizmoDragActive = false;
		if (m_GizmoDragBefore.empty()) return;

		std::vector<TransformEditCommand::Entry> entries;
		entries.reserve(m_GizmoDragBefore.size());
		for (const auto& [id, before] : m_GizmoDragBefore) {
			EntityHandle handle = entt::null;
			if (!scene.TryResolveEntityRef(id, handle)) continue;
			EntityTransformSnapshot after = CaptureEntityTransform(scene, handle);
			if (!after.Valid) continue;
			TransformEditCommand::Entry entry;
			entry.PersistentId = id;
			entry.Before = before;
			entry.After = after;
			entries.push_back(entry);
		}
		m_GizmoDragBefore.clear();

		const char* label = m_GizmoMode == EditorGizmoMode::Rotate ? "Rotate"
			: m_GizmoMode == EditorGizmoMode::Scale ? "Scale"
			: m_GizmoMode == EditorGizmoMode::Bounds ? "Resize" : "Move";
		auto command = std::make_unique<TransformEditCommand>(std::move(entries), label);
		if (command->HasChange()) {
			GetUndoStack(scene).Push(std::move(command));
		}
	}

	UndoStack& ImGuiEditorLayer::GetUndoStack(Scene& scene) {
		UndoStack* stack = &m_DetachedUndoStack;
		for (const auto& weak : SceneManager::GetLoadedScenes()) {
			if (const auto owner = weak.lock(); owner && owner.get() == &scene) {
				stack = &m_SceneUndoHistory.ForScene(owner);
				break;
			}
		}
		if (m_UndoStackSceneId != &scene || m_ActiveUndoStack != stack) {
			if (m_ActiveUndoStack) InspectorUndoRecorder::Flush(*m_ActiveUndoStack, true);
			InspectorUndoRecorder::Reset();
			m_UndoStackSceneId = &scene;
			m_ActiveUndoStack = stack;
		}
		return *stack;
	}

	void ImGuiEditorLayer::ClearUndoHistory() {
		InspectorUndoRecorder::Reset();
		m_ActiveUndoStack = nullptr;
		m_UndoStackSceneId = nullptr;
		m_SceneUndoHistory.Clear();
		m_DetachedUndoStack.Clear();
	}

	void ImGuiEditorLayer::ForgetUndoHistory(const Scene* scene) {
		if (m_UndoStackSceneId == scene) {
			InspectorUndoRecorder::Reset();
			m_ActiveUndoStack = nullptr;
			m_UndoStackSceneId = nullptr;
		}
		m_SceneUndoHistory.Forget(scene);
		if (scene == m_PrefabEditScene.get()) m_DetachedUndoStack.Clear();
	}

	void ImGuiEditorLayer::PerformUndoRedo(Scene& scene, bool redo) {
		FinishInspectorRename(scene);
		std::vector<uint64_t> selectedIds;
		for (EntityHandle h : GetSelectedEntities(scene)) selectedIds.push_back(scene.GetEntityPersistentID(h));
		const uint64_t primaryId = (m_SelectedEntity != entt::null && scene.IsValid(m_SelectedEntity))
			? scene.GetEntityPersistentID(m_SelectedEntity) : 0;
		const uint64_t prefabRootId = m_PrefabEditScene.get() == &scene && scene.IsValid(m_PrefabEditRootEntity)
			? scene.GetEntityPersistentID(m_PrefabEditRootEntity) : 0;

		UndoStack& history = GetUndoStack(scene);
		InspectorUndoRecorder::Flush(history, true);
		const std::string action = redo ? history.Redo(scene) : history.Undo(scene);
		if (action.empty()) return;
		if (prefabRootId != 0) scene.TryResolveEntityRef(prefabRootId, m_PrefabEditRootEntity);

		// Structural steps rebuild entities with new handles; re-resolve the selection by persistent id.
		m_SelectedEntities.clear();
		for (uint64_t id : selectedIds) {
			EntityHandle h = entt::null;
			if (scene.TryResolveEntityRef(id, h) && scene.IsValid(h)) m_SelectedEntities.push_back(h);
		}
		EntityHandle primary = entt::null;
		if (primaryId != 0 && scene.TryResolveEntityRef(primaryId, primary) && scene.IsValid(primary)) {
			m_SelectedEntity = primary;
		}
		else {
			m_SelectedEntity = m_SelectedEntities.empty() ? entt::null : m_SelectedEntities.back();
		}
		RebuildSelectionSet();
		m_PressedEntity = entt::null;
		m_EntityOrder.clear();
		m_EntityOrderDirty = true;
		++m_SelectionVersion;
		ShowUndoToast(action, redo);
	}

	void ImGuiEditorLayer::ProcessUndoRedoShortcuts() {
		const auto action = EditorShortcuts::ReadHistoryAction(Application::GetIsPlaying()
			|| m_PlayModeRecompilePending || m_RenamingEntity != entt::null
			|| m_BuildState > 0 || m_ReserializeState > 0 || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId));
		if (action == EditorShortcuts::HistoryAction::None) return;
		if (Scene* scene = GetContextScene()) {
			PerformUndoRedo(*scene, action == EditorShortcuts::HistoryAction::Redo);
		}
	}

	void ImGuiEditorLayer::ShowUndoToast(const std::string& action, bool redo) {
		if (action.empty()) return;
		m_UndoToastText = (redo ? "Redo: " : "Undo: ") + action;
		m_UndoToastRedo = redo;
		m_UndoToastElapsed = 0.0f;
		m_UndoToastActive = true;
	}

	void ImGuiEditorLayer::DrawUndoToast() {
		if (!m_UndoToastActive || m_EditorViewImageW <= 0.0f) return;

		constexpr float fadeIn = 0.15f, hold = 1.3f, fadeOut = 0.5f;
		const float total = fadeIn + hold + fadeOut;
		const float t = m_UndoToastElapsed;
		float alpha = 1.0f;
		if (t < fadeIn) alpha = t / fadeIn;
		else if (t < fadeIn + hold) alpha = 1.0f;
		else if (t < total) alpha = 1.0f - (t - fadeIn - hold) / fadeOut;
		else alpha = 0.0f;
		alpha = std::clamp(alpha, 0.0f, 1.0f);
		if (alpha <= 0.0f) return;

		ImDrawList* dl = ImGui::GetForegroundDrawList();

		constexpr float iconSize = 18.0f;
		constexpr float padX = 12.0f, padY = 8.0f, gap = 8.0f;
		const ImVec2 textSize = ImGui::CalcTextSize(m_UndoToastText.c_str());
		const float boxW = iconSize + gap + textSize.x + padX * 2.0f;
		const float boxH = std::max(iconSize, textSize.y) + padY * 2.0f;
		const float boxX = m_EditorViewImageX + (m_EditorViewImageW - boxW) * 0.5f;
		const float boxY = m_EditorViewImageY + 18.0f;

		const int a = static_cast<int>(255.0f * alpha);
		const ImU32 bg = ImGui::GetColorU32(ImGuiCol_PopupBg, 0.92f * alpha);
		const ImU32 border = ImGui::GetColorU32(ImGuiCol_Border, alpha);
		const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text, alpha);
		const ImU32 tint = IM_COL32(255, 255, 255, a);

		dl->AddRectFilled(ImVec2(boxX, boxY), ImVec2(boxX + boxW, boxY + boxH), bg, 6.0f);
		dl->AddRect(ImVec2(boxX, boxY), ImVec2(boxX + boxW, boxY + boxH), border, 6.0f);

		const float iconX = boxX + padX;
		const float iconY = boxY + (boxH - iconSize) * 0.5f;
		const uint64_t icon = EditorIcons::Get(m_UndoToastRedo ? "redo" : "undo", 16);
		if (icon != 0) {
			dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(icon)),
				ImVec2(iconX, iconY), ImVec2(iconX + iconSize, iconY + iconSize),
				ImVec2(0, 1), ImVec2(1, 0), tint);
		}
		const float textX = iconX + iconSize + gap;
		const float textY = boxY + (boxH - textSize.y) * 0.5f;
		dl->AddText(ImVec2(textX, textY), textCol, m_UndoToastText.c_str());
	}



	// ──────────────────────────────────────────────
	//  Lifecycle
	// ──────────────────────────────────────────────

	void ImGuiEditorLayer::OnAttach(Application& app) {
		// Start the splash first: SignalSplashAttached (inside Begin) must run
		// before Application's deferred-scene check so the project scene load is
		// held behind the splash.
		m_Splash.Begin();
		Application::SetIsPlaying(false);
		ApplicationEditorAccess::SetGameInputEnabled(false);
		// Scope the project's custom game cursor to the Game View: disable engine
		// cursor application now (before Application applies the project cursor in
		// Initialize) so it never goes editor-wide. OnUpdate re-enables it only
		// while the Game View is hovered/focused.
		if (Window* w = app.GetWindow()) {
			w->SetGameCursorEnabled(false);
		}
		Gizmo::SetShowInRuntime(false);
		if (app.GetRenderer2D()) {
			app.GetRenderer2D()->SetSkipBeginFrameRender(true);
		}
		// UI must render into the per-panel FBO, not the OS backbuffer; RenderSceneIntoFBO drives this manually.
		if (app.GetGuiRenderer()) {
			app.GetGuiRenderer()->SetSkipBeginFrameRender(true);
		}

		if (m_LogSubscriptionId.value != 0) {
			Log::OnLog.Remove(m_LogSubscriptionId);
		}

		m_ProfilerPanel.Initialize();
		m_SpriteEditorPanel.Initialize();
		m_ScriptWindows.Initialize();

		// Load BEFORE ApplyTheme: ImGuiContextLayer already ran ApplyFunkeTheme on its OnAttach; this reskins the live style with the user's saved choice.
		EditorPreferences::Load();
		EditorPreferences::ApplyTheme();
		m_EditorPreferencesPanel.Initialize();

		// One-time migration: seed EditorPreferences from legacy inline project fields if no prefs file existed yet.
		if (EditorPreferences::WasFreshlyCreated()) {
			if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
				const auto& lp = project->LegacyEditorPrefs;
				if (lp.ShowFileExtensionsPresent) {
					EditorPreferences::SetShowFileExtensions(lp.ShowFileExtensions);
				}
				if (lp.AutoSaveScenesPresent) {
					EditorPreferences::SetAutoSaveScenes(lp.AutoSaveScenes);
				}
				if (lp.AutoSaveIntervalSecondsPresent) {
					EditorPreferences::SetAutoSaveIntervalSeconds(lp.AutoSaveIntervalSeconds);
				}
				if (lp.AutoRecompileScriptsPresent) {
					EditorPreferences::SetAutoRecompileScripts(lp.AutoRecompileScripts);
				}
				if (lp.RecompileScriptsOnPlayPresent) {
					EditorPreferences::SetRecompileScriptsOnPlay(lp.RecompileScriptsOnPlay);
				}
			}
		}
		ScriptSystem::SetAutoRecompileEnabled(EditorPreferences::GetAutoRecompileScripts());

		ClearLogEntries();
		m_LogDispatchState = std::make_shared<LogDispatchState>();
		std::weak_ptr<LogDispatchState> weakLogDispatchState = m_LogDispatchState;
		m_LogSubscriptionId = Log::OnLog.Add([weakLogDispatchState](const Log::Entry& entry) {
			if (entry.Source == Log::Type::Core) return; // Core logs go to stdout only, not editor UI

			std::shared_ptr<LogDispatchState> state = weakLogDispatchState.lock();
			if (!state) return;

			// try_lock to avoid blocking worker threads; entries that miss the lock go to a per-thread buffer and are drained on the next successful lock (entries from threads that never log again are lost, acceptable).
			thread_local std::vector<LogEntry> tlBuf;

			// Stamp at receipt (emit time) so ordering survives the buffered/deferred path.
			LogEntry record{ entry.Message, entry.Level };
			record.Timestamp = FormatLogClockTimestamp();

			if (state->Mutex.try_lock()) {
				std::lock_guard<std::mutex> guard(state->Mutex, std::adopt_lock);
				if (!tlBuf.empty()) {
					state->PendingEntries.insert(state->PendingEntries.end(),
						std::make_move_iterator(tlBuf.begin()),
						std::make_move_iterator(tlBuf.end()));
					tlBuf.clear();
				}
				state->PendingEntries.push_back(std::move(record));
			}
			else {
				tlBuf.push_back(std::move(record));
			}
			});
	}

	void ImGuiEditorLayer::OnDetach(Application& app) {
		PropertyDrawer::ClearTransientState();
		(void)app;
		ClearUndoHistory();
		m_PlayModeUndoHistory.clear();
		// Drain the async workers first — their lambdas write member state (the progress mutexes) that
		// the imminent destructor would otherwise free out from under a still-running thread. The future
		// destructors already block for the same duration; this just joins while the members are alive.
		Process::CancelAll();
		auto joinWorker = [](std::future<void>& worker) {
			if (!worker.valid()) return;
			while (worker.wait_for(std::chrono::milliseconds(25)) != std::future_status::ready) {
				Process::CancelAll();
			}
			try { worker.get(); } catch (...) {}
		};
		joinWorker(m_ReserializeFuture);
		joinWorker(m_BuildFuture);
		// MUST precede WebGPU teardown: Texture2D destructor touches the device.
		m_Splash.Shutdown();
		m_ProfilerPanel.Shutdown();
		m_ScriptWindows.Shutdown();
		m_SpriteEditorPanel.Shutdown();
		m_EditorPreferencesPanel.Shutdown();
		ApplicationEditorAccess::SetGameInputEnabled(true);
		Gizmo::SetShowInRuntime(true);
		if (m_LogSubscriptionId.value != 0) {
			Log::OnLog.Remove(m_LogSubscriptionId);
			m_LogSubscriptionId = EventId{};
		}
		m_AssetWatcher.Stop();
		m_ContentWatcher.Stop();
		m_LocalizationWatcher.Stop();
		m_ProjectFileWatcher.Stop();
		m_AssetWatcherRoot.clear();
		m_LogDispatchState.reset();
		// Reset before further teardown: destructor unsubscribes from Log::OnLog, which must run while the layer is still live.
		m_GameViewLogOverlay.reset();
		ClearPreviewTextureCache();

		// Reverse construction order: panels → icons → FBOs.
		m_AssetBrowser.Shutdown();
		m_PackageManagerPanel.Shutdown();
		m_PackageManager.Shutdown();
		ExternalEditor::JoinPendingLaunchThreads();
		ReferencePicker::Shutdown();
		ScriptComponentInspector::Shutdown();
		EditorIcons::Shutdown();
		Icons::Shutdown();
		m_EditorViewFBO.Destroy();
		m_GameViewFBO.Destroy();
	}

	void ImGuiEditorLayer::OnEvent(Application& /*app*/, FunkeEvent& event) {
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowFocusEvent>([this](WindowFocusEvent&) {
			m_AssetBrowser.RequestRefresh();
			return false;
			});
		dispatcher.Dispatch<WindowLostFocusEvent>([this](WindowLostFocusEvent&) {
			// Coming back to the editor must not grab the mouse; a click into the Game View takes it again.
			if (Application::GetIsPlaying()) m_GameCursorReleased = true;
			return false;
			});

		// Scene swap: must clear all editor entity handles (selection, order cache, drag state) before any consumer sees the new world, or stale handles reach the hierarchy clipper and cause asserts.
		auto resetEditorEntityState = [this]() {
			m_SelectedEntity = entt::null;
			m_SelectedEntities.clear();
			m_SelectedEntitySet.clear();
			m_PressedEntity = entt::null;
			m_RenamingEntity = entt::null;
			m_EntityRenameFrameCounter = 0;
			m_EntityOrder.clear();
			m_RenderedEntityOrder.clear();
			m_RenderedEntityDepths.clear();
			m_VisibleEntityOrder.clear();
			m_VisibleEntityDepths.clear();
			m_EntityOrderDirty = true;
			m_EntityOrderSceneId = nullptr;
			m_CollapsedHierarchyEntities.clear();
			m_CutEntities.clear();
			m_LastInteractedSceneName.clear();
			++m_SelectionVersion;
		};
		dispatcher.Dispatch<ScenePreStopEvent>([&resetEditorEntityState](ScenePreStopEvent&) {
			resetEditorEntityState();
			return false;
			});
		dispatcher.Dispatch<ScenePostStartEvent>([&resetEditorEntityState](ScenePostStartEvent&) {
			// Belt-and-braces: additive loads skip PreStop, so PostStart must also clear.
			resetEditorEntityState();
			return false;
			});
	}

	void ImGuiEditorLayer::OnUpdate(Application& app, float dt) {
		// As in Unity, the game owns the mouse (lock/hide, custom cursor) only while it plays in a focused Game View.
		// Escape or switching apps hands the plain OS cursor back to the editor without touching what the game asked
		// for; focusing or clicking into the Game View applies the game's settings again.
		// Uses last frame's hover/focus — a 1-frame settle is imperceptible.
		if (Window* win = app.GetWindow()) {
			ImGuiIO& io = ImGui::GetIO();
			const bool playing = Application::GetIsPlaying();
			// Only a running game may hold the mouse; anything left locked outside play mode would trap the editor.
			if (!playing && win->GetCursorMode() != 0) win->SetCursorMode(0);
			const int requestedMode = win->GetCursorMode();
			const bool gameViewFocusGained = m_IsGameViewFocused && !m_WasGameViewFocused;
			// A fresh lock from the game (e.g. closing its pause menu with Escape) takes the mouse back too.
			const bool freshLock = m_IsGameViewFocused && requestedMode != 0 && m_CursorModeLastUpdate == 0;
			if (!playing || gameViewFocusGained || m_GameViewCanvasClicked || freshLock)
				m_GameCursorReleased = false;
			else if (requestedMode != 0 && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
				m_GameCursorReleased = true;
			m_GameViewCanvasClicked = false;
			m_WasGameViewFocused = m_IsGameViewFocused;
			m_CursorModeLastUpdate = requestedMode;
			win->SetCursorModeSuspended(!playing || !m_IsGameViewFocused || m_GameCursorReleased);

			// A locked/hidden cursor is parked on the Game View's centre (Window::GetCursorLockPoint); ImGui's pointer is
			// held there too, so clicks while playing reach the Game View and no other panel.
			const bool captured = win->GetAppliedCursorMode() != 0;
			if (captured) {
				Vec2 lockPoint = win->GetCursorLockPoint();
				if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
					const Vec2Int windowPosition = win->GetPosition();
					lockPoint.x += static_cast<float>(windowPosition.x);
					lockPoint.y += static_cast<float>(windowPosition.y);
				}
				io.AddMousePosEvent(lockPoint.x, lockPoint.y);
			}
			const bool customCursorHere = win->HasGameCursor() && !m_GameCursorReleased
				&& (m_IsGameViewHovered || m_IsGameViewFocused);
			// Without NoMouseCursorChange the SDL3 backend calls SDL_ShowCursor every frame and undoes a hidden cursor.
			if (customCursorHere || captured)
				io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
			else
				io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
			if (win->HasGameCursor())
				win->SetGameCursorEnabled(customCursorHere);
		}

		DrainPendingLogEntries();
		RunAutoSaveTick(app, dt);
		// Held back until the startup prompt is answered so it can't overwrite copies awaiting recovery.
		if (m_RecoveryChecked && m_PendingRecovery.empty()) {
			constexpr float k_RecoveryIntervalSeconds = 30.0f;
			m_RecoveryAccumulator += dt;
			if (m_RecoveryAccumulator >= k_RecoveryIntervalSeconds) {
				m_RecoveryAccumulator = 0.0f;
				SceneRecovery::SnapshotDirtyScenes();
			}
		}
		// MUST precede RunPrefabAutoSaveTick: if the prefab file was deleted, exit edit mode first so auto-save doesn't recreate it at a stale path.
		if (m_PrefabEditScene && !m_PrefabEditPath.empty() && !File::Exists(m_PrefabEditPath)) {
			ClosePrefabEditing(false);
		}
		RunPrefabAutoSaveTick();

		if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
			if (!project->AssetsDirectory.empty() && project->AssetsDirectory != m_AssetWatcherRoot) {
				m_AssetWatcherRoot = project->AssetsDirectory;
				m_AssetWatcher.Watch(
					std::vector<std::string>{ m_AssetWatcherRoot },
					// .meta too: a GUID read while its .meta was mid-write stays "missing" until the registry is marked dirty again.
					std::vector<std::string>{ ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".meta" },
					[this]() {
						if (m_AssetBrowser.IsImportingAssets()
							|| m_AssetBrowser.IsDeleting()
							|| m_AssetBrowser.ConsumeAssetWatcherImportRefreshSuppression()) {
							return;
						}
						const size_t reloaded = TextureManager::ReloadTexturesFromDisk();
						SkyAmbient::ClearCache();
						// Slice rects and borders may come from an externally edited .meta.
						NotifySpriteSliceEpochBumped();
						AssetRegistry::MarkDirty();
						// Loads that failed on a half-written file get another try, and templates baked from them are dropped.
						TextureManager::ResolveMissingSceneTextures();
						PrefabTemplateCache::Get().InvalidateAll();
						m_AssetBrowser.RequestRefresh();
						// Clear thumbnail tile cache too — otherwise the asset
						// browser keeps showing the pre-edit file icon until
						// LRU evicts the stale entry.
						m_AssetBrowser.InvalidateAllThumbnails();
						ClearPreviewTextureCache();
						if (reloaded > 0) {
							FNK_INFO_TAG("Editor", "Hot-reloaded {} texture(s)", reloaded);
						}
					});
				m_ContentWatcher.Watch(
					std::vector<std::string>{ m_AssetWatcherRoot },
					std::vector<std::string>{ ".scene", ".prefab", ".wav", ".mp3", ".flac", ".ttf", ".otf", ".dataasset", ".material", ".obj", ".gltf", ".glb", ".anim", ".animctrl" },
					[this]() { ReloadExternallyChangedContent(); });
				// The whole Assets tree: the Localization folder may not exist yet. Reload is a no-op when the tables didn't change.
				m_LocalizationWatcher.Watch(
					std::vector<std::string>{ m_AssetWatcherRoot },
					std::vector<std::string>{ ".json" },
					[]() { GameLocalization::Reload(); });
				m_ProjectFileWatcher.Watch(
					std::vector<std::string>{ project->ProjectFilePath, project->GetBuildProfilesDirectory() },
					std::vector<std::string>{ "project.json", ".build" },
					[this]() { m_ExternalProjectCheckPending = true; },
					/*recursive=*/false);
			}
			if (!m_AssetBrowser.IsImportingAssets() && !m_AssetBrowser.IsDeleting()) {
				m_AssetWatcher.Poll(0.25f);
				m_ContentWatcher.Poll(0.25f);
			}
			m_LocalizationWatcher.Poll(0.25f);
			m_ProjectFileWatcher.Poll(0.25f);
			const bool canApplyExternalChanges = !Application::GetIsPlaying()
				&& m_BuildState == 0 && m_ReserializeState == 0;
			if (m_ExternalSceneCheckPending && canApplyExternalChanges) {
				m_ExternalSceneCheckPending = false;
				CheckExternalSceneChanges();
			}
			if (m_ExternalPrefabCheckPending && canApplyExternalChanges) {
				m_ExternalPrefabCheckPending = false;
				CheckExternalPrefabChanges();
			}
			if (m_ExternalProjectCheckPending && canApplyExternalChanges) {
				m_ExternalProjectCheckPending = false;
				CheckExternalProjectChanges();
			}
		}
		else if (!m_AssetWatcherRoot.empty()) {
			m_AssetWatcher.Stop();
			m_ContentWatcher.Stop();
			m_LocalizationWatcher.Stop();
			m_ProjectFileWatcher.Stop();
			m_AssetWatcherRoot.clear();
			m_ExternalSceneConflicts.clear();
			m_PendingPrefabRefreshes.clear();
		}

		// Script-driven Application.Quit() in editor routes through the same stop path as the Stop button.
		if (Application::GetIsPlaying() && ApplicationEditorAccess::ConsumeQuitStopPlayRequest()) {
			RequestPlayModeStop();
		}
		ProcessPendingPlayModeStop();
		// Click-to-edit-frame latency: the stop itself plus the first edit-mode frame (texture/pipeline warm-up).
		if (m_PlayModeStopFramesToMeasure > 0 && --m_PlayModeStopFramesToMeasure == 0) {
			FNK_INFO_TAG("PlayMode", "Stop to second edit frame: {:.1f} ms", std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - m_PlayModeStopRequestTime).count());
		}

		Scene* activeScene = app.GetSceneManager() ? SceneManager::GetActiveScene() : nullptr;
		if (!activeScene) {
			ClearUndoHistory();
			return;
		}

		// CRITICAL: validate against GetContextScene(), not activeScene — in prefab-edit mode the selection lives in the detached prefab registry; using activeScene would clear the selection every frame.
		Scene* selectionContextScene = GetContextScene();
		if (!selectionContextScene) selectionContextScene = activeScene;
		Scene& selectionScene = *selectionContextScene;

		UndoStack& history = GetUndoStack(selectionScene);
		m_SceneUndoHistory.Retain(SceneManager::GetLoadedScenes());
		InspectorUndoRecorder::Flush(history);

		// Advance the undo/redo toast fade (fade-in 0.15 + hold 1.3 + fade-out 0.5).
		if (m_UndoToastActive) {
			m_UndoToastElapsed += dt;
			if (m_UndoToastElapsed >= 1.95f) m_UndoToastActive = false;
		}

		if (m_SelectedEntity != entt::null && !selectionScene.IsValid(m_SelectedEntity)) {
			m_SelectedEntity = entt::null;
		}
		if (m_PressedEntity != entt::null && !selectionScene.IsValid(m_PressedEntity)) {
			m_PressedEntity = entt::null;
		}
		if (!m_SelectedEntities.empty()) {
			auto isStale = [&selectionScene](EntityHandle h) {
				return h == entt::null || !selectionScene.IsValid(h);
			};
			const std::size_t before = m_SelectedEntities.size();
			m_SelectedEntities.erase(
				std::remove_if(m_SelectedEntities.begin(), m_SelectedEntities.end(), isStale),
				m_SelectedEntities.end());
			if (m_SelectedEntities.size() != before) {
				RebuildSelectionSet();
			}
		}
		UpdateEditorCameraFocus(dt);
		const bool hasShortcutFocus = HasEntityShortcutFocus();
		const ImGuiIO& io = ImGui::GetIO();
		// Shortcut handlers use the context scene so they work on prefab entities in prefab-edit mode.
		Scene& shortcutScene = selectionScene;
		const bool hasEntitySelection = !GetSelectedEntities(shortcutScene).empty();

		if (!io.WantTextInput && !ImGui::IsAnyItemActive()
			&& ImGui::IsKeyChordPressed(ImGuiKey_F) && hasShortcutFocus && hasEntitySelection) {
			FocusSelectedEntity(shortcutScene);
		}

		if (!hasShortcutFocus) {
			return;
		}
		if (m_RenamingEntity != entt::null || io.WantTextInput || ImGui::IsAnyItemActive()) {
			return;
		}

		if (io.KeyShift && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper
			&& ImGui::IsKeyPressed(ImGuiKey_C, false)) {
			FinishCreatedEditorEntity(shortcutScene, Entity::Null, shortcutScene.CreateEmptyEntity());
			return;
		}

		// Arrow-key hierarchy navigation follows the currently rendered row list.
		if (m_IsEntitiesPanelFocused && hasEntitySelection && !m_VisibleEntityOrder.empty()
			&& !io.KeyCtrl && !io.KeySuper && !io.KeyAlt) {
			const int direction = ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) ? 1
				: (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) ? -1 : 0);
			if (direction != 0) {
				int currentIndex = -1;
				for (int i = 0; i < static_cast<int>(m_VisibleEntityOrder.size()); ++i) {
					if (m_VisibleEntityOrder[static_cast<std::size_t>(i)] == m_SelectedEntity) {
						currentIndex = i;
						break;
					}
				}

				if (currentIndex < 0) {
					for (EntityHandle selected : GetSelectedEntities(shortcutScene)) {
						for (int i = 0; i < static_cast<int>(m_VisibleEntityOrder.size()); ++i) {
							if (m_VisibleEntityOrder[static_cast<std::size_t>(i)] == selected) {
								currentIndex = i;
								break;
							}
						}
						if (currentIndex >= 0) break;
					}
				}

				if (currentIndex >= 0) {
					const int targetIndex = std::clamp(
						currentIndex + direction,
						0,
						static_cast<int>(m_VisibleEntityOrder.size()) - 1);
					const EntityHandle target = m_VisibleEntityOrder[static_cast<std::size_t>(targetIndex)];
					if (target != entt::null && shortcutScene.IsValid(target)) {
						SetSingleEntitySelection(target, targetIndex);
					}
				}
			}
		}

		// Use io.KeyCtrl + ImGui::IsKeyPressed (not raw Input): covers right Ctrl/Cmd and prevents auto-repeat.
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && hasEntitySelection) {
			CopySelectedEntities(shortcutScene);
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false) && hasEntitySelection) {
			CutSelectedEntities(shortcutScene);
			return;
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
			PasteEntities(shortcutScene);
		}
		// Esc cancels a pending Cut: entities stay alive and un-dim. The
		// clipboard JSON itself is left intact so Ctrl+V still pastes a
		// copy, matching the behaviour of typical OS file managers.
		if (!m_CutEntities.empty() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)
			&& m_RenamingEntity == entt::null) {
			m_CutEntities.clear();
		}
		if (!hasEntitySelection) {
			return;
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
			DuplicateSelectedEntity(shortcutScene);
		}
		if (EditorShortcuts::IsDeletePressed()) {
			RequestDeleteSelectedEntity(shortcutScene);
		}
		if (EditorShortcuts::IsRenamePressed()) {
			BeginRenameSelectedEntity(shortcutScene);
		}
	}

	void ImGuiEditorLayer::DrainPendingLogEntries() {
		if (!m_LogDispatchState) {
			return;
		}

		std::vector<LogEntry> pendingEntries;
		{
			std::scoped_lock lock(m_LogDispatchState->Mutex);
			if (m_LogDispatchState->PendingEntries.empty()) {
				return;
			}

			pendingEntries.swap(m_LogDispatchState->PendingEntries);
		}

		for (LogEntry& entry : pendingEntries) {
			AppendLogEntry(std::move(entry));
		}
	}

	void ImGuiEditorLayer::AppendLogEntry(LogEntry entry) {
		if (entry.Level >= Log::Level::Warn) {
			if (std::optional<LogSourceLocation> location = FindLogSourceLocation(entry.Message)) {
				entry.SourceFilePath = std::move(location->FilePath);
				entry.SourceLinkText = std::move(location->LinkText);
				entry.SourceLine = location->Line;
				entry.SourceLinkStart = location->MessageStart;
				entry.SourceLinkEnd = location->MessageEnd;
			}
		}

		m_LogEntries.push_back(std::move(entry));
		if (m_LogEntries.size() > 2000) {
			m_LogEntries.erase(m_LogEntries.begin(), m_LogEntries.begin() + 500);
			// The erase shifts every surviving entry down by 500; re-pin the selection to the same
			// log line (or drop it if that line was trimmed) so the highlight and Copy stay correct.
			if (m_SelectedLogIndex >= 0) {
				m_SelectedLogIndex = m_SelectedLogIndex >= 500 ? m_SelectedLogIndex - 500 : -1;
			}
		}
	}

	void ImGuiEditorLayer::RunAutoSaveTick(Application& app, float dt) {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project || !EditorPreferences::GetAutoSaveScenes()) {
			m_AutoSaveAccumulator = 0.0f;
			return;
		}

		// Skip in Play mode: the pre-play snapshot was already saved; auto-saving mid-play would persist transient mutations that Stop discards.
		if (Application::GetIsPlaying()) {
			m_AutoSaveAccumulator = 0.0f;
			return;
		}

		// Skip while the reserialization worker owns the .scene/.prefab tree — a write here would race it.
		if (m_ReserializeState != 0) {
			m_AutoSaveAccumulator = 0.0f;
			return;
		}

		// Reset accumulator on scene change so the new scene gets a full interval before its first auto-save.
		Scene* active = app.GetSceneManager() ? SceneManager::GetActiveScene() : nullptr;
		if (!active || active->IsLifecycleSuspended()) {
			m_AutoSaveAccumulator = 0.0f;
			if (!active) m_AutoSaveLastScenePath.clear();
			return;
		}
		const std::string scenePath = project->GetSceneFilePath(active->GetName());
		if (scenePath != m_AutoSaveLastScenePath) {
			m_AutoSaveAccumulator = 0.0f;
			m_AutoSaveLastScenePath = scenePath;
		}

		if (!active->IsDirty()) {
			m_AutoSaveAccumulator = 0.0f;
			return;
		}

		m_AutoSaveAccumulator += dt;
		const float interval = std::max(5.0f, EditorPreferences::GetAutoSaveIntervalSeconds());
		if (m_AutoSaveAccumulator < interval) {
			return;
		}
		m_AutoSaveAccumulator = 0.0f;

		if (scenePath.empty()) {
			return;
		}
		// Never overwrite an external edit unasked; the conflict prompt decides which version wins.
		if (KnownFileStamps::HasChangedOnDisk(scenePath)) {
			m_ExternalSceneCheckPending = true;
			return;
		}
		if (SceneSerializer::SaveToFile(*active, scenePath, false)) {
			active->ClearDirty();
		}
	}

	void ImGuiEditorLayer::RunPrefabAutoSaveTick() {
		if (!m_PrefabEditScene) return;
		if (m_ReserializeState != 0) return; // worker owns the .prefab tree — don't race it
		if (!EditorPreferences::GetAutoSavePrefabs()) return;
		if (!m_PrefabEditScene->IsDirty()) return;
		// Debounce: skip while any ImGui widget is active (e.g. dragging a slider) to avoid thrashing disk on every intermediate value.
		if (ImGui::IsAnyItemActive()) return;
		// Never overwrite an external edit unasked; the conflict prompt decides which version wins.
		if (m_PrefabEditConflict) return;
		if (KnownFileStamps::HasChangedOnDisk(m_PrefabEditPath)) {
			m_ExternalPrefabCheckPending = true;
			return;
		}

		SavePrefabEditChanges();
	}

	void ImGuiEditorLayer::ClearLogEntries() {
		m_LogEntries.clear();
		m_SelectedLogIndex = -1;
		if (m_LogDispatchState) {
			std::scoped_lock lock(m_LogDispatchState->Mutex);
			m_LogDispatchState->PendingEntries.clear();
		}
	}

	// ──────────────────────────────────────────────
	//  Dockspace & Menu
	// ──────────────────────────────────────────────

	template <typename T>
	static bool TryComponentIcon(Scene& scene, EntityHandle handle, const char*& icon) {
		if (!scene.HasComponent<T>(handle)) return false;
		const ComponentInfo* info = SceneManager::GetComponentRegistry().GetInfo<T>();
		if (info && !info->icon.empty()) icon = info->icon.c_str();
		return true;
	}

	// Icon (ComponentInfo::icon) of an entity's dominant component, first match wins; shared by the hierarchy rows and the inspector header.
	static const char* EntityTypeIconName(Scene& scene, EntityHandle handle) {
		const char* icon = "entity";
		TryComponentIcon<Camera2DComponent>(scene, handle, icon)
			|| TryComponentIcon<Camera3DComponent>(scene, handle, icon)
			|| TryComponentIcon<DirectionalLightComponent>(scene, handle, icon)
			|| TryComponentIcon<PointLightComponent>(scene, handle, icon)
			|| TryComponentIcon<SpotLightComponent>(scene, handle, icon)
			|| TryComponentIcon<ParticleSystem2DComponent>(scene, handle, icon)
			|| TryComponentIcon<ParticleSystem3DComponent>(scene, handle, icon)
			|| TryComponentIcon<AudioSourceComponent>(scene, handle, icon)
			|| TryComponentIcon<RectTransform2DComponent>(scene, handle, icon);
		return icon;
	}

	void ImGuiEditorLayer::RenderDockspaceRoot() {
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		// MUST precede dockspace Begin: SetSize above mutates MainViewport that UI systems fall back to.
		const float titlebarH = EditorRuntime::GetTitlebarRowHeight();
		ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + titlebarH));
		ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - titlebarH));
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("EditorDockspace", nullptr, flags);
		ImGui::PopStyleVar(3);

		{
			const float padY = 6.0f;
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, padY));
			ImGui::BeginChild("##EditorToolbar", ImVec2(0.0f, ImGui::GetFrameHeight() + padY * 2.0f),
				ImGuiChildFlags_AlwaysUseWindowPadding,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor();
			const ImVec2 wp = ImGui::GetWindowPos();
			const ImVec2 ws = ImGui::GetWindowSize();
			ImGui::GetWindowDrawList()->AddLine(ImVec2(wp.x, wp.y + ws.y - 1.0f), ImVec2(wp.x + ws.x, wp.y + ws.y - 1.0f),
				ImGui::GetColorU32(ImGuiCol_Border));

			ImGui::BeginDisabled(m_BuildState > 0 || m_ReserializeState > 0);
			RenderToolbarLeft();
			RenderToolbar();
			ImGui::EndDisabled();
			ImGui::EndChild();
		}

		const float statusBarH = std::round(ImGui::GetFrameHeight() + 6.0f);
		ImGuiID dockspaceId = ImGui::GetID("FunkeEditorDockspace");
		// The spacing before the status bar child counts too, or the bar is pushed below the window.
		const float reservedBelow = statusBarH + ImGui::GetStyle().ItemSpacing.y;
		const ImVec2 dockspaceSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - reservedBelow);
		const ImGuiContextLayer::DockLayoutRequest layoutRequest = ImGuiContextLayer::ConsumeDockLayoutRequest();
		if (layoutRequest == ImGuiContextLayer::DockLayoutRequest::Default) {
			EditorDockLayout::BuildDefault(dockspaceId, dockspaceSize);
		}
		else if (layoutRequest == ImGuiContextLayer::DockLayoutRequest::DefaultIfUndocked && !EditorDockLayout::HasDockedPanels(dockspaceId)) {
			FNK_CORE_INFO_TAG("ImGui", "Saved editor layout docks no panels into the editor dockspace; applying the default layout.");
			EditorDockLayout::BuildDefault(dockspaceId, dockspaceSize);
		}
		// A translucent theme would otherwise stack the dockspace's own WindowBg fill between the host and every panel.
		const bool translucent = EditorTheme::IsTranslucent();
		if (translucent) {
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		}
		ImGui::DockSpace(dockspaceId, ImVec2(0.0f, -reservedBelow), ImGuiDockNodeFlags_PassthruCentralNode);
		if (translucent) {
			ImGui::PopStyleColor();
		}
		RenderStatusBar(statusBarH);
		// End() is in OnPreRender after RenderMainMenu — must stay open for the menubar.
	}

	void ImGuiEditorLayer::OpenPanel(const char* windowName, bool& showPanel) {
		showPanel = true;
		m_PendingPanelFocus = windowName;
	}

	void ImGuiEditorLayer::ApplyPanelFocusRequest() {
		if (m_PendingPanelFocus.empty()) return;
		ImGuiWindow* window = ImGui::FindWindowByName(m_PendingPanelFocus.c_str());
		m_PendingPanelFocus.clear();
		if (!window || !window->Active || ImGui::FindBlockingModal(window)) return;

		ImGui::SetWindowCollapsed(window, false, ImGuiCond_Always);
		ImGui::FocusWindow(window, ImGuiFocusRequestFlags_RestoreFocusedChild);

		// ImGui focus does not raise an existing platform window.
		ImGuiViewport* viewport = window->Viewport;
		ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
		if (RunAutomation::IsBackground() || !viewport || !viewport->PlatformUserData
			|| !viewport->PlatformHandle || !platformIO.Platform_SetWindowFocus) return;
		if (SDL_Window* nativeWindow = SDL_GetWindowFromID(static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(viewport->PlatformHandle)));
			nativeWindow && (SDL_GetWindowFlags(nativeWindow) & SDL_WINDOW_MINIMIZED)) {
			SDL_RestoreWindow(nativeWindow);
		}
		platformIO.Platform_SetWindowFocus(viewport);
	}

	void ImGuiEditorLayer::RenderMainMenu(Scene& scene) {
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_K)) {
			m_OpenCommandPalette = true;
		}
		RenderCommandPalette(scene);

		if (!ImGui::BeginMenuBar()) {
			return;
		}


		if (ImGui::BeginMenu("Application")) {
			if (ImGui::MenuItem("Save Scene", EditorShortcuts::k_SaveLabel, false, !Application::GetIsPlaying())) {
				SaveScenesFromMenu(scene);
			}

			if (ImGui::MenuItem("Quit")) {
				Application::RequestQuit();
			}
			const bool reloadPending = EditorRestart::HasPending();
			if (ImGui::MenuItem(reloadPending ? "Reload App (pending)" : "Reload App")) {
				RequestEditorAction(reloadPending && EditorRestart::GetPendingKind() == EditorRestart::Kind::Restart
					? PendingEditorAction::Restart : PendingEditorAction::Reload);
			}
			if (reloadPending && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
				std::string reasons;
				for (const EditorRestart::Reason& reason : EditorRestart::GetPending()) {
					reasons += (reasons.empty() ? "" : "\n") + reason.Text;
				}
				ImGui::SetTooltip("%s", reasons.c_str());
			}
			ImGui::EndMenu();
		}




		if (ImGui::BeginMenu("Project")) {
			bool hasProject = ProjectManager::HasProject();
			if (ImGui::MenuItem("Build", nullptr, false, hasProject && !Application::GetIsPlaying())) {
				OpenPanel("Build", m_ShowBuildPanel);
				if (m_BuildOutputDir.empty()) {
					FunkeProject* project = ProjectManager::GetCurrentProject();
					if (project) {
						m_BuildOutputDir = Path::Combine(project->RootDirectory, "Builds", "Windows");
						std::snprintf(m_BuildOutputDirBuffer, sizeof(m_BuildOutputDirBuffer), "%s", m_BuildOutputDir.c_str());
					}
				}
			}
			if (ImGui::MenuItem("Build Profiles…", nullptr, false, hasProject)) {
				OpenPanel("Build Profiles", m_ShowBuildProfilesPanel);
			}
			if (ImGui::MenuItem("Project Settings", nullptr, false, hasProject)) {
				OpenPanel("Project Settings", m_ShowProjectSettings);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Package Manager", nullptr, false, hasProject)) {
				OpenPanel("Package Manager", m_ShowPackageManager);
			}

			ImGui::EndMenu();
		}

		// Edit — user-scoped editor preferences (theme, fonts, layout
		// presets, external script editor, asset-browser / auto-save
		// toggles). All routed through EditorPreferencesPanel.
		if (ImGui::BeginMenu("Edit")) {
			// Operate on the context scene (prefab-edit aware), matching the
			// keyboard handler in OnUpdate. Disabled during play.
			Scene* undoScene = GetContextScene();
			const bool canEditHistory = undoScene && !Application::GetIsPlaying();
			if (ImGui::MenuItem("Undo", EditorShortcuts::k_UndoLabel, false, canEditHistory && GetUndoStack(*undoScene).CanUndo())) {
				PerformUndoRedo(*undoScene, false);
			}
			if (ImGui::MenuItem("Redo", EditorShortcuts::k_RedoLabel, false, canEditHistory && GetUndoStack(*undoScene).CanRedo())) {
				PerformUndoRedo(*undoScene, true);
			}
			ImGui::Separator();
			if (ImGui::BeginMenu("View")) {
				RenderEditorViewSettingsMenu();
				ImGui::Separator();
				RenderGameViewSettingsMenu();
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Layout")) {
				if (ImGui::MenuItem("Default")) {
					ImGuiContextLayer::ResetLayoutToDefault();
				}
				const std::vector<std::string> presets = ImGuiContextLayer::ListLayoutPresets();
				if (!presets.empty()) ImGui::Separator();
				for (const std::string& preset : presets) {
					if (ImGui::MenuItem(preset.c_str())) {
						ImGuiContextLayer::LoadLayoutPreset(preset);
					}
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Manage layouts...")) {
					OpenPanel("Editor Preferences", m_ShowEditorPreferences);
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Preferences...")) {
				OpenPanel("Editor Preferences", m_ShowEditorPreferences);
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Tools")) {
			if (ImGui::MenuItem("Search Commands...", EditorShortcuts::k_CommandPaletteLabel)) {
				m_OpenCommandPalette = true;
			}
			ImGui::Separator();
			ImGui::MenuItem("Profiler", EditorShortcuts::k_ProfilerLabel, &m_ShowProfiler);
			ImGui::MenuItem("Animation", nullptr, &m_ShowAnimationWindow);
			ImGui::MenuItem("Animator", nullptr, &m_ShowAnimatorWindow);
			// Windows written in C# by the game or its packages (Funke.Editor.EditorWindow).
			m_ScriptWindows.DrawMenuItems();
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	void ImGuiEditorLayer::SaveScenesFromMenu(Scene& scene) {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (project) {
			bool hadDirtyScene = false;
			bool allSaved = true;
			for (auto& weakScene : SceneManager::GetLoadedScenes()) {
				auto loadedScene = weakScene.lock();
				if (!loadedScene || !loadedScene->IsDirty()) continue;
				hadDirtyScene = true;
				std::string scenePath = project->GetSceneFilePath(loadedScene->GetName());
				if (scenePath.empty() || !SceneSerializer::SaveToFile(*loadedScene, scenePath)) {
					allSaved = false;
					FNK_ERROR_TAG("Editor", "Failed to save scene '{}'.", loadedScene->GetName());
				}
			}
			if (!hadDirtyScene) {
				std::string scenePath = project->GetSceneFilePath(scene.GetName());
				allSaved = !scenePath.empty() && SceneSerializer::SaveToFile(scene, scenePath);
				if (!allSaved) {
					FNK_ERROR_TAG("Editor", "Failed to save scene '{}'.", scene.GetName());
				}
			}
			if (allSaved) {
				const std::string previousLastOpenedScene = project->LastOpenedScene;
				project->LastOpenedScene = scene.GetName();
				if (!project->Save()) {
					project->LastOpenedScene = previousLastOpenedScene;
					FNK_ERROR_TAG("Editor", "Scenes were saved, but project metadata could not be updated.");
				}
			}
		}
	}

	void ImGuiEditorLayer::RenderToolbarLeft() {
		ImGui::AlignTextToFramePadding();
		const FunkeProject* project = ProjectManager::GetCurrentProject();
		Scene* scene = GetContextScene();
		if (project) {
			ImGui::TextDisabled("%s", project->Name.c_str());
			if (scene) {
				ImGui::SameLine(0.0f, 6.0f);
				ImGui::TextDisabled("/");
				ImGui::SameLine(0.0f, 6.0f);
			}
		}
		if (scene) {
			const bool dirty = scene->IsDirty();
			ImGui::TextUnformatted(scene->GetName().c_str());
			if (dirty) {
				ImGui::SameLine(0.0f, 4.0f);
				ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors::Highlight());
				ImGui::TextUnformatted("*");
				ImGui::PopStyleColor();
				EditorUI::Tooltip("Unsaved changes");
			}
		}
	}

	void ImGuiEditorLayer::RenderCommandPalette(Scene& scene) {
		constexpr const char* k_PaletteId = "##CommandPalette";
		const bool justOpened = m_OpenCommandPalette;
		if (m_OpenCommandPalette) {
			ImGui::OpenPopup(k_PaletteId);
			m_OpenCommandPalette = false;
			m_CommandPaletteBuffer[0] = '\0';
			m_CommandPaletteSelection = 0;
		}

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float width = std::round(ImGui::GetFontSize() * 34.0f);
		ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + ImGui::GetFontSize() * 5.0f),
			ImGuiCond_Always, ImVec2(0.5f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
		if (!ImGui::BeginPopup(k_PaletteId)) return;

		struct Command {
			const char* Label;
			const char* Shortcut;
			bool Enabled;
			std::function<void()> Run;
		};
		const bool playing = Application::GetIsPlaying();
		const bool hasProject = ProjectManager::HasProject();
		Scene* undoScene = GetContextScene();
		const bool canEditHistory = undoScene && !playing;
		const bool canSwitchViewMode = EngineModules::IsActive(EngineModule::TwoD) && EngineModules::IsActive(EngineModule::ThreeD);
		std::vector<Command> commands = {
			{ "Save Scene", EditorShortcuts::k_SaveLabel, !playing, [&]() { SaveScenesFromMenu(scene); } },
			{ "Enter Play Mode", nullptr, !playing && !m_PlayModeRecompilePending, [&]() {
				if (Scene* active = SceneManager::GetActiveScene()) BeginPlayModeRequest(*active);
			} },
			{ "Undo", EditorShortcuts::k_UndoLabel, canEditHistory && GetUndoStack(*undoScene).CanUndo(), [&]() { PerformUndoRedo(*undoScene, false); } },
			{ "Redo", EditorShortcuts::k_RedoLabel, canEditHistory && GetUndoStack(*undoScene).CanRedo(), [&]() { PerformUndoRedo(*undoScene, true); } },
			{ "Build...", nullptr, hasProject && !playing, [&]() { OpenPanel("Build", m_ShowBuildPanel); } },
			{ "Build Profiles", nullptr, hasProject, [&]() { OpenPanel("Build Profiles", m_ShowBuildProfilesPanel); } },
			{ "Project Settings", nullptr, hasProject, [&]() { OpenPanel("Project Settings", m_ShowProjectSettings); } },
			{ "Package Manager", nullptr, hasProject, [&]() { OpenPanel("Package Manager", m_ShowPackageManager); } },
			{ "Preferences", nullptr, true, [&]() { OpenPanel("Editor Preferences", m_ShowEditorPreferences); } },
			{ "Toggle Profiler", EditorShortcuts::k_ProfilerLabel, true, [&]() { m_ShowProfiler = !m_ShowProfiler; } },
			{ "Animation Window", nullptr, true, [&]() { m_ShowAnimationWindow = true; } },
			{ "Animator Window", nullptr, true, [&]() { m_ShowAnimatorWindow = true; } },
			{ "Reset Layout", nullptr, true, [&]() { ImGuiContextLayer::ResetLayoutToDefault(); } },
			// Editor View toolbar actions stay here so hiding them in Edit > View loses nothing.
			{ "Gizmo Mode: None", nullptr, true, [&]() { m_GizmoMode = EditorGizmoMode::None; } },
			{ "Gizmo Mode: Move", nullptr, true, [&]() { m_GizmoMode = EditorGizmoMode::Translate; } },
			{ "Gizmo Mode: Rotate", nullptr, true, [&]() { m_GizmoMode = EditorGizmoMode::Rotate; } },
			{ "Gizmo Mode: Scale", nullptr, true, [&]() { m_GizmoMode = EditorGizmoMode::Scale; } },
			{ "Gizmo Mode: Rect", nullptr, true, [&]() { m_GizmoMode = EditorGizmoMode::Bounds; } },
			{ "Toggle 3D Editor View", nullptr, canSwitchViewMode, [&]() { SetEditorView3D(!m_EditorView3D); } },
			{ "Draw Mode: Default", nullptr, true, [&]() { m_EditorViewDrawMode = EditorViewDrawMode::Default; } },
			{ "Draw Mode: Triangle", nullptr, true, [&]() { m_EditorViewDrawMode = EditorViewDrawMode::Triangle; } },
			{ "Draw Mode: Mixed", nullptr, true, [&]() { m_EditorViewDrawMode = EditorViewDrawMode::Mixed; } },
			{ "Toggle Gizmos", nullptr, true, [&]() { m_ShowGizmos = !m_ShowGizmos; } },
			{ "Toggle Post Processing in Editor View", nullptr, true, [&]() { m_ShowPostProcessing = !m_ShowPostProcessing; } },
			{ "Toggle Grid Snap", nullptr, true, [&]() { EditorPreferences::SetGridSnapEnabled(!EditorPreferences::GetGridSnapEnabled()); } },
			{ "Reset Editor View Toolbar", nullptr, !EditorPreferences::IsEditorViewToolbarDefault(),
				[&]() { EditorPreferences::ResetEditorViewToolbar(); } },
			// Game View toolbar actions, reachable with the toolbar hidden.
			{ "Toggle Game View Stats", nullptr, true, [&]() { m_ShowGameViewStats = !m_ShowGameViewStats; } },
			{ "Toggle Game View Logs", nullptr, true, [&]() { m_ShowGameViewLogs = !m_ShowGameViewLogs; } },
			{ "Toggle Game View VSync", nullptr, true, [&]() {
				m_GameViewVsync = !m_GameViewVsync;
				m_GameViewHasRendered = false;
				if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
					project->GameViewVsync = m_GameViewVsync;
					project->Save();
				}
			} },
			{ "Toggle Game View Toolbar", nullptr, true, [&]() {
				EditorPreferences::SetGameViewToolbarItemVisible(GameViewToolbarItem::Toolbar,
					!EditorPreferences::IsGameViewToolbarItemVisible(GameViewToolbarItem::Toolbar));
			} },
			{ "Reset Game View Toolbar", nullptr, !EditorPreferences::IsGameViewToolbarDefault(),
				[&]() { EditorPreferences::ResetGameViewToolbar(); } },
			{ "Reload Editor", nullptr, true, [&]() { RequestEditorAction(PendingEditorAction::Reload); } },
			{ "Quit", nullptr, true, [&]() { Application::RequestQuit(); } },
		};

		// Labels must outlive the commands that point at them.
		m_ScriptWindows.Refresh();
		std::vector<std::string> scriptWindowLabels;
		scriptWindowLabels.reserve(m_ScriptWindows.GetTypes().size());
		for (const ScriptEditorWindows::WindowType& window : m_ScriptWindows.GetTypes()) {
			scriptWindowLabels.push_back("Open " + window.Title);
			const std::string type = window.Type;
			commands.push_back({ scriptWindowLabels.back().c_str(), nullptr, true, [this, type]() { m_ScriptWindows.Open(type); } });
		}

		if (justOpened) ImGui::SetKeyboardFocusHere();
		if (EditorUI::SearchField("##CommandFilter", m_CommandPaletteBuffer, sizeof(m_CommandPaletteBuffer), "Type a command...")) {
			m_CommandPaletteSelection = 0;
		}
		ImGui::Spacing();

		const std::string filter = EditorScriptDiscovery::ToLowerCopy(m_CommandPaletteBuffer);
		std::vector<const Command*> matches;
		for (const Command& command : commands) {
			if (filter.empty() || EditorScriptDiscovery::ToLowerCopy(command.Label).find(filter) != std::string::npos) {
				matches.push_back(&command);
			}
		}

		if (matches.empty()) {
			EditorUI::EmptyState("No matching commands");
		}
		else {
			const int count = static_cast<int>(matches.size());
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) m_CommandPaletteSelection = (m_CommandPaletteSelection + 1) % count;
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) m_CommandPaletteSelection = (m_CommandPaletteSelection + count - 1) % count;
			m_CommandPaletteSelection = std::clamp(m_CommandPaletteSelection, 0, count - 1);

			const Command* run = nullptr;
			for (int i = 0; i < count; ++i) {
				const Command& command = *matches[static_cast<std::size_t>(i)];
				ImGui::PushID(i);
				ImGui::BeginDisabled(!command.Enabled);
				const bool highlighted = i == m_CommandPaletteSelection;
				if (highlighted) ImGui::PushStyleColor(ImGuiCol_Header, EditorTheme::Colors::RowSelection());
				if (ImGui::Selectable(command.Label, highlighted, ImGuiSelectableFlags_None)) run = &command;
				if (highlighted) ImGui::PopStyleColor();
				if (command.Shortcut) {
					ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(command.Shortcut).x);
					ImGui::TextDisabled("%s", command.Shortcut);
				}
				ImGui::EndDisabled();
				ImGui::PopID();
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
				run = matches[static_cast<std::size_t>(m_CommandPaletteSelection)];
			}
			if (run && run->Enabled) {
				ImGui::CloseCurrentPopup();
				run->Run();
			}
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	void ImGuiEditorLayer::RenderStatusBar(float height) {
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 3.0f));
		ImGui::BeginChild("##StatusBar", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();

		const ImVec2 wp = ImGui::GetWindowPos();
		ImGui::GetWindowDrawList()->AddLine(wp, ImVec2(wp.x + ImGui::GetWindowWidth(), wp.y), ImGui::GetColorU32(ImGuiCol_Border));

		char frameRate[32];
		std::snprintf(frameRate, sizeof(frameRate), "%.0f FPS", ImGui::GetIO().Framerate);
		const float frameRateWidth = ImGui::CalcTextSize(frameRate).x;
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - frameRateWidth));
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", frameRate);

		ImGui::EndChild();
	}

	static bool IconButton(const char* id, const char* iconName, float iconSize, const ImVec2& btnSize, bool accent = false) {
		if (accent) {
			const ImVec4 a = EditorTheme::Colors::Accent();
			ImGui::PushStyleColor(ImGuiCol_Button, a);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(std::min(a.x + 0.06f, 1.0f), std::min(a.y + 0.06f, 1.0f), std::min(a.z + 0.06f, 1.0f), 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, a);
		}
		const ImVec4 tint = accent ? ImVec4(1, 1, 1, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
		uint64_t tex = EditorIcons::Get(iconName, (int)iconSize);
		const bool clicked = tex
			? ImGui::ImageButton(id, (ImTextureID)(intptr_t)tex, btnSize, ImVec2(0, 1), ImVec2(1, 0), ImVec4(0, 0, 0, 0), tint)
			: ImGui::Button(id + 2);
		if (accent) ImGui::PopStyleColor(3);
		return clicked;
	}

	bool ImGuiEditorLayer::SaveDirtyPrefabsBeforeRun(std::string_view actionName) {
		if (m_PrefabEditScene && (m_PrefabEditScene->IsDirty() || m_PrefabEditDirty)
			&& !SavePrefabEditChanges()) {
			FNK_ERROR_TAG("Editor", "{} aborted because the prefab edit could not be saved.", actionName);
			return false;
		}
		if (m_PrefabInspector.IsOpen() && m_PrefabInspector.HasUnsavedChanges()
			&& !SavePrefabInspectorChanges()) {
			FNK_ERROR_TAG("Editor", "{} aborted because the prefab inspector could not be saved.", actionName);
			return false;
		}
		return true;
	}

	bool ImGuiEditorLayer::ValidateUniquePersistentEntityIds(
		const std::vector<Scene*>& scenes,
		std::string_view actionName) const {
		std::unordered_map<uint64_t, std::string> owners;
		for (const Scene* scene : scenes) {
			if (!scene) continue;
			const auto view = scene->GetRegistry().view<UUIDComponent>();
			for (const EntityHandle entity : view) {
				const uint64_t persistentId = scene->GetEntityPersistentID(entity);
				if (persistentId == 0) {
					FNK_ERROR_TAG("Editor",
						"{} aborted: scene '{}' contains an entity without a persistent ID.",
						actionName, scene->GetName());
					return false;
				}
				const auto [owner, inserted] = owners.emplace(persistentId, scene->GetName());
				if (!inserted) {
					FNK_ERROR_TAG("Editor",
						"{} aborted: persistent entity ID {} is shared by scenes '{}' and '{}'.",
						actionName, persistentId, owner->second, scene->GetName());
					return false;
				}
			}
		}
		return true;
	}

	bool ImGuiEditorLayer::PreparePlayModeSnapshot(Scene& scene) {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			FNK_ERROR_TAG("PlayMode", "Play Mode requires an active project.");
			return false;
		}
		std::vector<Scene*> loadedScenes;
		for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
			const std::shared_ptr<Scene> loadedScene = weakScene.lock();
			if (!loadedScene) continue;
			if (loadedScene->IsPersistent()) {
				FNK_ERROR_TAG("PlayMode", "Play Mode cannot snapshot persistent scene '{}'.", loadedScene->GetName());
				return false;
			}
			if (loadedScene->IsLifecycleSuspended()) {
				FNK_ERROR_TAG("PlayMode", "Play Mode cannot snapshot suspended recovery scene '{}'.", loadedScene->GetName());
				return false;
			}
			loadedScenes.push_back(loadedScene.get());
		}
		if (!ValidateUniquePersistentEntityIds(loadedScenes, "Play Mode")) {
			return false;
		}
		if (!SaveDirtyPrefabsBeforeRun("Play Mode")) {
			return false;
		}
		if (!ValidateUniquePersistentEntityIds(loadedScenes, "Play Mode")) {
			return false;
		}

		std::vector<std::pair<std::string, std::string>> playModeScenes;
		std::unordered_set<std::string> playModeSceneNames;
		std::unordered_set<std::string> playModeScenePaths;
		std::unordered_set<std::string> dirtySceneNames;
		bool sceneSnapshotFailed = false;
		SceneManager::ForeachLoadedScene([&](Scene& loadedScene) {
			if (sceneSnapshotFailed) return;
			if (!playModeSceneNames.insert(loadedScene.GetName()).second) {
				sceneSnapshotFailed = true;
				FNK_ERROR_TAG("PlayMode", "Loaded scenes must have unique names; '{}' is duplicated.", loadedScene.GetName());
				return;
			}
			const std::string scenePath = project->GetSceneFilePath(loadedScene.GetName());
			if (scenePath.empty()) {
				sceneSnapshotFailed = true;
				FNK_ERROR_TAG("PlayMode", "Scene '{}' has no writable project path.", loadedScene.GetName());
				return;
			}
			if (!playModeScenePaths.insert(scenePath).second) {
				sceneSnapshotFailed = true;
				FNK_ERROR_TAG("PlayMode", "Multiple loaded scenes resolve to snapshot path '{}'.", scenePath);
				return;
			}
			// Freeze even clean scenes so external file edits during Play cannot invalidate the retained history.
			const bool wasDirty = loadedScene.IsDirty();
			const std::filesystem::path snapshotDir = Path::FromUtf8(project->RootDirectory) / ".funke" / "PlayMode";
			std::error_code dirError;
			std::filesystem::create_directories(snapshotDir, dirError);
			const std::string snapshotPath = Path::ToUtf8(snapshotDir / Path::FromUtf8(loadedScene.GetName() + ".scene"));
			if (dirError || !SceneSerializer::SaveToFile(loadedScene, snapshotPath, false)) {
				sceneSnapshotFailed = true;
				FNK_ERROR_TAG("PlayMode", "Failed to snapshot scene '{}'. Play Mode was not started.", loadedScene.GetName());
				return;
			}
			if (wasDirty) {
				loadedScene.MarkDirty();
				dirtySceneNames.insert(loadedScene.GetName());
			}
			if (!File::Exists(snapshotPath)) {
				sceneSnapshotFailed = true;
				FNK_ERROR_TAG("PlayMode", "Scene snapshot is missing: {}", snapshotPath);
				return;
			}
			playModeScenes.push_back({ loadedScene.GetName(), snapshotPath });
		});
		if (sceneSnapshotFailed || playModeScenes.empty()) {
			return false;
		}
		const std::string previousLastOpenedScene = project->LastOpenedScene;
		project->LastOpenedScene = scene.GetName();
		if (!project->Save()) {
			project->LastOpenedScene = previousLastOpenedScene;
			FNK_ERROR_TAG("PlayMode", "Failed to save project metadata. Play Mode was not started.");
			return false;
		}

		m_PlayModeScenes = std::move(playModeScenes);
		m_PlayModeDirtyScenes = std::move(dirtySceneNames);
		m_PlayModeActiveScene = scene.GetName();
		return true;
	}

	void ImGuiEditorLayer::BeginPlayModeRequest(Scene& scene) {
		if (!PreparePlayModeSnapshot(scene)) {
			m_PlayModeRecompileScene.reset();
			m_PlayModeScenes.clear();
			m_PlayModeActiveScene.clear();
			m_PlayModeScriptFields.clear();
			return;
		}
		CaptureScriptFieldOverrides(/*merge=*/false);

		m_LogEntries.clear();
		m_SelectedLogIndex = -1;
		m_LogScrollToEnd = m_LogAutoScroll;
		ApplicationEditorAccess::SetPlaymodePaused(false);

		ScriptSystem* scriptSys = scene.HasSystem<ScriptSystem>() ? scene.GetSystem<ScriptSystem>() : nullptr;
		if (scriptSys && EditorPreferences::GetRecompileScriptsOnPlay()) {
			const bool rebuildStarted = scriptSys->RequestRebuildAndReloadAll();
			if (rebuildStarted || scriptSys->IsRebuilding()) {
				m_PlayModeRecompileScene = SceneManager::GetLoadedScene(scene.GetName());
				m_PlayModeRecompilePending = true;
				return;
			}
		}
		if (scriptSys && scriptSys->IsRebuilding()) {
			m_PlayModeRecompileScene = SceneManager::GetLoadedScene(scene.GetName());
			m_PlayModeRecompilePending = true;
			return;
		}
		if (scriptSys && !scriptSys->DidLastRebuildSucceed()) {
			m_PlayModeRecompileScene.reset();
			m_PlayModeScenes.clear();
			m_PlayModeActiveScene.clear();
			m_PlayModeScriptFields.clear();
			FNK_ERROR_TAG("PlayMode", "Script compilation failed. Play Mode was not started.");
			return;
		}

		CompletePlayModeEntry(scene);
	}

	void ImGuiEditorLayer::CompletePlayModeEntry(Scene& scene) {
		m_PlayModeRecompilePending = false;
		m_PlayModeRecompileScene.reset();
		m_PlayModeSelection = CaptureLoadedSceneSelection();
		if (Scene* context = GetContextScene()) FinishInspectorRename(*context);
		if (m_ActiveUndoStack) InspectorUndoRecorder::Flush(*m_ActiveUndoStack, true);
		InspectorUndoRecorder::Reset();
		m_ActiveUndoStack = nullptr;
		m_UndoStackSceneId = nullptr;
		m_PlayModeUndoHistory.clear();
		for (const auto& weakScene : SceneManager::GetLoadedScenes()) {
			if (const auto loaded = weakScene.lock()) {
				m_PlayModeUndoHistory.emplace(loaded->GetName(), m_SceneUndoHistory.Take(loaded));
			}
		}
		// Step counter is panel-frame state, not playmode state — without a reset
		// here a Step that didn't finish draining before the previous Stop would
		// trip the bottom-of-RenderToolbar auto-pause on this fresh session's first
		// frame, freezing the new Play instantly.
		m_StepFrames = 0;
		ApplicationEditorAccess::SetPlaymodePaused(false);
		SceneManager::ForeachLoadedScene([](Scene& loadedScene) { loadedScene.ClearDirty(); });
		Application::SetIsPlaying(true);
		m_FocusGameViewOnPlay = true;
		// Reset Time.TimeSinceStartup / Time.RealtimeSinceStartup so each
		// play session starts at t=0 from a script's perspective.
		ApplicationEditorAccess::MarkGameStart();
		// Re-seed the fixed timestep from the project default (see FunkeProject::PhysicsTickRate)
		// so a prior session's scripted Time.fixedDeltaTime override doesn't leak across
		// Stop/Play. Built games get this for free at Application::Initialize.
		if (const FunkeProject* project = ProjectManager::GetCurrentProject()) {
			Time::SetFixedDeltaTime(project->GetFixedDeltaTimeSeconds());
			RenderSettings::Apply(project->Graphics);
		}
		scene.StartManagedSceneSystemsForPlayMode();
		// Reset Box2D sleep timers so bodies that sat through editor
		// idle don't immediately freeze on the first physics step.
		if (PhysicsSystem2D::IsInitialized()) {
			PhysicsSystem2D::WakeAllBodies();
		}
		StartPlayOnAwakeComponents(scene);
	}

	void ImGuiEditorLayer::PollPendingPlayModeRequest() {
		if (!m_PlayModeRecompilePending || Application::GetIsPlaying()) {
			return;
		}

		const std::shared_ptr<Scene> rebuildScene = m_PlayModeRecompileScene.lock();
		ScriptSystem* scriptSys = rebuildScene && rebuildScene->HasSystem<ScriptSystem>()
			? rebuildScene->GetSystem<ScriptSystem>() : nullptr;
		if (scriptSys && scriptSys->IsRebuilding()) {
			return;
		}

		if (!scriptSys || !scriptSys->DidLastRebuildSucceed()) {
			m_PlayModeRecompilePending = false;
			m_PlayModeRecompileScene.reset();
			m_PlayModeScenes.clear();
			m_PlayModeActiveScene.clear();
			m_PlayModeScriptFields.clear();
			if (scriptSys) {
				FNK_ERROR_TAG("PlayMode", "Script compilation failed. Play Mode was not started.");
			}
			else {
				FNK_ERROR_TAG("PlayMode", "The scene that requested script compilation is no longer loaded. Play Mode was not started.");
			}
			return;
		}

		Scene* activeScene = SceneManager::GetActiveScene();
		if (!activeScene || !PreparePlayModeSnapshot(*activeScene)) {
			m_PlayModeRecompilePending = false;
			m_PlayModeRecompileScene.reset();
			m_PlayModeScenes.clear();
			m_PlayModeActiveScene.clear();
			m_PlayModeScriptFields.clear();
			return;
		}
		CaptureScriptFieldOverrides(/*merge=*/true);
		CompletePlayModeEntry(*activeScene);
	}

	void ImGuiEditorLayer::RenderToolbar() {
		const float frameH = ImGui::GetFrameHeight();
		const float iconSize = std::round(ImGui::GetFontSize() * 0.9f);
		const ImVec2 btnSize(iconSize, iconSize);
		const bool isPlaying = Application::GetIsPlaying();
		const bool isPaused = ApplicationEditorAccess::IsPlaymodePaused();

		// Frame-height buttons centred in the toolbar row: edit mode shows Play + Standalone, play mode four.
		const ImVec2 framePad(12.0f, std::max(0.0f, (frameH - iconSize) * 0.5f));
		const float buttonW = iconSize + framePad.x * 2.0f;
		const float gap = 2.0f;
		const int buttonCount = isPlaying ? 4 : 2;
		const float groupW = buttonW * buttonCount + gap * (buttonCount - 1);
		ImGui::SameLine();
		const float centeredX = (ImGui::GetWindowWidth() - groupW) * 0.5f;
		if (centeredX > ImGui::GetCursorPosX()) {
			ImGui::SetCursorPosX(centeredX);
		}

		// One rounded well behind the group; buttons inside are flat until hovered.
		const ImVec2 groupMin = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(groupMin.x - 3.0f, groupMin.y - 2.0f),
			ImVec2(groupMin.x + groupW + 3.0f, groupMin.y + frameH + 2.0f),
			ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding + 2.0f);

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, framePad);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetStyle().FrameRounding);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, ImGui::GetStyle().ItemSpacing.y));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

		if (!isPlaying) {
			const bool playButtonDisabled = m_PlayModeRecompilePending;
			if (playButtonDisabled) {
				ImGui::BeginDisabled();
			}
			if (IconButton("##Play", "play", iconSize, btnSize, /*accent=*/true)) {
				Scene* active = SceneManager::GetActiveScene();
				if (active) {
					BeginPlayModeRequest(*active);
				}
			}
			if (playButtonDisabled) {
				ImGui::EndDisabled();
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
				ImGui::SetTooltip("Play (Enter playmode)");
			}
		}
		else {
			if (isPaused) {
				if (IconButton("##Continue", "play", iconSize, btnSize)) {
					SetPlayModePaused(false);
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Continue");
			}
			else {
				if (IconButton("##Pause", "pause", iconSize, btnSize)) {
					SetPlayModePaused(true);
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause");
			}

			ImGui::SameLine();
			if (IconButton("##Step", "step_forward", iconSize, btnSize)) {
				ApplicationEditorAccess::SetPlaymodePaused(false);
				m_StepFrames = 2;
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step (advance one frame)");

			ImGui::SameLine();
			if (IconButton("##Stop", "stop", iconSize, btnSize, /*accent=*/true)) {
				RequestPlayModeStop();
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop (exit playmode)");
		}

		// Disabled mid-build to avoid racing the file-copy step that touches the runtime binary.
		ImGui::SameLine();
		const bool standaloneDisabled = (m_BuildState > 0) || (m_ReserializeState > 0)
			|| !ProjectManager::GetCurrentProject();
		if (standaloneDisabled) ImGui::BeginDisabled();
		if (IconButton("##LaunchStandalone", "launch_standalone", iconSize, btnSize)) {
			LaunchStandalone();
		}
		if (standaloneDisabled) ImGui::EndDisabled();

		ImGui::PopStyleColor();
		ImGui::PopStyleVar(3);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			if (m_BuildState > 0)
				ImGui::SetTooltip("Launch Standalone (disabled — build in progress)");
			else if (m_ReserializeState > 0)
				ImGui::SetTooltip("Launch Standalone (disabled — asset reserialization in progress)");
			else if (!ProjectManager::GetCurrentProject())
				ImGui::SetTooltip("Launch Standalone (disabled — no project loaded)");
			else
				ImGui::SetTooltip(
					"Launches the game in\n"
					"a separate window.");
		}
		// Status text sits beside the buttons, nudged down to their vertical center.
		if (isPlaying) {
			ImGui::SameLine(0.0f, 10.0f);
			ImGui::AlignTextToFramePadding();
			ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors::Highlight());
			ImGui::TextUnformatted(isPaused ? "Paused" : "Playing");
			ImGui::PopStyleColor();
		}
		if (m_PlayModeRecompilePending) {
			ImGui::SameLine(0.0f, 10.0f);
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("Waiting for script compilation...");
		}
		RenderSimulatedPlatformBadge();

		// Live playing flag (not the top-of-function snapshot): keeps us from
		// pausing if a handler earlier in this frame already left playmode.
		if (Application::GetIsPlaying() && m_StepFrames > 0) {
			m_StepFrames--;
			if (m_StepFrames == 0) {
				ApplicationEditorAccess::SetPlaymodePaused(true);
				// Audio runs on its own thread; mirror the Pause button's manual capture so Continue can resume it.
				Scene* active = SceneManager::GetActiveScene();
				if (active) {
					for (auto [ent, audio] : active->GetRegistry().view<AudioSourceComponent>().each()) {
						if (audio.IsPlaying()) {
							audio.Pause();
							m_EditorPausedAudioEntities.push_back(ent);
						}
					}
				}
			}
		}
	}

	void ImGuiEditorLayer::RenderSimulatedPlatformBadge() {
		const FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) return;
		const FunkeProject::EditorScriptTarget target = project->GetEditorScriptTarget();
		if (!target.Simulated) return;

		const std::string platformName = FunkeBuildProfile::PlatformDisplayName(target.Platform);
		ImGui::SameLine(0.0f, 10.0f);
		EditorUI::Badge(("Simulating " + platformName).c_str(), EditorUI::Tone::Info);
		const std::optional<BuildPlatform> host = BuildPlatformSupport::HostDesktopPlatform();
		const std::string tooltip = "Scripts compile and play as " + platformName + " ("
			+ FunkeProject::GetManagedPlatformDefine(target.Platform) + ", Application.Platform) because the active build profile '"
			+ target.ProfileName + "' targets it.\nThe engine itself still runs as "
			+ (host ? FunkeBuildProfile::PlatformDisplayName(*host) : "this computer's platform")
			+ ".\nClick to open Build Profiles.";
		EditorUI::Tooltip(tooltip.c_str());
		if (ImGui::IsItemClicked()) OpenPanel("Build Profiles", m_ShowBuildProfilesPanel);
	}

	void ImGuiEditorLayer::LaunchStandalone() {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			FNK_CORE_WARN_TAG("Editor",
				"LaunchStandalone: no project loaded — nothing to launch.");
			return;
		}
		if (m_ReserializeState != 0) {
			FNK_CORE_WARN_TAG("Editor", "LaunchStandalone: asset reserialization is still in progress.");
			return;
		}
		std::vector<Scene*> loadedScenes;
		for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
			const std::shared_ptr<Scene> loadedScene = weakScene.lock();
			if (!loadedScene) continue;
			if (loadedScene->IsLifecycleSuspended()) {
				FNK_ERROR_TAG("Editor", "Launch Standalone aborted: scene '{}' is suspended for recovery.", loadedScene->GetName());
				return;
			}
			loadedScenes.push_back(loadedScene.get());
		}
		if (!ValidateUniquePersistentEntityIds(loadedScenes, "Launch Standalone")) {
			return;
		}

		// Tries two layouts: dev-tree sibling dir, then same dir as editor exe.
		const std::filesystem::path exeDir = Path::FromUtf8(Path::ExecutableDir());
#ifdef FNK_PLATFORM_WINDOWS
		constexpr const char* k_RuntimeExe = "Funke-Runtime.exe";
#else
		constexpr const char* k_RuntimeExe = "Funke-Runtime";
#endif
		std::filesystem::path runtimePath = exeDir.parent_path() / "Funke-Runtime" / k_RuntimeExe;
		if (!std::filesystem::exists(runtimePath)) {
			runtimePath = exeDir / k_RuntimeExe;
		}
		if (!std::filesystem::exists(runtimePath)) {
			FNK_CORE_ERROR_TAG("Editor",
				"LaunchStandalone: Funke-Runtime executable not found near editor "
				"(tried '{}' and '{}'). Build the engine's Runtime project first.",
				Path::ToUtf8(exeDir.parent_path() / "Funke-Runtime" / k_RuntimeExe),
				Path::ToUtf8(exeDir / k_RuntimeExe));
			return;
		}
		if (!SaveDirtyPrefabsBeforeRun("Launch Standalone")) {
			return;
		}
		if (!ValidateUniquePersistentEntityIds(loadedScenes, "Launch Standalone")) {
			return;
		}

		const std::string previousLastOpenedScene = project->LastOpenedScene;
		if (!Application::GetIsPlaying()) {
			bool saveFailed = false;
			bool sawLoadedScene = false;
			SceneManager::ForeachLoadedScene([&](Scene& loadedScene) {
				if (saveFailed) return;
				sawLoadedScene = true;
				const std::string scenePath = project->GetSceneFilePath(loadedScene.GetName());
				if (scenePath.empty()) {
					saveFailed = true;
					FNK_ERROR_TAG("Editor", "LaunchStandalone: scene '{}' has no writable project path.", loadedScene.GetName());
					return;
				}
				if (loadedScene.IsDirty() && !SceneSerializer::SaveToFile(loadedScene, scenePath)) {
					saveFailed = true;
					FNK_ERROR_TAG("Editor", "LaunchStandalone: failed to save scene '{}'.", loadedScene.GetName());
					return;
				}
				if (!File::Exists(scenePath)) {
					saveFailed = true;
					FNK_ERROR_TAG("Editor", "LaunchStandalone: scene snapshot is missing: {}", scenePath);
				}
			});
			if (saveFailed || !sawLoadedScene) {
				if (!sawLoadedScene) {
					FNK_ERROR_TAG("Editor", "LaunchStandalone: no loaded scene is available.");
				}
				return;
			}
		}

		if (!Application::GetIsPlaying()) {
			if (Scene* active = SceneManager::GetActiveScene()) {
				project->LastOpenedScene = active->GetName();
			}
		}
		if (!project->Save()) {
			project->LastOpenedScene = previousLastOpenedScene;
			FNK_ERROR_TAG("Editor", "LaunchStandalone: failed to save project metadata.");
			return;
		}

		const std::string runtimeStr = Path::ToUtf8(runtimePath);
		const std::string projectArg = "--project=" + project->RootDirectory;
		FNK_CORE_INFO_TAG("Editor",
			"LaunchStandalone: spawning '{}' for project '{}'",
			runtimeStr, project->Name);

		unsigned long runtimePid = 0;
		std::string loaderError;
		if (!Process::LaunchDetachedChecked({ runtimeStr, projectArg },
			runtimePath.parent_path(), runtimePid, loaderError))
		{
			if (!loaderError.empty()) {
				FNK_CORE_ERROR_TAG("Editor", "LaunchStandalone: '{}' did not start. {}", runtimeStr, loaderError);
			}
			else {
				FNK_CORE_ERROR_TAG("Editor",
					"LaunchStandalone: failed to spawn '{}' (Process::LaunchDetached "
					"returned false). See earlier log for the OS-level reason.",
					runtimeStr);
			}
		}
	}

	void ImGuiEditorLayer::CaptureScriptFieldOverrides(bool merge) {
		if (!merge) {
			m_PlayModeScriptFields.clear();
		}
		int captured = 0;
		SceneManager::ForeachLoadedScene([&](Scene& s) {
			auto view = s.GetRegistry().view<ScriptComponent>();
			for (EntityHandle entity : view) {
				const ScriptComponent& sc = view.get<ScriptComponent>(entity);
				if (sc.PendingFieldValues.empty()) {
					continue;
				}
				const uint64_t uuid = s.GetEntityPersistentID(entity);
				if (uuid == 0) {
					continue;
				}
				auto& fields = m_PlayModeScriptFields[s.GetName()][uuid];
				for (const auto& [key, value] : sc.PendingFieldValues) {
					fields[key] = value;
					++captured;
				}
			}
		});
		FNK_CORE_INFO_TAG("PlayFix", "CaptureScriptFieldOverrides(merge={}) captured {} field(s)", merge, captured);
	}

	void ImGuiEditorLayer::ReapplyScriptFieldOverrides(const std::vector<std::shared_ptr<Scene>>& scenes) {
		FNK_CORE_INFO_TAG("PlayFix", "ReapplyScriptFieldOverrides: {} scene(s) in snapshot", m_PlayModeScriptFields.size());
		for (const std::shared_ptr<Scene>& loaded : scenes) {
			if (!loaded) {
				continue;
			}
			const std::string& sceneName = loaded->GetName();
			const auto sceneFields = m_PlayModeScriptFields.find(sceneName);
			if (sceneFields == m_PlayModeScriptFields.end()) continue;
			const auto& entities = sceneFields->second;
			for (const auto& [uuid, fields] : entities) {
				EntityHandle handle = entt::null;
				if (!loaded->TryResolveEntityRef(uuid, handle)) {
					FNK_CORE_WARN_TAG("PlayFix", "reapply: scene '{}' uuid={} did NOT resolve to an entity — skipping", sceneName, uuid);
					continue;
				}
				if (!loaded->HasComponent<ScriptComponent>(handle)) {
					FNK_CORE_WARN_TAG("PlayFix", "reapply: scene '{}' uuid={} has no ScriptComponent — skipping", sceneName, uuid);
					continue;
				}
				ScriptComponent& sc = loaded->GetComponent<ScriptComponent>(handle);
				for (const auto& [key, value] : fields) {
					sc.PendingFieldValues[key] = value;
				}
			}
		}
	}

	void ImGuiEditorLayer::RequestPlayModeStop(std::string sceneToOpen) {
		if (!m_PlayModeStopRequested) m_PlayModeStopRequestTime = std::chrono::steady_clock::now();
		m_PlayModeStopRequested = true;
		if (!sceneToOpen.empty()) {
			m_PlayModeStopOpenScene = std::move(sceneToOpen);
		}
	}

	void ImGuiEditorLayer::ProcessPendingPlayModeStop() {
		if (!m_PlayModeStopRequested) {
			return;
		}
		m_PlayModeStopRequested = false;
		std::string sceneToOpen = std::exchange(m_PlayModeStopOpenScene, {});
		if (!Application::GetIsPlaying()) {
			return;
		}

		if (Scene* act = SceneManager::GetActiveScene()) {
			for (auto [ent, audio] : act->GetRegistry().view<AudioSourceComponent>().each()) {
				if (audio.IsPlaying() || audio.IsPaused()) audio.Stop();
			}
		}
		// The game can't release a mouse it locked any more; give it back before the editor scene returns.
		if (Window* window = Application::GetWindow()) window->SetCursorMode(0);
		const bool restored = RestoreEditorSceneAfterPlaymode();
		if (restored) m_PlayModeStopFramesToMeasure = 2;
		if (restored && !sceneToOpen.empty()) {
			RequestEditorAction(PendingEditorAction::OpenScene, std::move(sceneToOpen));
		}
	}

	bool ImGuiEditorLayer::RestoreEditorSceneAfterPlaymode() {
		if (m_PlayModeScenes.empty()) {
			FNK_ERROR_TAG("PlayMode", "No edit-mode scene snapshot is available.");
			return false;
		}

		FUNKE_PROFILE_SCOPE("PlayMode Stop");
		using StopClock = std::chrono::steady_clock;
		const StopClock::time_point stopStart = StopClock::now();
		StopClock::time_point stageStart = stopStart;
		auto stageMs = [&stageStart]() {
			const StopClock::time_point now = StopClock::now();
			const double ms = std::chrono::duration<double, std::milli>(now - stageStart).count();
			stageStart = now;
			return ms;
		};
		double reloadMs = 0.0, releaseMs = 0.0, scriptsMs = 0.0;

		const bool inPrefabEdit = IsInPrefabEditMode();
		const LoadedSceneSelectionSnapshot interruptedPlaySelection = CaptureLoadedSceneSelection();
		std::vector<std::shared_ptr<Scene>> playScenes;
		for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
			if (std::shared_ptr<Scene> scene = weakScene.lock()) {
				playScenes.push_back(std::move(scene));
			}
		}

		// No capture here: values edited in the inspector during play are discarded like every other play-time change.
		struct StagedScene {
			std::string Name;
			std::string DefinitionName;
			std::shared_ptr<Scene> SceneInstance;
		};
		std::vector<StagedScene> stagedScenes;
		stagedScenes.reserve(m_PlayModeScenes.size());
		struct StagedSceneCleanupGuard {
			std::vector<StagedScene>& Scenes;
			bool Armed = true;
			~StagedSceneCleanupGuard() {
				if (!Armed) return;
				for (const StagedScene& staged : Scenes) {
					try { SceneManager::UnregisterScene(staged.DefinitionName); }
					catch (...) {}
				}
			}
		};
		StagedSceneCleanupGuard stagedSceneCleanup{ stagedScenes };
		struct PlayModeRestoreGuard {
			bool PreviousState;
			bool Armed = true;
			~PlayModeRestoreGuard() {
				if (Armed) Application::SetIsPlaying(PreviousState);
			}
		};
		PlayModeRestoreGuard playModeRestore{ Application::GetIsPlaying() };
		Application::SetIsPlaying(false);
		auto discardStagedScenes = [&]() {
			for (const StagedScene& staged : stagedScenes) {
				SceneManager::UnregisterScene(staged.DefinitionName);
			}
			stagedScenes.clear();
		};
		for (const auto& [sceneName, scenePath] : m_PlayModeScenes) {
			std::shared_ptr<Scene> stagedScene;
			std::string definitionName;
			if (!LoadEditorScene(scenePath, true, stagedScene, definitionName, true, true)) {
				discardStagedScenes();
				FNK_ERROR_TAG("PlayMode", "Failed to restore scene '{}'. Play Mode remains active.", sceneName);
				return false;
			}
			if (stagedScene->GetName() != sceneName) {
				const std::string serializedName = stagedScene->GetName();
				SceneManager::UnregisterScene(definitionName);
				discardStagedScenes();
				FNK_ERROR_TAG("PlayMode",
					"Scene snapshot '{}' identifies itself as '{}'. Play Mode remains active.",
					sceneName, serializedName);
				return false;
			}
			try {
				stagedScenes.push_back({ sceneName, definitionName, stagedScene });
			}
			catch (...) {
				SceneManager::UnregisterScene(definitionName);
				throw;
			}
		}

		reloadMs = stageMs();
		std::vector<Scene*> stagedScenePointers;
		std::vector<std::shared_ptr<Scene>> restoredScenes;
		std::unordered_set<std::string> restoredNames;
		std::unordered_set<std::string> restoredDefinitions;
		std::unordered_map<std::string, std::string> restoredDefinitionsByName;
		stagedScenePointers.reserve(stagedScenes.size());
		restoredScenes.reserve(stagedScenes.size());
		restoredNames.reserve(stagedScenes.size());
		restoredDefinitions.reserve(stagedScenes.size());
		restoredDefinitionsByName.reserve(stagedScenes.size());
		std::shared_ptr<Scene> restoredActiveScene;
		for (const StagedScene& staged : stagedScenes) {
			if (!restoredNames.insert(staged.SceneInstance->GetName()).second) {
				discardStagedScenes();
				FNK_ERROR_TAG("PlayMode", "Restored scene name '{}' is duplicated. Play Mode remains active.", staged.Name);
				return false;
			}
			stagedScenePointers.push_back(staged.SceneInstance.get());
			restoredScenes.push_back(staged.SceneInstance);
			restoredDefinitions.insert(staged.DefinitionName);
			restoredDefinitionsByName.emplace(staged.SceneInstance->GetName(), staged.DefinitionName);
			if (staged.Name == m_PlayModeActiveScene) {
				restoredActiveScene = staged.SceneInstance;
			}
		}
		if (!ValidateUniquePersistentEntityIds(stagedScenePointers, "Play Mode restore")) {
			discardStagedScenes();
			return false;
		}
		if (!restoredActiveScene && !restoredScenes.empty()) {
			restoredActiveScene = restoredScenes.front();
		}
		if (!restoredActiveScene) {
			discardStagedScenes();
			FNK_ERROR_TAG("PlayMode", "Restored scene set has no active-scene candidate. Play Mode remains active.");
			return false;
		}

		const std::unordered_set<std::string> previousDefinitions = m_EditorOwnedSceneDefinitions;
		std::vector<std::shared_ptr<Scene>> activatedScenes;
		activatedScenes.reserve(restoredScenes.size());
		Application::SetIsPlaying(playModeRestore.PreviousState);
		for (const std::shared_ptr<Scene>& playScene : playScenes) {
			if (SceneManager::SuspendLoadedSceneForRelease(playScene)) continue;

			discardStagedScenes();
			Application::SetIsPlaying(playModeRestore.PreviousState);
			bool playScenesRecovered = true;
			std::vector<std::shared_ptr<Scene>> recoveredPlayScenes;
			recoveredPlayScenes.reserve(playScenes.size());
			for (const std::shared_ptr<Scene>& recoveryScene : playScenes) {
				if (!recoveryScene->IsLifecycleSuspended()) continue;
				if (!SceneManager::RollbackLoadedSceneRecovery(recoveryScene)) {
					playScenesRecovered = false;
					continue;
				}
				recoveredPlayScenes.push_back(recoveryScene);
			}
			std::vector<std::shared_ptr<Scene>> reactivatedPlayScenes;
			if (playScenesRecovered) {
				reactivatedPlayScenes.reserve(recoveredPlayScenes.size());
				for (const std::shared_ptr<Scene>& recoveryScene : recoveredPlayScenes) {
					if (!SceneManager::ActivateSuspendedScene(recoveryScene)) {
						playScenesRecovered = false;
						break;
					}
					reactivatedPlayScenes.push_back(recoveryScene);
				}
			}
			if (playScenesRecovered) {
				for (const std::shared_ptr<Scene>& recoveryScene : reactivatedPlayScenes) {
					if (!SceneManager::CommitSuspendedSceneActivation(recoveryScene)) {
						playScenesRecovered = false;
					}
				}
			}
			if (!playScenesRecovered) {
				for (auto it = reactivatedPlayScenes.rbegin(); it != reactivatedPlayScenes.rend(); ++it) {
					if (!(*it)->IsLifecycleSuspended()) {
						SceneManager::ResuspendSceneForRecovery(*it);
					}
				}
				Application::SetIsPlaying(false);
				std::shared_ptr<Scene> recoveryActiveScene;
				for (const std::shared_ptr<Scene>& recoveryScene : playScenes) {
					if (recoveryScene->IsLifecycleSuspended()) {
						recoveryActiveScene = recoveryScene;
						break;
					}
				}
				if (recoveryActiveScene) {
					SceneManager::SetActiveSuspendedSceneForRecovery(recoveryActiveScene);
				}
			}
			RestoreLoadedSceneSelection(interruptedPlaySelection);
			if (!playScenesRecovered) {
				playModeRestore.Armed = false;
				m_PlayModeRestoreError = "Play Mode could not stop because scene lifecycle teardown failed, and the live Play scenes could not be fully resumed. Inspect the loaded scenes before saving.";
				m_ShowPlayModeRestoreError = true;
				FNK_ERROR_TAG("PlayMode", "{}", m_PlayModeRestoreError);
			}
			else {
				FNK_ERROR_TAG("PlayMode", "Play Mode could not stop because scene '{}' could not suspend safely.", playScene->GetName());
			}
			return false;
		}
		Application::SetIsPlaying(false);
		stagedSceneCleanup.Armed = false;
		playModeRestore.Armed = false;
		bool restoreSucceeded = true;
		std::string restoreFailure;
		const double suspendMs = stageMs();
		for (const std::shared_ptr<Scene>& playScene : playScenes) {
			if (!SceneManager::ReleaseLoadedSceneInstance(playScene, false, false) && playScene->IsLoaded()) {
				restoreSucceeded = false;
				restoreFailure = "A Play Mode scene could not be released safely.";
				FNK_ERROR_TAG("PlayMode", "Failed to release Play Mode scene instance '{}'.", playScene->GetName());
			}
		}
		for (const std::string& definitionName : previousDefinitions) {
			SceneManager::UnregisterScene(definitionName);
		}
		// Scripts own their streams; one that wasn't disposed would keep playing in edit mode.
		AudioStreams::CloseAll();
		releaseMs = stageMs();

		try {
			ScriptEngine::OnPlayModeExited();
			scriptsMs = stageMs();
		}
		catch (const std::exception& exception) {
			restoreSucceeded = false;
			restoreFailure = "Play Mode script cleanup failed: " + std::string(exception.what());
			FNK_ERROR_TAG("PlayMode", "{}", restoreFailure);
		}
		catch (...) {
			restoreSucceeded = false;
			restoreFailure = "Play Mode script cleanup failed.";
			FNK_ERROR_TAG("PlayMode", "{}", restoreFailure);
		}

		if (restoreSucceeded) {
			try {
				Time::SetTimeScale(1.0f);
				if (const FunkeProject* project = ProjectManager::GetCurrentProject()) {
					Time::SetFixedDeltaTime(project->GetFixedDeltaTimeSeconds());
					RenderSettings::Apply(project->Graphics);
				}
				ReapplyScriptFieldOverrides(restoredScenes);
			}
			catch (const std::exception& exception) {
				restoreSucceeded = false;
				restoreFailure = "Could not restore inspector-authored script fields: " + std::string(exception.what());
				FNK_ERROR_TAG("PlayMode", "{}", restoreFailure);
			}
			catch (...) {
				restoreSucceeded = false;
				restoreFailure = "Could not restore inspector-authored script fields.";
				FNK_ERROR_TAG("PlayMode", "{}", restoreFailure);
			}
		}

		if (restoreSucceeded) {
			for (const std::shared_ptr<Scene>& restoredScene : restoredScenes) {
				// Just deserialized from the Play snapshot, so the rollback baseline needs no extra round-trip check.
				if (!SceneManager::ActivateSuspendedScene(restoredScene, false)) {
					restoreSucceeded = false;
					restoreFailure = "Restored scene '" + restoredScene->GetName() + "' could not be activated.";
					break;
				}
				activatedScenes.push_back(restoredScene);
			}
		}
		if (restoreSucceeded) {
			try {
				restoreSucceeded = SceneManager::SetActiveScene(*restoredActiveScene);
			}
			catch (...) {
				restoreSucceeded = false;
			}
			if (!restoreSucceeded) {
				restoreFailure = "The restored active scene could not be selected.";
			}
		}

		bool recoveryExact = true;
		if (!restoreSucceeded) {
			for (auto it = activatedScenes.rbegin(); it != activatedScenes.rend(); ++it) {
				if (!SceneManager::ResuspendSceneForRecovery(*it)) {
					recoveryExact = false;
					FNK_ERROR_TAG("PlayMode", "Failed to restore suspended recovery state for scene '{}'.", (*it)->GetName());
				}
			}
			std::shared_ptr<Scene> recoveryActiveScene = restoredActiveScene;
			if (!recoveryActiveScene->IsLifecycleSuspended()) {
				recoveryActiveScene.reset();
				for (const std::shared_ptr<Scene>& restoredScene : restoredScenes) {
					if (restoredScene->IsLifecycleSuspended()) {
						recoveryActiveScene = restoredScene;
						break;
					}
				}
			}
			if (!recoveryActiveScene
				|| !SceneManager::SetActiveSuspendedSceneForRecovery(recoveryActiveScene)) {
				recoveryExact = false;
				FNK_ERROR_TAG("PlayMode", "Recovered edit scenes could not be selected for inspection.");
			}
			else {
				restoredActiveScene = std::move(recoveryActiveScene);
			}
		}
		else {
			for (const std::shared_ptr<Scene>& restoredScene : restoredScenes) {
				if (!SceneManager::CommitSuspendedSceneActivation(restoredScene)) {
					restoreSucceeded = false;
					recoveryExact = false;
					restoreFailure = "A restored scene activation could not be finalized.";
					FNK_ERROR_TAG("PlayMode", "Failed to finalize restored scene activation for '{}'.", restoredScene->GetName());
				}
			}
		}

		m_EditorOwnedSceneDefinitions.swap(restoredDefinitions);
		m_EditorSceneDefinitionsBySceneName.swap(restoredDefinitionsByName);
		InspectorUndoRecorder::Reset();
		m_ActiveUndoStack = nullptr;
		m_UndoStackSceneId = nullptr;
		m_SceneUndoHistory.Clear();
		if (restoreSucceeded) {
			for (auto& [name, history] : m_PlayModeUndoHistory) {
				m_SceneUndoHistory.Restore(SceneManager::GetLoadedScene(name).lock(), std::move(history));
			}
		}
		m_PlayModeUndoHistory.clear();
		m_GizmoDragActive = false;
		m_GizmoDragBefore.clear();
		m_EditorPausedAudioEntities.clear();
		m_PlayModeRecompilePending = false;
		m_PlayModeRecompileScene.reset();
		m_StepFrames = 0;
		ApplicationEditorAccess::SetPlaymodePaused(false);

		if (!inPrefabEdit) {
			ClearEntitySelection();
			m_CutEntities.clear();
			m_CollapsedHierarchyEntities.clear();
			m_EntityOrder.clear();
			m_EntityOrderSceneId = nullptr;
			m_LastHierarchyVersion = 0;
			m_EntityOrderDirty = true;
			m_ShowEntityDeleteConfirm = false;
			m_PendingEntityDeleteSceneId = 0;
			m_PendingEntityDeleteCount = 0;
			m_PendingEntityDeleteLabel.clear();

			if (!m_PlayModeSelection.SceneName.empty()
				&& SceneManager::GetLoadedScene(m_PlayModeSelection.SceneName).lock()) {
				RestoreLoadedSceneSelection(m_PlayModeSelection);
			}
			else {
				m_LastInteractedSceneName = restoredActiveScene->GetName();
			}
		}

		// Scenes restored from a private snapshot still differ from their file on disk.
		for (const std::string& dirtyName : m_PlayModeDirtyScenes) {
			if (std::shared_ptr<Scene> restored = SceneManager::GetLoadedScene(dirtyName).lock()) {
				restored->MarkDirty();
			}
		}
		m_PlayModeDirtyScenes.clear();
		m_PlayModeScenes.clear();
		m_PlayModeActiveScene.clear();
		m_PlayModeSelection = {};
		m_PlayModeScriptFields.clear();

		if (!restoreSucceeded) {
			if (restoreFailure.empty()) restoreFailure = "The edit-scene restore did not complete.";
			m_PlayModeRestoreError = restoreFailure
				+ (recoveryExact
					? " Play Mode stopped and the recovered edit scenes remain loaded for inspection."
					: " Play Mode stopped, but recovery was incomplete. Inspect the loaded scenes before saving.");
			m_ShowPlayModeRestoreError = true;
			FNK_ERROR_TAG("PlayMode", "{}", m_PlayModeRestoreError);
			return false;
		}
		const double activateMs = stageMs();
		FNK_INFO_TAG("PlayMode", "Stopped in {:.1f} ms (reload {:.1f}, suspend {:.1f}, release {:.1f}, scripts {:.1f}, activate {:.1f})",
			std::chrono::duration<double, std::milli>(StopClock::now() - stopStart).count(),
			reloadMs, suspendMs, releaseMs, scriptsMs, activateMs);
		return true;
	}

	void ImGuiEditorLayer::SelectSceneNode() {
		ResetEditorFocusCycle();
		m_SelectedEntity = entt::null;
		m_PressedEntity = entt::null;
		m_SelectedEntities.clear();
		m_SelectedEntitySet.clear();
		++m_SelectionVersion;
		m_LastEntitySelectionIndex = -1;
		m_RenamingEntity = entt::null;
		m_EntityRenameFrameCounter = 0;
		m_IsSceneNodeSelected = true;
		m_AssetBrowser.ClearSelection();
	}

	void ImGuiEditorLayer::SelectEntity(EntityHandle entity) {
		ResetEditorFocusCycle();
		m_SelectedEntity = entity;
		m_SelectedEntities.clear();
		m_SelectedEntitySet.clear();
		if (entity != entt::null) {
			m_SelectedEntities.push_back(entity);
			m_SelectedEntitySet.insert(entity);
		}
		++m_SelectionVersion;
		m_LastEntitySelectionIndex = -1;
		m_IsSceneNodeSelected = false;
		if (entity != entt::null) {
			m_AssetBrowser.ClearSelection();
		}
	}

	EntityHandle ImGuiEditorLayer::SpawnAssetEntityAt(Scene& scene, const std::string& assetPath, const Vec2& worldPos) {
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
			m_EntityOrder.clear();
			m_EntityOrderDirty = true;
			++m_SelectionVersion;
			RecordCreatedEntities(scene, std::span(&created, 1), ext == ".prefab" ? "Instantiate Prefab" : "Create Sprite");
		}
		return created;
	}

	void ImGuiEditorLayer::ClearEntitySelection() {
		ResetEditorFocusCycle();
		m_SelectedEntity = entt::null;
		m_PressedEntity = entt::null;
		m_SelectedEntities.clear();
		m_SelectedEntitySet.clear();
		++m_SelectionVersion;
		m_LastEntitySelectionIndex = -1;
		m_RenamingEntity = entt::null;
		m_EntityRenameFrameCounter = 0;
		m_IsSceneNodeSelected = false;
	}

	void ImGuiEditorLayer::RebuildSelectionSet() {
		m_SelectedEntitySet.clear();
		m_SelectedEntitySet.reserve(m_SelectedEntities.size());
		for (EntityHandle e : m_SelectedEntities) {
			if (e != entt::null) m_SelectedEntitySet.insert(e);
		}
		++m_SelectionVersion;
	}

	bool ImGuiEditorLayer::IsEntitySelected(EntityHandle entity) const {
		// O(1) lookup; linear scan over large multi-selections was a dominant frame cost.
		return m_SelectedEntitySet.find(entity) != m_SelectedEntitySet.end();
	}

	std::vector<EntityHandle> ImGuiEditorLayer::GetSelectedEntities(const Scene& scene) const {
		// Dedup via hash set; old std::find was O(N²) at 25k-entity selections.
		std::vector<EntityHandle> entities;
		entities.reserve(m_SelectedEntities.size());
		std::unordered_set<EntityHandle> seen;
		seen.reserve(m_SelectedEntities.size());
		for (EntityHandle entity : m_SelectedEntities) {
			if (entity == entt::null || !scene.IsValid(entity)) continue;
			if (seen.insert(entity).second) {
				entities.push_back(entity);
			}
		}

		if (entities.empty() && m_SelectedEntity != entt::null && scene.IsValid(m_SelectedEntity)) {
			entities.push_back(m_SelectedEntity);
		}

		return entities;
	}

	void ImGuiEditorLayer::SetSingleEntitySelection(EntityHandle entity, int index) {
		ResetEditorFocusCycle();
		m_SelectedEntity = entity;
		m_SelectedEntities.clear();
		m_SelectedEntitySet.clear();
		if (entity != entt::null) {
			m_SelectedEntities.push_back(entity);
			m_SelectedEntitySet.insert(entity);
			m_AssetBrowser.ClearSelection();
		}
		++m_SelectionVersion;
		m_LastEntitySelectionIndex = index;
		m_IsSceneNodeSelected = false;
	}

	void ImGuiEditorLayer::ToggleEntitySelection(EntityHandle entity, int index) {
		ResetEditorFocusCycle();
		auto setIt = m_SelectedEntitySet.find(entity);
		if (setIt != m_SelectedEntitySet.end()) {
			m_SelectedEntitySet.erase(setIt);
			auto it = std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity);
			if (it != m_SelectedEntities.end()) {
				m_SelectedEntities.erase(it);
			}
			if (m_SelectedEntity == entity) {
				m_SelectedEntity = m_SelectedEntities.empty() ? entt::null : m_SelectedEntities.back();
			}
		}
		else if (entity != entt::null) {
			m_SelectedEntities.push_back(entity);
			m_SelectedEntitySet.insert(entity);
			m_SelectedEntity = entity;
			m_AssetBrowser.ClearSelection();
		}

		++m_SelectionVersion;
		m_LastEntitySelectionIndex = index;
		m_IsSceneNodeSelected = false;
	}

	void ImGuiEditorLayer::SelectEntitiesInBox(const std::vector<EntityHandle>& entities, bool additive) {
		ResetEditorFocusCycle();
		if (!additive) {
			m_SelectedEntities.clear();
			m_SelectedEntitySet.clear();
			m_SelectedEntity = entt::null;
		}
		for (EntityHandle entity : entities) {
			if (entity == entt::null) continue;
			if (m_SelectedEntitySet.insert(entity).second) {
				m_SelectedEntities.push_back(entity);
				m_SelectedEntity = entity;
			}
		}
		if (!m_SelectedEntities.empty()) {
			m_AssetBrowser.ClearSelection();
		}
		++m_SelectionVersion;
		m_LastEntitySelectionIndex = -1;
		m_IsSceneNodeSelected = false;
	}

	void ImGuiEditorLayer::SelectEntityRange(int index) {
		ResetEditorFocusCycle();
		const auto& order = !m_VisibleEntityOrder.empty() ? m_VisibleEntityOrder : m_EntityOrder;
		if (order.empty()) {
			return;
		}

		if (index < 0 || index >= static_cast<int>(order.size())) {
			return;
		}

		int anchorIndex = m_LastEntitySelectionIndex;
		if (anchorIndex < 0 || anchorIndex >= static_cast<int>(order.size())) {
			for (int i = 0; i < static_cast<int>(order.size()); ++i) {
				if (order[static_cast<std::size_t>(i)] == m_SelectedEntity) {
					anchorIndex = i;
					break;
				}
			}
		}
		if (anchorIndex < 0 || anchorIndex >= static_cast<int>(order.size())) {
			for (EntityHandle selected : m_SelectedEntities) {
				for (int i = 0; i < static_cast<int>(order.size()); ++i) {
					if (order[static_cast<std::size_t>(i)] == selected) {
						anchorIndex = i;
						break;
					}
				}
				if (anchorIndex >= 0) break;
			}
		}

		if (anchorIndex < 0 || anchorIndex >= static_cast<int>(order.size())) {
			SetSingleEntitySelection(order[static_cast<std::size_t>(index)], index);
			return;
		}

		const int first = std::min(anchorIndex, index);
		const int last = std::max(anchorIndex, index);
		m_SelectedEntities.clear();
		for (int i = first; i <= last; ++i) {
			m_SelectedEntities.push_back(order[static_cast<std::size_t>(i)]);
		}
		RebuildSelectionSet();
		m_SelectedEntity = order[static_cast<std::size_t>(index)];
		m_LastEntitySelectionIndex = index;
		m_IsSceneNodeSelected = false;
		m_AssetBrowser.ClearSelection();
	}

	void ImGuiEditorLayer::RecomputeInspectorSelectionCache(Scene& scene) {
		// Runs once per selection version bump; avoids O(N×M) sweep over (selection × components) every frame.
		m_InspectorCache.Handles = GetSelectedEntities(scene);
		m_InspectorCache.CommonComponentTypes.clear();
		m_InspectorCache.PartialComponents.clear();

		if (m_InspectorCache.Handles.empty()) {
			m_InspectorCache.NameUniform = true;
			m_InspectorCache.FirstName.clear();
			m_InspectorCache.EnabledUniform = true;
			m_InspectorCache.EnabledFirst = true;
			m_InspectorCache.StaticUniform = true;
			m_InspectorCache.StaticFirst = false;
			return;
		}

		std::vector<Entity> entities;
		entities.reserve(m_InspectorCache.Handles.size());
		for (EntityHandle h : m_InspectorCache.Handles) {
			entities.push_back(scene.GetEntity(h));
		}

		{
			// Empty FirstName means no NameComponent; InputText renders a hint instead of pre-filling to avoid accidental creation.
			Entity& first = entities[0];
			m_InspectorCache.FirstName = first.HasComponent<NameComponent>()
				? first.GetComponent<NameComponent>().Name
				: std::string{};
			m_InspectorCache.NameUniform = true;
			const std::string_view firstView = m_InspectorCache.FirstName;
			for (std::size_t i = 1; i < entities.size(); ++i) {
				Entity& e = entities[i];
				const std::string_view name = e.HasComponent<NameComponent>()
					? std::string_view(e.GetComponent<NameComponent>().Name)
					: std::string_view{};
				if (name != firstView) { m_InspectorCache.NameUniform = false; break; }
			}
		}

		{
			auto enabledOf = [](Entity& e) {
				return !e.HasComponent<DisabledTag>() || e.HasComponent<InheritedDisabledTag>();
			};
			auto staticOf = [](Entity& e) { return e.HasComponent<StaticTag>(); };

			m_InspectorCache.EnabledFirst = enabledOf(entities[0]);
			m_InspectorCache.StaticFirst = staticOf(entities[0]);
			m_InspectorCache.EnabledUniform = true;
			m_InspectorCache.StaticUniform = true;
			for (std::size_t i = 1; i < entities.size(); ++i) {
				if (m_InspectorCache.EnabledUniform && enabledOf(entities[i]) != m_InspectorCache.EnabledFirst) {
					m_InspectorCache.EnabledUniform = false;
				}
				if (m_InspectorCache.StaticUniform && staticOf(entities[i]) != m_InspectorCache.StaticFirst) {
					m_InspectorCache.StaticUniform = false;
				}
				if (!m_InspectorCache.EnabledUniform && !m_InspectorCache.StaticUniform) break;
			}
		}

		const auto& registry = SceneManager::GetComponentRegistry();
		registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
			if (info.category != ComponentCategory::Component) return;
			if (info.displayName == "Name") return;

			std::size_t haveCount = 0;
			for (const Entity& e : entities) {
				if (info.has(e)) ++haveCount;
			}
			if (haveCount == 0) return;
			if (haveCount < entities.size()) {
				m_InspectorCache.PartialComponents.push_back(info.displayName);
			}
			else {
				m_InspectorCache.CommonComponentTypes.insert(typeId);
			}
		});
	}

	void ImGuiEditorLayer::ResetEditorFocusCycle() {
		m_EditorCameraFocusActive = false;
		m_EditorCameraFocusElapsed = 0.0f;
		m_FocusLastEntity = entt::null;
		m_FocusNextPressTight = false;
	}

	void ImGuiEditorLayer::UpdateEditorCameraFocus(float dt) {
		if (!m_EditorCameraFocusActive) {
			return;
		}

		// Fixed-duration (not exponential-decay) so perceived animation time is distance-independent.
		constexpr float k_FocusDuration = 0.25f;
		m_EditorCameraFocusElapsed += Max(0.0f, dt);
		const float t = Min(1.0f, m_EditorCameraFocusElapsed / k_FocusDuration);
		const float eased = t * t * (3.0f - 2.0f * t);

		const Vec2 nextPosition = Vector::Lerp(m_EditorCameraFocusStartPosition, m_EditorCameraFocusTarget, eased);
		const float nextSize = m_EditorCameraFocusStartOrthoSize
			+ (m_EditorCameraFocusOrthoSize - m_EditorCameraFocusStartOrthoSize) * eased;

		m_EditorCamera.SetPosition(nextPosition);
		m_EditorCamera.SetOrthographicSize(nextSize);

		if (t >= 1.0f) {
			m_EditorCamera.SetPosition(m_EditorCameraFocusTarget);
			m_EditorCamera.SetOrthographicSize(m_EditorCameraFocusOrthoSize);
			m_EditorCameraFocusActive = false;
			m_EditorCameraFocusElapsed = 0.0f;
		}
	}

	bool ImGuiEditorLayer::TryBuildEntityAABB(Scene& scene, EntityHandle entity, bool includeChildren, AABB& outAABB) {
		if (entity == entt::null || !scene.IsValid(entity)) {
			return false;
		}

		TransformHierarchySystem::Propagate(scene);
		ComputeUILayout(scene);

		bool hasBounds = false;
		auto includeAABB = [&](const AABB& aabb) {
			if (!hasBounds) {
				outAABB = aabb;
				hasBounds = true;
				return;
			}
			outAABB.Min.x = std::min(outAABB.Min.x, aabb.Min.x);
			outAABB.Min.y = std::min(outAABB.Min.y, aabb.Min.y);
			outAABB.Max.x = std::max(outAABB.Max.x, aabb.Max.x);
			outAABB.Max.y = std::max(outAABB.Max.y, aabb.Max.y);
		};

		std::unordered_set<uint32_t> visited;
		std::function<void(EntityHandle)> visit = [&](EntityHandle current) {
			if (current == entt::null || !scene.IsValid(current)) {
				return;
			}
			if (!visited.insert(static_cast<uint32_t>(current)).second) {
				return;
			}

			Transform2DComponent* transform = nullptr;
			if (scene.TryGetComponent<Transform2DComponent>(current, transform) && transform) {
				includeAABB(AABB::FromTransform(*transform));
			}

			RectTransform2DComponent* rect = nullptr;
			if (scene.TryGetComponent<RectTransform2DComponent>(current, rect) && rect) {
				const float worldScale = GuiRenderer::ComputeWorldUIPixelScale();
				const Vec2 bl = rect->GetBottomLeft();
				const Vec2 tr = rect->GetTopRight();
				const Vec2 pivot = rect->ResolvedValid ? rect->ResolvedPivot
					: Vec2{ (bl.x + tr.x) * 0.5f, (bl.y + tr.y) * 0.5f };

				Vec2 corners[4] = {
					Vec2{ bl.x * worldScale, bl.y * worldScale },
					Vec2{ tr.x * worldScale, bl.y * worldScale },
					Vec2{ tr.x * worldScale, tr.y * worldScale },
					Vec2{ bl.x * worldScale, tr.y * worldScale },
				};

				if (rect->Rotation != 0.0f) {
					const Vec2 worldPivot{ pivot.x * worldScale, pivot.y * worldScale };
					for (Vec2& corner : corners) {
						corner = worldPivot + Vector::Rotated(corner - worldPivot, rect->Rotation);
					}
				}

				Vec2 minPoint = corners[0];
				Vec2 maxPoint = corners[0];
				for (int i = 1; i < 4; ++i) {
					minPoint.x = std::min(minPoint.x, corners[i].x);
					minPoint.y = std::min(minPoint.y, corners[i].y);
					maxPoint.x = std::max(maxPoint.x, corners[i].x);
					maxPoint.y = std::max(maxPoint.y, corners[i].y);
				}
				includeAABB({ minPoint, maxPoint });
			}

			if (includeChildren && scene.HasComponent<HierarchyComponent>(current)) {
				for (EntityHandle child : scene.GetComponent<HierarchyComponent>(current).Children) {
					visit(child);
				}
			}
		};

		visit(entity);
		return hasBounds;
	}

	void ImGuiEditorLayer::FocusSelectedEntity(Scene& scene) {
		if (m_SelectedEntity == entt::null || !scene.IsValid(m_SelectedEntity)) {
			return;
		}
		if (m_EditorView3D) {
			FocusSelectedEntity3D(scene);
			return;
		}

		const bool tightFocus = m_FocusLastEntity == m_SelectedEntity && m_FocusNextPressTight;
		AABB bounds;
		if (!TryBuildEntityAABB(scene, m_SelectedEntity, !tightFocus, bounds)) {
			return;
		}

		const Vec2 size = bounds.Scale();
		const Vec2 center{
			(bounds.Min.x + bounds.Max.x) * 0.5f,
			(bounds.Min.y + bounds.Max.y) * 0.5f
		};

		const AABB viewport = m_EditorCamera.GetViewportAABB();
		const Vec2 viewportSize = viewport.Scale();
		const float aspect = viewportSize.y > 0.0001f ? viewportSize.x / viewportSize.y : 16.0f / 9.0f;
		const float paddedHalfWidth = std::max(std::abs(size.x) * 0.5f, 0.5f) * 1.25f;
		const float paddedHalfHeight = std::max(std::abs(size.y) * 0.5f, 0.5f) * 1.25f;
		const float targetHalfHeight = std::max(paddedHalfHeight, paddedHalfWidth / std::max(aspect, 0.0001f));
		const float zoom = std::max(m_EditorCamera.GetZoom(), 0.0001f);

		m_EditorCameraFocusTarget = center;
		m_EditorCameraFocusOrthoSize = Clamp(targetHalfHeight / zoom,
			EditorCamera::k_MinOrthographicSize, EditorCamera::k_MaxOrthographicSize);
		m_EditorCameraFocusStartPosition = m_EditorCamera.GetPosition();
		m_EditorCameraFocusStartOrthoSize = m_EditorCamera.GetOrthographicSize();
		m_EditorCameraFocusElapsed = 0.0f;
		m_EditorCameraFocusActive = true;
		m_FocusLastEntity = m_SelectedEntity;
		m_FocusNextPressTight = !tightFocus;
	}

	void ImGuiEditorLayer::PushStructureUndo(Scene& scene, std::unique_ptr<EditCommand> command) {
		if (!command || (Application::GetIsPlaying() && !scene.IsDetached())) return;
		UndoStack& stack = GetUndoStack(scene);
		InspectorUndoRecorder::Flush(stack, true);
		stack.Push(std::move(command));
	}

	void ImGuiEditorLayer::RecordCreatedEntities(Scene& scene, std::span<const EntityHandle> created, std::string label) {
		if (created.empty()) return;
		StructureEditRecorder recorder(scene, {}, std::move(label));
		PushStructureUndo(scene, recorder.Finish(scene, created));
	}

	void ImGuiEditorLayer::DuplicateSelectedEntity(Scene& scene) {
		const std::vector<EntityHandle> selectedEntities = FilterSelectedHierarchyRoots(scene, GetSelectedEntities(scene));
		if (selectedEntities.empty()) {
			return;
		}

		std::vector<EntityHandle> createdEntities;
		for (EntityHandle sourceEntity : selectedEntities) {
			Json::Value entityValue = SceneSerializer::SerializeEntityForClipboard(scene, sourceEntity);
			if (!entityValue.IsObject()) {
				continue;
			}

			EntityHandle clone = SceneSerializer::DeserializeEntityFromValue(scene, entityValue);
			if (clone != entt::null) {
				Entity source = scene.GetEntity(sourceEntity);
				Entity sourceParent = source.GetParent();
				if (sourceParent.IsValid()) {
					Entity cloneEntity = scene.GetEntity(clone);
					cloneEntity.SetParent(sourceParent);
					MoveSiblingNextTo(scene, clone, sourceEntity, true);
					m_CollapsedHierarchyEntities.erase(static_cast<uint32_t>(sourceParent.GetHandle()));
				}
				createdEntities.push_back(clone);
			}
		}

		if (!createdEntities.empty()) {
			EnsureEditorUniqueEntityNames(scene, createdEntities);
			m_SelectedEntities = createdEntities;
			RebuildSelectionSet();
			m_SelectedEntity = createdEntities.back();
			m_LastEntitySelectionIndex = -1;
			m_IsSceneNodeSelected = false;
			scene.MarkDirty();
			m_EntityOrder.clear(); m_EntityOrderDirty = true;
			RecordCreatedEntities(scene, createdEntities, "Duplicate");
		}
	}

	void ImGuiEditorLayer::DeleteSelectedEntity(Scene& scene) {
		const std::vector<EntityHandle> selectedEntities = GetSelectedEntities(scene);
		if (selectedEntities.empty()) {
			return;
		}

		m_PressedEntity = entt::null;

		StructureEditRecorder recorder(scene, selectedEntities, selectedEntities.size() == 1 ? "Delete Entity" : "Delete Entities");
		for (EntityHandle entity : selectedEntities) {
			if (scene.IsValid(entity)) {
				scene.DestroyEntity(entity);
			}
		}
		PushStructureUndo(scene, recorder.Finish(scene, {}));
		ClearEntitySelection();
		m_EntityOrder.clear(); m_EntityOrderDirty = true;
	}

	void ImGuiEditorLayer::RequestDeleteSelectedEntity(Scene& scene) {
		const std::vector<EntityHandle> selectedEntities = GetSelectedEntities(scene);
		if (selectedEntities.empty()) {
			return;
		}

		// Confirmation disabled in preferences: delete straight away.
		if (!EditorPreferences::GetConfirmOnDeleteEntity()) {
			DeleteSelectedEntity(scene);
			return;
		}

		// Capture the scene id + a label for the modal. The modal blocks
		// interaction, so the selection DeleteSelectedEntity re-reads on
		// confirm stays frozen between now and then.
		m_PendingEntityDeleteSceneId = static_cast<uint64_t>(scene.GetSceneId());
		m_PendingEntityDeleteCount = selectedEntities.size();
		m_PendingEntityDeleteLabel.clear();
		if (selectedEntities.size() == 1) {
			const EntityHandle handle = selectedEntities.front();
			if (scene.HasComponent<NameComponent>(handle)) {
				m_PendingEntityDeleteLabel = scene.GetComponent<NameComponent>(handle).Name;
			}
			if (m_PendingEntityDeleteLabel.empty()) {
				m_PendingEntityDeleteLabel = "Entity";
			}
		}
		m_ShowEntityDeleteConfirm = true;
	}

	void ImGuiEditorLayer::BeginRenameSelectedEntity(Scene& scene) {
		if (m_SelectedEntity == entt::null || !scene.IsValid(m_SelectedEntity)) {
			return;
		}

		Entity entity = scene.GetEntity(m_SelectedEntity);
		m_RenamingEntity = m_SelectedEntity;
		m_EntityRenameFrameCounter = 0;
		// Empty buffer for nameless entities: pre-filling GetName()'s placeholder would commit a real NameComponent with synthesized text.
		if (entity.HasComponent<NameComponent>()) {
			std::snprintf(m_EntityRenameBuffer, sizeof(m_EntityRenameBuffer), "%s",
				entity.GetComponent<NameComponent>().Name.c_str());
		}
		else {
			m_EntityRenameBuffer[0] = '\0';
		}
	}

	void ImGuiEditorLayer::UnpackSelectedPrefabs(Scene& scene) {
		const std::vector<EntityHandle> selectedEntities = GetSelectedEntities(scene);
		if (selectedEntities.empty()) {
			return;
		}

		std::vector<EntityHandle> instanceRoots;
		for (EntityHandle selected : selectedEntities) {
			if (!scene.IsValid(selected) || !scene.HasComponent<PrefabInstanceComponent>(selected)) continue;
			const EntityHandle root = SceneSerializer::GetPrefabInstanceRoot(scene, selected);
			instanceRoots.push_back(root != entt::null ? root : selected);
		}
		StructureEditRecorder recorder(scene, instanceRoots, "Unpack Prefab");

		bool anyUnpacked = false;
		for (EntityHandle selected : selectedEntities) {
			if (!scene.IsValid(selected)) continue;
			if (!scene.HasComponent<PrefabInstanceComponent>(selected)) continue;

			// Unpack the whole instance, not just the selected node: every entity in the
			// subtree carries Origin::Prefab + the same GUID, so clearing only the root left
			// children rendering as prefab instances. Resolve to the instance root so it works
			// regardless of which node is selected; stop at a different GUID so a nested,
			// separately-instanced child prefab stays an instance (matches GetPrefabInstanceRoot).
			const EntityHandle root = SceneSerializer::GetPrefabInstanceRoot(scene, selected);
			const EntityHandle start = (root != entt::null) ? root : selected;
			const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(start));

			std::vector<EntityHandle> stack{ start };
			while (!stack.empty()) {
				const EntityHandle current = stack.back();
				stack.pop_back();
				if (!scene.IsValid(current)) continue;
				if (scene.GetEntityOrigin(current) != EntityOrigin::Prefab) continue;
				if (static_cast<uint64_t>(scene.GetPrefabGUID(current)) != prefabGuid) continue;

				if (scene.HasComponent<HierarchyComponent>(current)) {
					for (EntityHandle child : scene.GetComponent<HierarchyComponent>(current).Children) {
						stack.push_back(child);
					}
				}

				scene.SetEntityMetaData(current, EntityOrigin::Scene);
				anyUnpacked = true;
			}
		}
		if (anyUnpacked) {
			scene.MarkDirty();
			PushStructureUndo(scene, recorder.Finish(scene, instanceRoots));
		}
	}

	void ImGuiEditorLayer::CopySelectedEntities(Scene& scene) {
		const std::vector<EntityHandle> selectedEntities = FilterSelectedHierarchyRoots(scene, GetSelectedEntities(scene));
		if (selectedEntities.empty()) {
			return;
		}

		Json::Value root = Json::Value::MakeObject();
		root.AddMember("type", Json::Value("EntityClipboard"));

		Json::Value entities = Json::Value::MakeArray();
		for (EntityHandle entity : selectedEntities) {
			Json::Value entityValue = SceneSerializer::SerializeEntityForClipboard(scene, entity);
			if (entityValue.IsObject()) {
				entities.Append(std::move(entityValue));
			}
		}

		if (entities.GetArray().empty()) {
			return;
		}

		root.AddMember("entities", std::move(entities));
		m_EntityClipboardJson = Json::Stringify(root, false);
	}

	void ImGuiEditorLayer::CutSelectedEntities(Scene& scene) {
		const std::vector<EntityHandle> selectedEntities = GetSelectedEntities(scene);
		if (selectedEntities.empty()) {
			return;
		}

		// New cut supersedes prior cut: drop old marker (entities stay) and snapshot new selection.
		m_CutEntities.clear();
		m_EntityClipboardJson.clear();
		CopySelectedEntities(scene);
		if (!m_EntityClipboardJson.empty()) {
			for (EntityHandle h : selectedEntities) {
				m_CutEntities.insert(static_cast<uint32_t>(h));
			}
		}
	}

	void ImGuiEditorLayer::PasteEntities(Scene& scene) {
		if (m_EntityClipboardJson.empty()) {
			return;
		}

		Json::Value root;
		std::string parseError;
		if (!Json::TryParse(m_EntityClipboardJson, root, &parseError) || !root.IsObject()) {
			return;
		}

		const Json::Value* typeValue = root.FindMember("type");
		const Json::Value* entitiesValue = root.FindMember("entities");
		if (!typeValue || typeValue->AsStringOr() != "EntityClipboard" || !entitiesValue || !entitiesValue->IsArray()) {
			return;
		}

		EntityHandle pasteParent = (!m_IsSceneNodeSelected
			&& m_SelectedEntity != entt::null
			&& scene.IsValid(m_SelectedEntity))
			? m_SelectedEntity
			: entt::null;
		// Prefab mode: rootless paste would create a sibling-to-root that the serializer silently drops; force under prefab root.
		if (pasteParent == entt::null
			&& IsInPrefabEditMode()
			&& m_PrefabEditScene
			&& m_PrefabEditRootEntity != entt::null
			&& m_PrefabEditScene->IsValid(m_PrefabEditRootEntity)
			&& &scene == m_PrefabEditScene.get())
		{
			pasteParent = m_PrefabEditRootEntity;
		}

		std::vector<EntityHandle> cutEntities;
		for (uint32_t raw : m_CutEntities) {
			const EntityHandle h = static_cast<EntityHandle>(raw);
			if (scene.IsValid(h)) cutEntities.push_back(h);
		}
		StructureEditRecorder recorder(scene, cutEntities, cutEntities.empty() ? "Paste" : "Cut & Paste");

		std::vector<EntityHandle> createdEntities;
		for (const Json::Value& entityValue : entitiesValue->GetArray()) {
			EntityHandle entity = SceneSerializer::DeserializeEntityFromValue(scene, entityValue);
			if (entity != entt::null) {
				if (pasteParent != entt::null) {
					scene.GetEntity(entity).SetParent(scene.GetEntity(pasteParent));
				}
				createdEntities.push_back(entity);
			}
		}

		if (!createdEntities.empty()) {
			EnsureEditorUniqueEntityNames(scene, createdEntities);
			m_SelectedEntities = createdEntities;
			RebuildSelectionSet();
			m_SelectedEntity = createdEntities.back();
			m_LastEntitySelectionIndex = -1;
			m_IsSceneNodeSelected = false;
			if (pasteParent != entt::null) {
				m_CollapsedHierarchyEntities.erase(static_cast<uint32_t>(pasteParent));
			}
			scene.MarkDirty();
			m_EntityOrder.clear(); m_EntityOrderDirty = true;

			if (!m_CutEntities.empty()) {
				for (uint32_t raw : m_CutEntities) {
					EntityHandle h = static_cast<EntityHandle>(raw);
					if (scene.IsValid(h)) {
						scene.DestroyEntity(h);
					}
				}
				m_CutEntities.clear();
				m_EntityOrderDirty = true;
			}
			PushStructureUndo(scene, recorder.Finish(scene, createdEntities));
		}
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
			m_CollapsedHierarchyEntities.erase(static_cast<uint32_t>(effectiveParent.GetHandle()));
		}

		const EntityHandle createdHandle = created.GetHandle();
		EnsureEditorUniqueEntityNames(scene, { createdHandle });
		SelectEntity(createdHandle);
		scene.MarkDirty();
		m_EntityOrder.clear();
		m_EntityOrderDirty = true;
		RecordCreatedEntities(scene, std::span(&createdHandle, 1), "Create " + created.GetName());
		return createdHandle;
	}

	EntityHandle ImGuiEditorLayer::RenderCreateEntityMenu(Scene& scene, Entity parent) {
		EntityHandle createdHandle = entt::null;

		auto finishCreated = [&](Entity created) {
			createdHandle = FinishCreatedEditorEntity(scene, parent, created);
		};

		if (ImGui::MenuItem("Create (Empty)", "Shift + C"))
		{
			finishCreated(scene.CreateEmptyEntity());
		}
		if (ImGui::MenuItem("Create (Entity)"))
		{
			finishCreated(CreateDefaultEntity(scene, "Entity"));
		}

		auto renderPackagePresets = [&](std::string_view menuPath) {
			bool renderedAny = false;
			for (const SceneManager::EntityPresetInfo& preset : SceneManager::GetEntityPresets()) {
				if (preset.MenuPath != menuPath || !preset.IsModuleActive()) {
					continue;
				}
				if (!renderedAny) {
					ImGui::Separator();
					renderedAny = true;
				}
				if (ImGui::MenuItem(preset.Label.c_str())) {
					finishCreated(preset.Create(scene));
				}
			}
		};

		ImGui::Separator();

		if (EngineModules::IsActive(EngineModule::TwoD) && ImGui::BeginMenu("2D Entity"))
		{
			if (ImGui::BeginMenu("Sprite"))
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
			renderPackagePresets("2D Entity");

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("UI"))
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
			renderPackagePresets("UI");
			ImGui::EndMenu();
		}

		std::vector<std::string> packageMenuPaths;
		for (const SceneManager::EntityPresetInfo& preset : SceneManager::GetEntityPresets()) {
			if (preset.MenuPath.empty() || preset.MenuPath == "2D Entity" || preset.MenuPath == "UI" || !preset.IsModuleActive()) {
				continue;
			}
			if (std::find(packageMenuPaths.begin(), packageMenuPaths.end(), preset.MenuPath) == packageMenuPaths.end()) {
				packageMenuPaths.push_back(preset.MenuPath);
			}
		}
		for (const std::string& menuPath : packageMenuPaths) {
			if (ImGui::BeginMenu(menuPath.c_str())) {
				for (const SceneManager::EntityPresetInfo& preset : SceneManager::GetEntityPresets()) {
					if (preset.MenuPath == menuPath && preset.IsModuleActive() && ImGui::MenuItem(preset.Label.c_str())) {
						finishCreated(preset.Create(scene));
					}
				}
				ImGui::EndMenu();
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Camera"))
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

		if (ImGui::MenuItem("Audio Source"))
		{
			Entity created = scene.CreateEntity("Audio Source");
			created.AddComponent<AudioSourceComponent>();
			finishCreated(created);
		}

		return createdHandle;
	}

	bool ImGuiEditorLayer::SetEntityParentPreservingWorld(Scene& scene, EntityHandle childHandle, Entity parent) {
		if (childHandle == entt::null || !scene.IsValid(childHandle)) {
			return false;
		}
		if (parent.IsValid() && parent.GetScene() != &scene) {
			return false;
		}

		// Prefab mode: refuse unparenting-to-root (creates second root) and reparenting the prefab root (orphans subtree).
		if (IsInPrefabEditMode() && m_PrefabEditScene && &scene == m_PrefabEditScene.get()) {
			if (!parent.IsValid()) {
				return false;
			}
			if (childHandle == m_PrefabEditRootEntity) {
				return false;
			}
		}

		Entity child = scene.GetEntity(childHandle);
		if (parent.IsValid() && (parent.GetHandle() == childHandle || child.IsAncestorOf(parent))) {
			return false;
		}
		if (child.GetParent() == parent) {
			return false;
		}

		TransformHierarchySystem::Propagate(scene);

		Transform2DComponent* childTransform = nullptr;
		const bool hasTransform = scene.TryGetComponent<Transform2DComponent>(childHandle, childTransform) && childTransform;
		Vec2 worldPosition{ 0.0f, 0.0f };
		Vec2 worldScale{ 1.0f, 1.0f };
		float worldRotation = 0.0f;
		if (hasTransform) {
			worldPosition = childTransform->Position;
			worldRotation = childTransform->Rotation;
			worldScale = childTransform->Scale;
		}

		child.SetParent(parent);

		if (hasTransform) {
			Transform2DComponent* parentTransform = nullptr;
			if (parent.IsValid()) {
				scene.TryGetComponent<Transform2DComponent>(parent.GetHandle(), parentTransform);
			}
			SetLocalTransformFromWorld(*childTransform, parentTransform, worldPosition, worldRotation, worldScale);
			TransformHierarchySystem::Propagate(scene);
		}

		return true;
	}

	std::vector<EntityHandle> ImGuiEditorLayer::MigrateEntitiesToScene(Scene& sourceScene, Scene& targetScene,
		const std::vector<EntityHandle>& sourceRoots, Entity targetParent) {
		std::vector<EntityHandle> migratedRoots;
		if (&sourceScene == &targetScene) {
			FNK_CORE_WARN_TAG("Editor", "MigrateEntitiesToScene called with sourceScene == targetScene; ignoring");
			return migratedRoots;
		}
		if (sourceRoots.empty()) return migratedRoots;

		// Snapshot first, destroy after: destroying a parent before snapshotting its children would dangle JSON references.
		std::vector<Json::Value> snapshots;
		std::vector<EntityHandle> consumed;
		snapshots.reserve(sourceRoots.size());
		consumed.reserve(sourceRoots.size());
		for (EntityHandle root : sourceRoots) {
			if (!sourceScene.IsValid(root)) continue;
			Json::Value snapshot = SceneSerializer::SerializeEntityForClipboard(sourceScene, root);
			if (!snapshot.IsObject()) continue;
			snapshots.push_back(std::move(snapshot));
			consumed.push_back(root);
		}
		if (snapshots.empty()) return migratedRoots;

		migratedRoots.reserve(snapshots.size());
		for (Json::Value& snapshot : snapshots) {
			EntityHandle clone = SceneSerializer::DeserializeEntityFromValue(targetScene, snapshot);
			if (clone == entt::null) continue;
			if (targetParent.IsValid() && targetScene.IsValid(targetParent.GetHandle())) {
				Entity cloneEntity = targetScene.GetEntity(clone);
				cloneEntity.SetParent(targetParent);
			}
			migratedRoots.push_back(clone);
		}

		// Destroy originals even on partial deserialize failure: the gesture was "move", so leaving originals would silently duplicate.
		for (EntityHandle source : consumed) {
			if (sourceScene.IsValid(source)) {
				sourceScene.DestroyEntity(source);
			}
		}

		if (!migratedRoots.empty()) {
			EnsureEditorUniqueEntityNames(targetScene, migratedRoots);
			sourceScene.MarkDirty();
			targetScene.MarkDirty();
		}
		return migratedRoots;
	}

	std::vector<EntityHandle> ImGuiEditorLayer::ResolveDraggedHierarchyEntities(Scene& scene, EntityHandle primary) const {
		std::vector<EntityHandle> result;

		// Drag an unselected entity: single-entity move. Drag within selection: move the whole selection.
		const bool draggedIsInSelection =
			std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), primary) != m_SelectedEntities.end();
		if (draggedIsInSelection && m_SelectedEntities.size() > 1) {
			result = FilterSelectedHierarchyRoots(scene, GetSelectedEntities(scene));
		}

		if (result.empty()) {
			if (primary != entt::null && scene.IsValid(primary)) {
				result.push_back(primary);
			}
		}
		return result;
	}

	bool ImGuiEditorLayer::MoveSiblingNextTo(Scene& scene, EntityHandle dragged, EntityHandle target, bool insertAfter)
	{
		if (dragged == target || !scene.IsValid(dragged) || !scene.IsValid(target)) return false;

		Entity draggedEntity = scene.GetEntity(dragged);
		Entity targetEntity = scene.GetEntity(target);

		// Reparenting `dragged` under one of its own descendants would create
		// a cycle; Entity::SetParent already refuses this silently, but we
		// also guard the sibling reorder path so we don't claim success.
		if (draggedEntity.IsAncestorOf(targetEntity)) return false;

		Entity targetParent = targetEntity.GetParent();
		Entity draggedParent = draggedEntity.GetParent();

		if (draggedParent != targetParent) {
			if (!SetEntityParentPreservingWorld(scene, dragged, targetParent)) {
				return false;
			}
		}

		auto repositionVec = [&](std::vector<EntityHandle>& vec) -> bool {
			auto draggedIt = std::find(vec.begin(), vec.end(), dragged);
			if (draggedIt == vec.end()) return false;
			vec.erase(draggedIt);
			auto targetIt = std::find(vec.begin(), vec.end(), target);
			const size_t insertAt = (targetIt == vec.end())
				? vec.size()
				: static_cast<size_t>((targetIt - vec.begin()) + (insertAfter ? 1 : 0));
			vec.insert(vec.begin() + insertAt, dragged);
			return true;
		};

		if (targetParent.IsValid()) {
			auto& childList = scene.GetComponent<HierarchyComponent>(targetParent.GetHandle()).Children;
			repositionVec(childList);
		}
		else {
			// Root-level reorder: mutate m_EntityOrder AND mirror into entt storage so the renderer/serializer (which iterate entt directly) see the new order.
			// entt iterates dense storage newest-first; UIDrawOrder reverses that, so m_EntityOrder.back() must be the "newest" entity in the storage sort.
			repositionVec(m_EntityOrder);
			m_EntityOrderDirty = true; // M30: force re-DFS next render.

			std::unordered_map<entt::entity, std::size_t> indexOf;
			indexOf.reserve(m_EntityOrder.size());
			for (std::size_t i = 0; i < m_EntityOrder.size(); ++i) {
				indexOf[m_EntityOrder[i]] = i;
			}
			const std::size_t fallbackBase = m_EntityOrder.size();
			auto& storage = scene.GetRegistry().storage<entt::entity>();
			storage.sort([&](entt::entity a, entt::entity b) {
				auto ita = indexOf.find(a);
				auto itb = indexOf.find(b);
				const std::size_t ai = (ita != indexOf.end())
					? ita->second
					: fallbackBase + static_cast<std::size_t>(entt::to_integral(a));
				const std::size_t bi = (itb != indexOf.end())
					? itb->second
					: fallbackBase + static_cast<std::size_t>(entt::to_integral(b));
				return ai > bi;
			});
		}

		return true;
	}

	bool ImGuiEditorLayer::OpenPrefabForEditing(const std::string& path) {
		if (path.empty() || !File::Exists(path)) {
			FNK_CORE_WARN_TAG("PrefabEdit", "Prefab file not found: {}", path);
			return false;
		}
		if (m_PrefabEditScene && m_PrefabEditPath == path) {
			return true;
		}

		// Before the read: an edit landing mid-read must still count as a change afterwards.
		KnownFileStamps::Record(path);
		std::unique_ptr<Scene> detached;
		const EntityHandle rootEntity = LoadPrefabSourceIntoDetachedScene(path, "##PrefabEdit", "PrefabEdit", detached);
		if (rootEntity == entt::null) return false;
		TransformHierarchySystem::Propagate(*detached);

		if (m_PrefabEditScene) {
			if ((m_PrefabEditScene->IsDirty() || m_PrefabEditDirty) && !SavePrefabEditChanges()) {
				return false;
			}
			ClosePrefabEditing(false);
		}

		ClearEntitySelection();
		m_EntityOrder.clear(); m_EntityOrderDirty = true;
		m_CollapsedHierarchyEntities.clear();
		m_CutEntities.clear();

		// Save camera state to restore on Close; repoint at prefab root so it's not off-screen (prefabs typically authored at origin).
		m_PrefabEditSavedCameraPosition = m_EditorCamera.GetPosition();
		m_PrefabEditSavedCameraOrthoSize = m_EditorCamera.GetOrthographicSize();
		m_PrefabEditSavedCameraZoom = m_EditorCamera.GetZoom();
		m_PrefabEditHasSavedCameraState = true;

		m_PrefabEditScene = std::move(detached);
		m_PrefabEditPath = path;
		m_PrefabEditRootEntity = rootEntity;
		m_PrefabEditDirty = false;

		if (rootEntity != entt::null && m_PrefabEditScene->IsValid(rootEntity)) {
			SelectEntity(rootEntity);
			Vec2 focusPos{ 0.0f, 0.0f };
			Transform2DComponent* rootTransform = nullptr;
			if (m_PrefabEditScene->TryGetComponent<Transform2DComponent>(rootEntity, rootTransform) && rootTransform) {
				focusPos = rootTransform->Position;
			}
			m_EditorCamera.SetPosition(focusPos);
			// Reset zoom/ortho size to a sane default so a prior heavily-
			// zoomed-in scene-edit framing doesn't make the prefab fill the
			// viewport (or vice-versa).
			m_EditorCamera.SetZoom(1.0f);
			m_EditorCamera.SetOrthographicSize(5.0f);
		}

		FNK_INFO_TAG("PrefabEdit", "Editing {}", path);
		return true;
	}

	ImGuiEditorLayer::LoadedSceneSelectionSnapshot ImGuiEditorLayer::CaptureLoadedSceneSelection() const {
		LoadedSceneSelectionSnapshot snapshot;
		if (IsInPrefabEditMode()) return snapshot;
		Scene* scene = GetContextScene();
		if (!scene) return snapshot;

		snapshot.SceneName = scene->GetName();
		snapshot.SceneNodeSelected = m_IsSceneNodeSelected;
		for (const EntityHandle entity : GetSelectedEntities(*scene)) {
			LoadedSceneSelectionEntity selected;
			selected.PersistentId = scene->GetEntityPersistentID(entity);
			if (scene->GetEntityOrigin(entity) == EntityOrigin::Prefab
				&& scene->HasComponent<PrefabInstanceComponent>(entity)) {
				const EntityHandle root = SceneSerializer::GetPrefabInstanceRoot(*scene, entity);
				if (root != entt::null) {
					selected.PrefabRootPersistentId = scene->GetEntityPersistentID(root);
				}
				selected.PrefabSourceEntityId = scene->GetComponent<PrefabInstanceComponent>(entity).SourceEntityId;
			}
			if (entity == m_SelectedEntity) snapshot.PrimaryIndex = snapshot.Entities.size();
			snapshot.Entities.push_back(selected);
		}
		return snapshot;
	}

	void ImGuiEditorLayer::RestoreLoadedSceneSelection(const LoadedSceneSelectionSnapshot& snapshot) {
		if (snapshot.SceneName.empty()) return;
		const std::shared_ptr<Scene> scene = SceneManager::GetLoadedScene(snapshot.SceneName).lock();
		if (!scene) return;

		ClearEntitySelection();
		m_CutEntities.clear();
		m_LastInteractedSceneName = scene->GetName();
		m_EntityOrder.clear();
		m_EntityOrderSceneId = nullptr;
		m_EntityOrderDirty = true;
		if (snapshot.SceneNodeSelected) {
			SelectSceneNode();
			return;
		}

		std::vector<EntityHandle> resolved;
		resolved.reserve(snapshot.Entities.size());
		for (const LoadedSceneSelectionEntity& selected : snapshot.Entities) {
			EntityHandle entity = entt::null;
			if (selected.PersistentId != 0) {
				scene->TryResolveEntityRef(selected.PersistentId, entity);
			}
			if (entity == entt::null && selected.PrefabRootPersistentId != 0
				&& selected.PrefabSourceEntityId != 0) {
				EntityHandle root = entt::null;
				if (scene->TryResolveEntityRef(selected.PrefabRootPersistentId, root)) {
					for (const EntityHandle candidate : scene->GetRegistry().view<PrefabInstanceComponent>()) {
						const auto& instance = scene->GetComponent<PrefabInstanceComponent>(candidate);
						if (instance.SourceEntityId == selected.PrefabSourceEntityId
							&& SceneSerializer::GetPrefabInstanceRoot(*scene, candidate) == root) {
							entity = candidate;
							break;
						}
					}
				}
			}
			resolved.push_back(entity);
			if (entity != entt::null) m_SelectedEntities.push_back(entity);
		}
		RebuildSelectionSet();
		if (snapshot.PrimaryIndex < resolved.size()) {
			m_SelectedEntity = resolved[snapshot.PrimaryIndex];
		}
		if (m_SelectedEntity == entt::null && !m_SelectedEntities.empty()) {
			m_SelectedEntity = m_SelectedEntities.back();
		}
		if (m_SelectedEntity != entt::null) m_AssetBrowser.ClearSelection();
	}

	bool ImGuiEditorLayer::SavePrefabInspectorChanges() {
		const LoadedSceneSelectionSnapshot selection = CaptureLoadedSceneSelection();
		const bool saved = m_PrefabInspector.Save();
		RestoreLoadedSceneSelection(selection);
		return saved;
	}

	bool ImGuiEditorLayer::ApplyPrefabInstanceToSource(Scene& scene, EntityHandle entity) {
		const EntityHandle root = SceneSerializer::GetPrefabInstanceRoot(scene, entity);
		if (root == entt::null) return false;

		const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(root));
		const std::string prefabPath = AssetRegistry::ResolvePath(prefabGuid);
		if (prefabGuid == 0 || prefabPath.empty()) return false;

		Json::Value source = SceneSerializer::SerializePrefabSource(scene, root, prefabGuid);
		auto stagedScene = Scene::CreateDetachedScene("##PrefabApply");
		const EntityHandle stagedRoot = SceneSerializer::DeserializeEntityFromValue(*stagedScene, source);
		if (stagedRoot == entt::null || !stagedScene->IsValid(stagedRoot)) return false;

		const LoadedSceneSelectionSnapshot selection = CaptureLoadedSceneSelection();
		const bool saved = SavePrefabSourceTransactionally(
			*stagedScene, stagedRoot, prefabPath, "PrefabApply");
		RestoreLoadedSceneSelection(selection);
		return saved;
	}

	bool ImGuiEditorLayer::SavePrefabEditChanges() {
		if (!m_PrefabEditScene) return false;
		if (m_PrefabEditRootEntity == entt::null
			|| !m_PrefabEditScene->IsValid(m_PrefabEditRootEntity)
			|| m_PrefabEditPath.empty()) {
			return false;
		}

		const LoadedSceneSelectionSnapshot selection = CaptureLoadedSceneSelection();
		if (!SavePrefabSourceTransactionally(*m_PrefabEditScene, m_PrefabEditRootEntity,
			m_PrefabEditPath, "PrefabEdit")) {
			RestoreLoadedSceneSelection(selection);
			return false;
		}
		RestoreLoadedSceneSelection(selection);

		// Mark the detached scene clean so the toolbar no longer shows the
		// "*" suffix and the per-frame auto-save tick doesn't loop on the
		// same edits.
		m_PrefabEditScene->ClearDirty();
		m_PrefabEditDirty = false;
		return true;
	}

	bool ImGuiEditorLayer::ClosePrefabEditing(bool save) {
		if (!m_PrefabEditScene) return true;

		if (save && !SavePrefabEditChanges()) {
			return false;
		}
		ForgetUndoHistory(m_PrefabEditScene.get());

		ClearEntitySelection();
		m_EntityOrder.clear(); m_EntityOrderDirty = true;
		m_CollapsedHierarchyEntities.clear();
		// Drop any cut markers — they refer to entities in the detached
		// prefab scene we're about to discard, and a leftover marker
		// would otherwise dim a freshly-recycled handle in the next scene.
		m_CutEntities.clear();
		m_PrefabEditRootEntity = entt::null;
		m_PrefabEditPath.clear();
		m_PrefabEditDirty = false;
		m_PrefabEditConflict = false;
		// unique_ptr destruction tears down the registry; destroy hooks
		// gated by Scene::IsDetached() short-circuit so global physics/
		// audio/script subsystems aren't touched.
		m_PrefabEditScene.reset();

		// Position MUST come last: SetOrthographicSize/SetZoom recompute projection only; position rebuilds the view matrix.
		if (m_PrefabEditHasSavedCameraState) {
			m_EditorCamera.SetOrthographicSize(m_PrefabEditSavedCameraOrthoSize);
			m_EditorCamera.SetZoom(m_PrefabEditSavedCameraZoom);
			m_EditorCamera.SetPosition(m_PrefabEditSavedCameraPosition);
			m_PrefabEditHasSavedCameraState = false;
		}
		return true;
	}

	Scene* ImGuiEditorLayer::GetContextScene() const {
		if (m_PrefabEditScene) return m_PrefabEditScene.get();
		// Last-interacted scene wins so Create Entity lands where expected with multi-scene.
		if (!m_LastInteractedSceneName.empty()) {
			if (auto weak = SceneManager::GetLoadedScene(m_LastInteractedSceneName);
				auto shared = weak.lock()) {
				return shared.get();
			}
		}
		return SceneManager::GetActiveScene();
	}

	bool ImGuiEditorLayer::HasEntityShortcutFocus() const {
		if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) return false;
		if ((m_ShowAnimationWindow && m_AnimationWindow.IsFocused()) || (m_ShowAnimatorWindow && m_AnimatorWindow.IsFocused())) return false;
		return m_IsEditorViewFocused || m_IsEntitiesPanelFocused || m_IsInspectorPanelFocused;
	}



	// ──────────────────────────────────────────────
	//  Entities Panel (with rename support)
	// ──────────────────────────────────────────────

	void ImGuiEditorLayer::RenderEntitiesPanel() {
		FUNKE_PROFILE_SCOPE("Hierarchy Panel");
		ImGui::Begin("Entities");

		// Scale the whole panel by the user's row-size preference. FontSizeBase already
		// folds in the global font zoom + DPI, so this composes; icons/arrows/row heights
		// all derive from the text line height and grow together. PopFont before End().
		// Capture the unscaled base first: PushFont overwrites style.FontSizeBase, so right-click
		// popups reset to this captured value (via ScopedPopupFontReset) to stay normal-sized.
		const float hierarchyRowScale = EditorPreferences::GetHierarchyRowScale();
		const bool scaleHierarchyFont = hierarchyRowScale > 1.001f || hierarchyRowScale < 0.999f;
		const float hierarchyBaseFontSize = ImGui::GetStyle().FontSizeBase;
		if (scaleHierarchyFont) {
			ImGui::PushFont(nullptr, hierarchyBaseFontSize * hierarchyRowScale);
		}

		if (IsInPrefabEditMode()) {
			// Mirror IsDirty onto m_PrefabEditDirty so callers (e.g. breadcrumb) read the canonical dirty state.
			m_PrefabEditDirty = m_PrefabEditScene->IsDirty();

			const std::string prefabName = std::filesystem::path(m_PrefabEditPath).stem().string();
			const bool showDirtyMark = m_PrefabEditDirty && !EditorPreferences::GetAutoSavePrefabs();
			const std::string title = showDirtyMark ? (prefabName + " *") : prefabName;

			const float backButtonStartX = ImGui::GetCursorPosX();
			if (EditorUI::IconButton("##PrefabBack", "arrow_left", "Back to scene")) {
				if (m_PrefabEditDirty) {
					m_ShowPrefabEditDiscardPrompt = true;
				}
				else {
					ClosePrefabEditing(false);
				}
			}

			const float rowEndX = backButtonStartX + ImGui::GetContentRegionAvail().x;
			const float textWidth = ImGui::CalcTextSize(title.c_str()).x;
			const float centerX = backButtonStartX + (rowEndX - backButtonStartX - textWidth) * 0.5f;
			ImGui::SameLine();
			// Guard against very narrow panels where the back button would
			// overlap the centered text; fall back to a normal SameLine gap.
			const float minCenterX = ImGui::GetCursorPosX();
			ImGui::SetCursorPosX(centerX > minCenterX ? centerX : minCenterX);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(title.c_str());
			ImGui::Separator();
		}

		// Hierarchy search bar: a non-empty query filters the entity rows below
		// to name matches (applied by the per-scene filter pass before rendering).
		const float addButtonW = ImGui::GetFrameHeight();
		if (EditorUI::SearchField("##HierarchySearch", m_HierarchySearchBuffer, sizeof(m_HierarchySearchBuffer),
			"Search entities...", ImGui::GetContentRegionAvail().x - addButtonW - ImGui::GetStyle().ItemSpacing.x)) {
			// Clearing the query must restore the unfiltered (collapse-pruned)
			// list, which only the rebuild produces — so force one this frame.
			m_EntityOrderDirty = true;
			// The query redefines what m_VisibleEntityOrder *is*, so a stored
			// range-select anchor (an index into it) is now meaningless — drop
			// it so the next shift-click re-derives the anchor from m_SelectedEntity.
			m_LastEntitySelectionIndex = -1;
		}
		ImGui::SameLine();
		if (EditorUI::IconButton("##HierarchyAdd", "plus", "Create entity")) {
			ImGui::OpenPopup("##HierarchyAddMenu");
		}
		if (ImGui::BeginPopup("##HierarchyAddMenu")) {
			if (scaleHierarchyFont) ImGui::PushFont(nullptr, hierarchyBaseFontSize);
			if (Scene* target = GetContextScene()) {
				RenderCreateEntityMenu(*target, Entity::Null);
			}
			if (scaleHierarchyFont) ImGui::PopFont();
			ImGui::EndPopup();
		}
		ImGui::Separator();

		// Pin the header above (prefab breadcrumb + search bar) and scroll only the
		// entity list: everything below lives in its own child so the search bar
		// stays visible no matter how far the hierarchy is scrolled.
		ImGui::BeginChild("##HierarchyScroll", ImVec2(0.0f, 0.0f),
			ImGuiChildFlags_None, ImGuiWindowFlags_None);

		Scene* activeScene = GetContextScene();

		// Right-click on empty space: create entities in the active scene
		if (activeScene && ImGui::BeginPopupContextWindow("EntityCreateContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			// Right-click menus stay at the normal size even when the rows are drawn scaled
			// (the scaled font otherwise leaks into the popup); pop before EndPopup so the
			// popup window's font stack stays balanced.
			if (scaleHierarchyFont) ImGui::PushFont(nullptr, hierarchyBaseFontSize);
			Scene& scene = *activeScene;
			RenderCreateEntityMenu(scene, Entity::Null);
			if (scaleHierarchyFont) ImGui::PopFont();
			ImGui::EndPopup();
		}

		std::vector<Scene*> scenesToShow;
		if (IsInPrefabEditMode()) {
			scenesToShow.push_back(m_PrefabEditScene.get());
		}
		else {
			auto loadedScenes = SceneManager::GetLoadedScenes();
			scenesToShow.reserve(loadedScenes.size());
			for (auto& weakScene : loadedScenes) {
				if (auto scenePtr = weakScene.lock()) {
					scenesToShow.push_back(scenePtr.get());
				}
			}
		}
		std::string sceneToRemove;
		struct PendingSceneReorder {
			std::string SourceName;
			size_t TargetIndex;
		};
		std::optional<PendingSceneReorder> pendingSceneReorder;

		for (size_t sceneIdx = 0; sceneIdx < scenesToShow.size(); ++sceneIdx) {
			Scene* scenePtrRaw = scenesToShow[sceneIdx];
			if (!scenePtrRaw) continue;
			Scene& scene = *scenePtrRaw;

			const uint64_t sceneIdValue = static_cast<uint64_t>(scene.GetSceneId());
			// Scene* (not sceneId) for the ImGui scope: two scenes can collide on
			// sceneId — duplicated .scene files keep their serialized id, and an
			// additively loaded script-only scene starts at the UUID default.
			// Scene* is always distinct among currently-loaded scenes.
			ImGui::PushID(scenePtrRaw);

			// Prefab mode: skip scene header; drop target would create a second root in the single-rooted prefab.
			bool sceneOpen = true;
			if (!IsInPrefabEditMode()) {
				ImGuiTreeNodeFlags sceneFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow
					| ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed;
				const std::string fullSceneLabel = scene.IsDirty() ? scene.GetName() + " *" : scene.GetName();
				const std::string sceneLabel = ImGuiUtils::Ellipsize(fullSceneLabel, ImGui::GetContentRegionAvail().x);
				// Use stable str_id (not the label) so the fold state survives the
				// dirty-asterisk toggle that flips on save. Label is supplied via fmt.
				// Neutral frame: the accent Header color is reserved for entity selection.
				ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
				ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
				ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
				sceneOpen = ImGui::TreeNodeEx("##scene_node", sceneFlags, "%s", sceneLabel.c_str());
				ImGui::PopStyleColor(3);
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
					const size_t entityCount = scene.GetEntityCount();
					ImGui::SetTooltip("%s\n%zu %s", fullSceneLabel.c_str(), entityCount, entityCount == 1 ? "entity" : "entities");
				}
				// Capture immediately after TreeNodeEx; subsequent calls would shift the "last item" rect.
				const ImVec2 sceneItemRectMin = ImGui::GetItemRectMin();
				const ImVec2 sceneItemRectMax = ImGui::GetItemRectMax();
				if (ImGui::IsItemActivated()) {
					m_LastInteractedSceneName = scene.GetName();
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
					SelectSceneNode();
				}

				if (scenesToShow.size() > 1 && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
					HierarchySceneDragData sceneDrag{ sceneIdValue };
					ImGui::SetDragDropPayload("HIERARCHY_SCENE", &sceneDrag, sizeof(sceneDrag));
					ImGui::Text("Move scene: %s", scene.GetName().c_str());
					ImGui::EndDragDropSource();
				}

				if (ImGui::BeginDragDropTarget()) {
					const float mouseY = ImGui::GetMousePos().y;
					const float headerMidY = (sceneItemRectMin.y + sceneItemRectMax.y) * 0.5f;
					const bool sceneInTopZone = mouseY < headerMidY;
					if (const ImGuiPayload* peek = ImGui::GetDragDropPayload()) {
						if (peek->IsDataType("HIERARCHY_SCENE")) {
							const float lineY = sceneInTopZone ? sceneItemRectMin.y : sceneItemRectMax.y;
							const ImU32 color = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
							ImGui::GetWindowDrawList()->AddLine(
								ImVec2(sceneItemRectMin.x, lineY),
								ImVec2(sceneItemRectMax.x, lineY),
								color, 2.0f);
						}
					}

					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_SCENE")) {
						auto* dragData = static_cast<const HierarchySceneDragData*>(payload->Data);
						if (dragData->SceneId != sceneIdValue) {
							size_t sourceIdx = SIZE_MAX;
							std::string sourceName;
							for (size_t i = 0; i < scenesToShow.size(); ++i) {
								Scene* candidate = scenesToShow[i];
								if (candidate && static_cast<uint64_t>(candidate->GetSceneId()) == dragData->SceneId) {
									sourceIdx = i;
									sourceName = candidate->GetName();
									break;
								}
							}
							if (sourceIdx != SIZE_MAX) {
								// Adjust for the removal shift: when source < target, removing source shifts target index down by 1.
								size_t finalIdx;
								if (sceneInTopZone) {
									finalIdx = (sourceIdx < sceneIdx) ? (sceneIdx - 1) : sceneIdx;
								}
								else {
									finalIdx = (sourceIdx < sceneIdx) ? sceneIdx : (sceneIdx + 1);
								}
								pendingSceneReorder = PendingSceneReorder{ sourceName, finalIdx };
							}
						}
					}

					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
						auto* dragData = static_cast<const HierarchyDragData*>(payload->Data);
						EntityHandle primaryDraggedHandle = static_cast<EntityHandle>(dragData->EntityHandle);
						const bool sameScene = dragData->SourceSceneId == sceneIdValue;
						if (sameScene) {
							std::vector<EntityHandle> draggedHandles = ResolveDraggedHierarchyEntities(scene, primaryDraggedHandle);
							StructureEditRecorder recorder(scene, draggedHandles, "Reparent");
							bool didMove = false;
							for (EntityHandle h : draggedHandles) {
								Entity draggedEntity = scene.GetEntity(h);
								if (!draggedEntity.HasParent()) continue;
								if (SetEntityParentPreservingWorld(scene, h, Entity::Null)) {
									didMove = true;
								}
							}
							if (didMove) {
								// Force a re-DFS — reparenting changes nesting but
								// not the registry count, so the size-mismatch
								// fallback won't trigger.
								m_EntityOrderDirty = true;
								scene.MarkDirty();
								PushStructureUndo(scene, recorder.Finish(scene, draggedHandles));
							}
						}
						else {
							// Cross-scene drag: locate the source scene by
							// the SceneId carried in the payload, resolve
							// the multi-selection against it, then migrate.
							Scene* sourceScene = nullptr;
							for (Scene* candidate : scenesToShow) {
								if (candidate && static_cast<uint64_t>(candidate->GetSceneId()) == dragData->SourceSceneId) {
									sourceScene = candidate;
									break;
								}
							}
							if (sourceScene && sourceScene != &scene) {
								std::vector<EntityHandle> draggedHandles = ResolveDraggedHierarchyEntities(*sourceScene, primaryDraggedHandle);
								std::vector<EntityHandle> migrated = MigrateEntitiesToScene(*sourceScene, scene, draggedHandles, Entity::Null);
								if (!migrated.empty()) {
									// Selection is keyed by raw entt handle; clear source-scene selection so inspector shows the migrated entities.
									ClearEntitySelection();
									m_SelectedEntities = migrated;
									RebuildSelectionSet();
									m_SelectedEntity = migrated.back();
									m_LastInteractedSceneName = scene.GetName();
									m_EntityOrderDirty = true;
								}
							}
						}
					}
					ImGui::EndDragDropTarget();
				}

				// Right-click context menu on scene tree node. "Remove"
				// only makes sense with more than one scene loaded.
				if (ImGui::BeginPopupContextItem()) {
					if (scaleHierarchyFont) ImGui::PushFont(nullptr, hierarchyBaseFontSize); // popup at normal size; pop before EndPopup
					const bool canRemove = scenesToShow.size() > 1;
					if (canRemove) {
						if (ImGui::MenuItem("Remove")) {
							sceneToRemove = scene.GetName();
						}
					} else {
						ImGui::MenuItem("Remove", nullptr, false, false);
					}
					// Manual reorder fallback for users without drag-and-drop
					// muscle memory (and a keyboard-only accessibility path).
					// Disabled at the boundaries.
					ImGui::Separator();
					const bool canMoveUp = sceneIdx > 0;
					const bool canMoveDown = sceneIdx + 1 < scenesToShow.size();
					if (ImGui::MenuItem("Move Up", nullptr, false, canMoveUp)) {
						pendingSceneReorder = PendingSceneReorder{ scene.GetName(), sceneIdx - 1 };
					}
					if (ImGui::MenuItem("Move Down", nullptr, false, canMoveDown)) {
						pendingSceneReorder = PendingSceneReorder{ scene.GetName(), sceneIdx + 1 };
					}
					if (scaleHierarchyFont) ImGui::PopFont();
					ImGui::EndPopup();
				}
			}

			if (sceneOpen) {
				auto view = scene.GetRegistry().view<entt::entity>();

				// Rebuild only when dirty or registry size changed; view.size() is O(1) (swap_only policy).
				const auto registryCount = static_cast<std::size_t>(view.size());
				// Force rebuild on scene-id change: same entity count from a different scene leaves m_VisibleEntityOrder pointing at destroyed handles.
				if (m_EntityOrderSceneId != scenePtrRaw) {
					m_EntityOrderDirty = true;
					m_EntityOrderSceneId = scenePtrRaw;
				}
				// Catch reparents that don't change the entity count (e.g. a script calling Entity.SetParent at runtime): the
				// cached row depths would otherwise keep a detached child nested under its old parent, with the parent's fold arrow gone.
				const uint64_t hierarchyVersion = scene.GetHierarchyVersion();
				if (hierarchyVersion != m_LastHierarchyVersion) {
					m_EntityOrderDirty = true;
					m_LastHierarchyVersion = hierarchyVersion;
				}
				const bool needsRebuild = m_EntityOrderDirty
					|| m_EntityOrder.size() != registryCount;

				if (needsRebuild) {
					FUNKE_PROFILE_SCOPE("Editor.HierarchyRebuild");
					// Set-diff sync: never blanket-reset (drag-reorder lives here); new entities appended at end (walk entt view backwards = oldest-first).
					{
						std::vector<entt::entity> viewEntities(view.begin(), view.end());

						std::unordered_set<uint32_t> viewSet;
						viewSet.reserve(viewEntities.size());
						for (auto e : viewEntities) viewSet.insert(static_cast<uint32_t>(e));
						m_EntityOrder.erase(
							std::remove_if(m_EntityOrder.begin(), m_EntityOrder.end(),
								[&](entt::entity e) { return viewSet.find(static_cast<uint32_t>(e)) == viewSet.end(); }),
							m_EntityOrder.end());

						std::unordered_set<uint32_t> orderSet;
						orderSet.reserve(m_EntityOrder.size());
						for (auto e : m_EntityOrder) orderSet.insert(static_cast<uint32_t>(e));
						for (auto it = viewEntities.rbegin(); it != viewEntities.rend(); ++it) {
							if (orderSet.find(static_cast<uint32_t>(*it)) == orderSet.end()) {
								m_EntityOrder.push_back(*it);
							}
						}
					}

					// DFS pass: emit each child immediately after its parent; also builds m_VisibleEntityOrder (pruned to exclude collapsed subtrees).
					m_RenderedEntityDepths.clear();
					m_RenderedEntityDepths.reserve(m_EntityOrder.size());
					std::vector<entt::entity> visibleReordered;
					std::vector<int> visibleDepths;
					visibleReordered.reserve(m_EntityOrder.size());
					visibleDepths.reserve(m_EntityOrder.size());
					{
						auto& reg = scene.GetRegistry();
						std::vector<entt::entity> reordered;
						reordered.reserve(m_EntityOrder.size());
						std::unordered_set<uint32_t> emitted;

						std::function<void(EntityHandle, int, bool)> emitSubtree =
							[&](EntityHandle e, int depth, bool insideCollapsed) {
								if (!reg.valid(e)) return;
								const uint32_t key = static_cast<uint32_t>(e);
								if (!emitted.insert(key).second) return; // already in tree
								reordered.push_back(e);
								m_RenderedEntityDepths.push_back(depth);
								if (!insideCollapsed) {
									visibleReordered.push_back(e);
									visibleDepths.push_back(depth);
								}
								const bool descendInsideCollapsed = insideCollapsed
									|| m_CollapsedHierarchyEntities.contains(key);
								if (reg.all_of<HierarchyComponent>(e)) {
									for (EntityHandle child : reg.get<HierarchyComponent>(e).Children) {
										emitSubtree(child, depth + 1, descendInsideCollapsed);
									}
								}
							};

						// Pass 1: emit each root-of-subtree from the user-ordered
						// list. Skip entities that already appeared as a child.
						for (EntityHandle e : m_EntityOrder) {
							if (!reg.valid(e)) continue;
							EntityHandle parent = entt::null;
							if (reg.all_of<HierarchyComponent>(e)) {
								parent = reg.get<HierarchyComponent>(e).Parent;
							}
							if (parent == entt::null) {
								emitSubtree(e, 0, /*insideCollapsed*/ false);
							}
						}
						// Pass 2: anything still missing — orphaned children
						// whose parent is not in the registry — render flat.
						for (EntityHandle e : m_EntityOrder) {
							if (!reg.valid(e)) continue;
							const uint32_t key = static_cast<uint32_t>(e);
							if (emitted.contains(key)) continue;
							emitSubtree(e, 0, /*insideCollapsed*/ false);
						}
						m_EntityOrder = std::move(reordered);
					}

					m_RenderedEntityOrder = m_EntityOrder;
					m_VisibleEntityOrder = std::move(visibleReordered);
					m_VisibleEntityDepths = std::move(visibleDepths);
					m_EntityOrderDirty = false;
				}

				// Hierarchy search: a non-empty query flattens the view to the
				// entities whose name matches (case-insensitive substring), drawn
				// at depth 0 with no fold arrows. Filtered from m_EntityOrder (the
				// full DFS list) so matches inside collapsed parents still surface.
				// Overwriting the visible lists routes the clipper, range-select,
				// and arrow-key nav through the filtered set automatically.
				const bool hierarchySearchActive = m_HierarchySearchBuffer[0] != '\0';
				if (hierarchySearchActive) {
					// ToLowerCopy casts to unsigned char first — raw ::tolower on a
					// signed char is UB for the >=0x80 bytes of UTF-8 entity names.
					const std::string needle = ToLowerCopy(m_HierarchySearchBuffer);

					std::vector<entt::entity> filtered;
					std::vector<int> filteredDepths;
					filtered.reserve(m_EntityOrder.size());
					filteredDepths.reserve(m_EntityOrder.size());
					for (EntityHandle e : m_EntityOrder) {
						if (!scene.IsValid(e)) continue;
						const std::string name = ToLowerCopy(scene.GetEntity(e).GetName());
						if (name.find(needle) != std::string::npos) {
							filtered.push_back(e);
							filteredDepths.push_back(0);
						}
					}
					m_VisibleEntityOrder = std::move(filtered);
					m_VisibleEntityDepths = std::move(filteredDepths);

					if (m_VisibleEntityOrder.empty()) {
						EditorUI::EmptyState("No matching entities");
					}
				}

				const ImVec2 defaultItemSpacing = ImGui::GetStyle().ItemSpacing;
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(defaultItemSpacing.x, 1.0f));

				// Tree guides, precomputed with one reverse pass: whether each row is the last child of its
				// parent, and which ancestor levels still have siblings further down (their line continues).
				std::vector<uint64_t> guideContinueMask(m_VisibleEntityDepths.size(), 0);
				std::vector<bool> guideIsLastChild(m_VisibleEntityDepths.size(), true);
				{
					std::vector<bool> siblingBelow;
					for (std::size_t i = m_VisibleEntityDepths.size(); i-- > 0;) {
						const int d = std::clamp(m_VisibleEntityDepths[i], 0, 63);
						if (siblingBelow.size() <= static_cast<std::size_t>(d)) siblingBelow.resize(d + 1, false);
						guideIsLastChild[i] = !siblingBelow[d];
						for (int k = 1; k < d; ++k) {
							if (siblingBelow[k]) guideContinueMask[i] |= (1ull << k);
						}
						siblingBelow[d] = true;
						std::fill(siblingBelow.begin() + d + 1, siblingBelow.end(), false);
					}
				}

				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(m_VisibleEntityOrder.size()));
					// Gate selection on scene identity: the same entt handle value can exist in multiple loaded scenes.
					Scene* const selectionScene = GetContextScene();
				while (clipper.Step()) {
				for (int entityIdx = clipper.DisplayStart; entityIdx < clipper.DisplayEnd; ++entityIdx) {
					const EntityHandle entityHandle = m_VisibleEntityOrder[static_cast<std::size_t>(entityIdx)];
					if (!scene.IsValid(entityHandle)) continue;
					Entity entity = scene.GetEntity(entityHandle);
					const bool selected = IsEntitySelected(entityHandle) && &scene == selectionScene;

					const int targetDepth = entityIdx < static_cast<int>(m_VisibleEntityDepths.size())
						? m_VisibleEntityDepths[static_cast<std::size_t>(entityIdx)] : 0;

					const int visibleIndex = entityIdx;

					const float rowIndent = static_cast<float>(targetDepth) * 14.0f;

					// Row highlight is painted before the guides, fold arrow and icon so it always sits behind them.
					{
						const ImVec2 rowPos = ImGui::GetCursorScreenPos();
						const ImVec2 rMin(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x * 0.5f, rowPos.y);
						const ImVec2 rMax(rowPos.x + ImGui::GetContentRegionAvail().x, rowPos.y + ImGui::GetTextLineHeight());
						const bool rowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
							&& ImGui::IsMouseHoveringRect(rMin, rMax);
						if (selected || rowHovered) {
							const bool pressed = rowHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
							const ImU32 col = (selected || pressed)
								? ImGui::GetColorU32(EditorTheme::Colors::RowSelection(pressed))
								: ImGui::GetColorU32(ImGuiCol_HeaderHovered);
							ImDrawList* rowDraw = ImGui::GetWindowDrawList();
							rowDraw->AddRectFilled(rMin, rMax, col, ImGui::GetStyle().FrameRounding);
							if (selected) {
								const float inset = (rMax.y - rMin.y) * 0.2f;
								rowDraw->AddRectFilled(ImVec2(rMin.x, rMin.y + inset), ImVec2(rMin.x + 3.0f, rMax.y - inset),
									ImGui::GetColorU32(EditorTheme::Colors::Accent()), 1.5f);
							}
						}
					}

					if (targetDepth > 0 && entityIdx < static_cast<int>(guideContinueMask.size())) {
						const ImVec2 rowPos = ImGui::GetCursorScreenPos();
						const float lineH = ImGui::GetTextLineHeight();
						const float half = lineH * 0.5f;
						const float top = rowPos.y - ImGui::GetStyle().ItemSpacing.y;
						const float midY = std::floor(rowPos.y + half) + 0.5f;
						const ImU32 guideCol = ImGui::GetColorU32(ImGuiCol_Text, 0.35f);
						ImDrawList* guideDraw = ImGui::GetWindowDrawList();
						// Line sits under the parent's fold arrow at each level.
						auto levelX = [&](int level) { return std::floor(rowPos.x + (level - 1) * 14.0f + half) + 0.5f; };

						const uint64_t mask = guideContinueMask[static_cast<std::size_t>(entityIdx)];
						for (int k = 1; k < targetDepth && k < 64; ++k) {
							if (mask & (1ull << k)) {
								guideDraw->AddLine(ImVec2(levelX(k), top), ImVec2(levelX(k), rowPos.y + lineH), guideCol);
							}
						}
						const float x = levelX(targetDepth);
						const bool isLast = guideIsLastChild[static_cast<std::size_t>(entityIdx)];
						guideDraw->AddLine(ImVec2(x, top), ImVec2(x, isLast ? midY : rowPos.y + lineH), guideCol);
						const bool rowHasChildren = scene.HasComponent<HierarchyComponent>(entityHandle)
							&& !scene.GetComponent<HierarchyComponent>(entityHandle).Children.empty();
						const float elbowEnd = rowPos.x + rowIndent + (rowHasChildren ? 1.0f : lineH * 0.8f);
						guideDraw->AddLine(ImVec2(x, midY), ImVec2(elbowEnd, midY), guideCol);
					}

					if (rowIndent > 0.0f) ImGui::Indent(rowIndent);

					ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entityHandle)));

					const bool hasChildren = scene.HasComponent<HierarchyComponent>(entityHandle)
						&& !scene.GetComponent<HierarchyComponent>(entityHandle).Children.empty();
					bool isCollapsed = m_CollapsedHierarchyEntities.contains(static_cast<uint32_t>(entityHandle));
					{
						const float arrowSize = ImGui::GetTextLineHeight();
						if (hasChildren && !hierarchySearchActive) {
							const ImVec2 arrowTopLeft = ImGui::GetCursorScreenPos();
							if (ImGui::InvisibleButton("##fold", ImVec2(arrowSize, arrowSize))) {
								if (isCollapsed) {
									m_CollapsedHierarchyEntities.erase(static_cast<uint32_t>(entityHandle));
								}
								else {
									m_CollapsedHierarchyEntities.insert(static_cast<uint32_t>(entityHandle));
								}
								isCollapsed = !isCollapsed;
								// Collapse state feeds into the visible-list
								// pruning done by the DFS rebuild, so any
								// toggle has to trigger a rebuild next frame.
								m_EntityOrderDirty = true;
							}
							const ImU32 arrowColor = ImGui::GetColorU32(
								ImGui::IsItemHovered() ? EditorTheme::Colors::Highlight() : ImGui::GetStyleColorVec4(ImGuiCol_Text));
							constexpr float k_ArrowScale = 0.7f;
							const ImVec2 arrowPos(arrowTopLeft.x,
								arrowTopLeft.y + (arrowSize - ImGui::GetFontSize() * k_ArrowScale) * 0.5f);
							ImGui::RenderArrow(ImGui::GetWindowDrawList(), arrowPos,
								arrowColor,
								isCollapsed ? ImGuiDir_Right : ImGuiDir_Down,
								k_ArrowScale);
						}
						else {
							ImGui::Dummy(ImVec2(arrowSize, arrowSize));
						}
						ImGui::SameLine(0.0f, 2.0f);
					}

					bool entityIsDisabled = scene.HasComponent<DisabledTag>(entityHandle);
					if (entityIsDisabled)
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

					// Prefab origin drives both the type icon and the row tint below.
					const bool entityIsPrefab = !Application::GetIsPlaying() && scene.GetEntityOrigin(entityHandle) == EntityOrigin::Prefab;
					bool entityPrefabResolvable = false;
					if (entityIsPrefab) {
						const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(entityHandle));
						entityPrefabResolvable = prefabGuid != 0 && !AssetRegistry::ResolvePath(prefabGuid).empty();
					}

					bool entityIsPrefabTinted = false;
					if (!entityIsDisabled && entityIsPrefab) {
						ImGui::PushStyleColor(ImGuiCol_Text,
							entityPrefabResolvable ? EditorTheme::Colors::PrefabInstance() : EditorTheme::Colors::PrefabOrphan());
						entityIsPrefabTinted = true;
					}

					bool entityIsCutMarked = m_CutEntities.contains(static_cast<uint32_t>(entityHandle));
					if (entityIsCutMarked)
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

					// Type icon at the left of every row: prefab / prefab_missing for prefab instances
					// (by whether the source asset resolves), otherwise the generic entity icon.
					{
						const char* entityIconName = entityIsPrefab
							? (entityPrefabResolvable ? "prefab" : "prefab_missing")
							: EntityTypeIconName(scene, entityHandle);
						const float entityIconSize = ImGui::GetTextLineHeight();
						const uint64_t entityIcon = EditorIcons::Get(entityIconName, static_cast<int>(entityIconSize));
						if (entityIcon) {
							// Prefab and component glyphs carry their own color; the plain entity glyph follows the theme text color.
							const ImVec4 entityIconTint = std::string_view(entityIconName) == "entity"
								? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImVec4(1, 1, 1, 1);
							ImGui::ImageWithBg(static_cast<ImTextureID>(static_cast<intptr_t>(entityIcon)),
								ImVec2(entityIconSize, entityIconSize), ImVec2(0, 1), ImVec2(1, 0),
								ImVec4(0, 0, 0, 0), entityIconTint);
							ImGui::SameLine(0.0f, 4.0f);
						}
					}

					if (m_RenamingEntity == entityHandle) {
						m_EntityRenameFrameCounter++;

						ImGui::PushItemWidth(-1);
						if (m_EntityRenameFrameCounter == 1) {
							ImGui::SetKeyboardFocusHere();
						}

						// Inline edit: row-height field with an accent outline so it sits inside the highlighted row.
						const ImVec4 renameBg = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
						ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x * 0.5f, 0.0f));
						ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
						ImGui::PushStyleColor(ImGuiCol_FrameBg, renameBg);
						ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, renameBg);
						ImGui::PushStyleColor(ImGuiCol_FrameBgActive, renameBg);
						ImGui::PushStyleColor(ImGuiCol_Border, EditorTheme::Colors::Accent());
						bool committed = ImGui::InputText("##EntityRename", m_EntityRenameBuffer, sizeof(m_EntityRenameBuffer),
							ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
						ImGui::PopStyleColor(4);
						ImGui::PopStyleVar(2);

						const bool renameFinished = committed
							|| (!ImGui::IsKeyPressed(ImGuiKey_Escape) && m_EntityRenameFrameCounter > 2 && !ImGui::IsItemActive());
						std::optional<StructureEditRecorder> renameRecorder;
						if (renameFinished && entity.GetName() != std::string_view(m_EntityRenameBuffer)) {
							renameRecorder.emplace(scene, std::span(&entityHandle, 1), "Rename");
						}

						if (committed) {
							std::string newName(m_EntityRenameBuffer);
							if (newName.empty()) {
								if (entity.HasComponent<NameComponent>()) {
									entity.RemoveComponent<NameComponent>();
									scene.MarkDirty();
								}
							}
							else if (entity.HasComponent<NameComponent>()) {
								entity.GetComponent<NameComponent>().Name = newName;
								scene.MarkDirty();
							}
							else {
								scene.AddComponent<NameComponent>(entity.GetHandle(), newName);
								scene.MarkDirty();
							}
							m_RenamingEntity = entt::null;
							m_EntityRenameFrameCounter = 0;
							++m_SelectionVersion; // Bump so inspector re-reads FirstName; stale cache keeps showing pre-rename value.
						}
						else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
							m_RenamingEntity = entt::null;
							m_EntityRenameFrameCounter = 0;
						}
						else if (m_EntityRenameFrameCounter > 2 && !ImGui::IsItemActive()) {
							std::string newName(m_EntityRenameBuffer);
							if (newName.empty()) {
								if (entity.HasComponent<NameComponent>()) {
									entity.RemoveComponent<NameComponent>();
									scene.MarkDirty();
								}
							}
							else if (entity.HasComponent<NameComponent>()) {
								entity.GetComponent<NameComponent>().Name = newName;
								scene.MarkDirty();
							}
							else {
								scene.AddComponent<NameComponent>(entity.GetHandle(), newName);
								scene.MarkDirty();
							}
							m_RenamingEntity = entt::null;
							m_EntityRenameFrameCounter = 0;
							// Same cache-invalidation reason as the committed branch above.
							++m_SelectionVersion;
						}
						if (renameRecorder) PushStructureUndo(scene, renameRecorder->Finish(scene, std::span(&entityHandle, 1)));

						ImGui::PopItemWidth();
					}
					else {
						bool entityLabelTruncated = false;
						const std::string entityLabel = ImGuiUtils::Ellipsize(entity.GetName(), ImGui::GetContentRegionAvail().x, &entityLabelTruncated);
						// Selectable's own fill is suppressed; the full-row highlight was painted at the start of the row.
						ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32(0, 0, 0, 0));
						ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
						ImGui::PushStyleColor(ImGuiCol_HeaderActive,  IM_COL32(0, 0, 0, 0));
						ImGui::Selectable(entityLabel.c_str(), selected);
						ImGui::PopStyleColor(3);
						if (entityLabelTruncated && ImGui::IsItemHovered()) {
							ImGui::SetTooltip("%s", entity.GetName().c_str());
						}
						if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
							m_PressedEntity = entityHandle;
						}
						if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
							if (ImGui::IsItemHovered() && m_PressedEntity == entityHandle && !IsLeftMouseDragPastClickThreshold()) {
								const ImGuiIO& io = ImGui::GetIO();
								if (io.KeyShift) {
									SelectEntityRange(visibleIndex);
								}
								else if (io.KeyCtrl) {
									ToggleEntitySelection(entityHandle, visibleIndex);
								}
								else {
									SetSingleEntitySelection(entityHandle, visibleIndex);
								}
								// Record the owning scene so a follow-up
								// "Create Entity" lands in the same scene the
								// user is selecting from.
								if (!IsInPrefabEditMode()) {
									m_LastInteractedSceneName = scene.GetName();
								}
							}
							if (m_PressedEntity == entityHandle) {
								m_PressedEntity = entt::null;
							}
						}

						if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
							m_RenamingEntity = entityHandle;
							m_EntityRenameFrameCounter = 0;
							std::snprintf(m_EntityRenameBuffer, sizeof(m_EntityRenameBuffer), "%s", entity.GetName().c_str());
						}
					}

					// SourceSceneId distinguishes same-scene reorder from cross-scene migration at the drop site.
					if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
						HierarchyDragData dragData{
							entityIdx,
							static_cast<uint32_t>(entityHandle),
							static_cast<uint64_t>(scene.GetSceneId()),
						};
						ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &dragData, sizeof(dragData));
						const bool draggingMulti = IsEntitySelected(entityHandle) && &scene == selectionScene && m_SelectedEntities.size() > 1;
						if (draggingMulti) {
							ImGui::Text("Move: %zu entities", m_SelectedEntities.size());
						} else {
							ImGui::Text("Move: %s", entity.GetName().c_str());
						}
						ImGui::EndDragDropSource();
					}

					const ImVec2 itemRectMin = ImGui::GetItemRectMin();
					const ImVec2 itemRectMax = ImGui::GetItemRectMax();
					const float itemHeight = itemRectMax.y - itemRectMin.y;
					const float zoneHeight = itemHeight * 0.25f;

					// Suppress sibling zones on prefab root: the gate refuses them anyway but without suppression the indicator still flashes.
					const bool suppressSiblingZones = IsInPrefabEditMode()
						&& entityHandle == m_PrefabEditRootEntity;

					if (ImGui::BeginDragDropTarget()) {
						const float mouseY = ImGui::GetMousePos().y;
						const bool inTopZone = !suppressSiblingZones && mouseY < itemRectMin.y + zoneHeight;
						const bool inBottomZone = !suppressSiblingZones && mouseY > itemRectMax.y - zoneHeight;

						if (const ImGuiPayload* peek = ImGui::GetDragDropPayload()) {
							if (peek->IsDataType("HIERARCHY_ENTITY") && (inTopZone || inBottomZone)) {
								const float lineY = inTopZone ? itemRectMin.y : itemRectMax.y;
								const ImU32 color = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
								ImGui::GetWindowDrawList()->AddLine(
									ImVec2(itemRectMin.x, lineY),
									ImVec2(itemRectMax.x, lineY),
									color, 2.0f);
							}
						}

						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
							auto* dragData = static_cast<const HierarchyDragData*>(payload->Data);
							EntityHandle primaryDraggedHandle = static_cast<EntityHandle>(dragData->EntityHandle);
							const uint64_t targetSceneIdValue = static_cast<uint64_t>(scene.GetSceneId());
							const bool sameScene = dragData->SourceSceneId == targetSceneIdValue;
							// Reject cross-scene migration into prefab edit mode: would import foreign PrefabGUID/origin metadata into the asset.
							if (!sameScene && IsInPrefabEditMode()) {
								// drop ignored
							}
							else if (!sameScene) {
								Scene* sourceScene = nullptr;
								for (Scene* candidate : scenesToShow) {
									if (candidate && static_cast<uint64_t>(candidate->GetSceneId()) == dragData->SourceSceneId) {
										sourceScene = candidate;
										break;
									}
								}
								if (sourceScene && sourceScene != &scene) {
									std::vector<EntityHandle> sourceHandles = ResolveDraggedHierarchyEntities(*sourceScene, primaryDraggedHandle);
									std::vector<EntityHandle> migrated = MigrateEntitiesToScene(*sourceScene, scene, sourceHandles, Entity::Null);
									if (!migrated.empty()) {
										// Splice migrated handles into m_EntityOrder immediately; MoveSiblingNextTo only repositions entries already in the list.
										for (EntityHandle h : migrated) {
											m_EntityOrder.push_back(h);
										}
										if (inTopZone || inBottomZone) {
											EntityHandle anchor = entityHandle;
											if (inBottomZone) {
												for (EntityHandle h : migrated) {
													MoveSiblingNextTo(scene, h, anchor, /*insertAfter*/true);
													anchor = h;
												}
											}
											else {
												for (auto it = migrated.rbegin(); it != migrated.rend(); ++it) {
													MoveSiblingNextTo(scene, *it, anchor, /*insertAfter*/false);
													anchor = *it;
												}
											}
										}
										else {
											// Middle-zone reparent under `entity`.
											for (EntityHandle h : migrated) {
												SetEntityParentPreservingWorld(scene, h, entity);
											}
											m_CollapsedHierarchyEntities.erase(static_cast<uint32_t>(entityHandle));
										}
										ClearEntitySelection();
										m_SelectedEntities = migrated;
										RebuildSelectionSet();
										m_SelectedEntity = migrated.back();
										m_LastInteractedSceneName = scene.GetName();
										m_EntityOrderDirty = true;
									}
								}
							}
							else {
							std::vector<EntityHandle> draggedHandles = ResolveDraggedHierarchyEntities(scene, primaryDraggedHandle);
							// Root-only reorders live in m_EntityOrder; replaying them through a snapshot would push roots to the end.
							const bool targetIsRoot = !entity.HasParent();
							bool recordMove = !(inTopZone || inBottomZone) || !targetIsRoot;
							for (EntityHandle h : draggedHandles) recordMove |= scene.GetEntity(h).HasParent();
							std::optional<StructureEditRecorder> moveRecorder;
							if (recordMove) moveRecorder.emplace(scene, draggedHandles, (inTopZone || inBottomZone) ? "Reorder" : "Reparent");
							bool didMove = false;
							if (inTopZone || inBottomZone) {
								// Multi-drop: advance anchor so each entity lands next to the previous one in selection order.
								EntityHandle anchor = entityHandle;
								if (inBottomZone) {
									for (EntityHandle h : draggedHandles) {
										if (h == anchor) continue;
										if (MoveSiblingNextTo(scene, h, anchor, /*insertAfter*/true)) {
											didMove = true;
											anchor = h;
										}
									}
								}
								else {
									for (auto it = draggedHandles.rbegin(); it != draggedHandles.rend(); ++it) {
										EntityHandle h = *it;
										if (h == anchor) continue;
										if (MoveSiblingNextTo(scene, h, anchor, /*insertAfter*/false)) {
											didMove = true;
											anchor = h;
										}
									}
								}
							}
							else {
								for (EntityHandle h : draggedHandles) {
									if (h == entityHandle) continue;
									Entity draggedEntity = scene.GetEntity(h);
									if (draggedEntity.IsAncestorOf(entity)) continue;
									if (SetEntityParentPreservingWorld(scene, h, entity)) {
										didMove = true;
									}
								}
								if (didMove) {
									m_CollapsedHierarchyEntities.erase(static_cast<uint32_t>(entityHandle));
									// Force re-DFS: reparenting doesn't change entity count so the size-mismatch heuristic won't trigger it automatically.
									m_EntityOrderDirty = true;
								}
							}
							if (didMove) {
								scene.MarkDirty();
								if (moveRecorder) PushStructureUndo(scene, moveRecorder->Finish(scene, draggedHandles));
							}
							}
						}
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
							// 1-arg form: asset-browser sends size()+1 bytes with a trailing '\0'; 2-arg would embed it in the string, breaking extension comparisons.
							std::string droppedPath(static_cast<const char*>(payload->Data));
							std::string ext = std::filesystem::path(droppedPath).extension().string();
							std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
							if (ext == ".prefab") {
								// Reject nested-prefab drop in prefab edit mode: would create a second root or allow self-recursion.
								if (!IsInPrefabEditMode()) {
									EntityHandle loaded = SceneSerializer::LoadEntityFromFile(scene, droppedPath);
									if (loaded != entt::null) {
										EnsureEditorUniqueEntityNames(scene, { loaded });
										// Drop ONTO an entity row parents the prefab under that entity (mirrors the
										// HIERARCHY_ENTITY reparent handler above); without this the instance stayed a root.
										SetEntityParentPreservingWorld(scene, loaded, entity);
										m_CollapsedHierarchyEntities.erase(static_cast<uint32_t>(entityHandle));
										SelectEntity(loaded);
										scene.MarkDirty();
										RecordCreatedEntities(scene, std::span(&loaded, 1), "Instantiate Prefab");
									}
									m_EntityOrder.clear(); m_EntityOrderDirty = true;
								}
							}
							else {
								std::vector<EditorScriptDiscovery::ScriptEntry> droppedScripts;
								EditorScriptDiscovery::CollectScriptFile(std::filesystem::path(droppedPath), droppedScripts);
								StructureEditRecorder scriptRecorder(scene, std::span(&entityHandle, 1), "Add Script");
								bool scriptAttached = false;
								for (const auto& scriptEntry : droppedScripts) {
									if (scriptEntry.IsSceneSystem || scriptEntry.IsGlobalSystem) {
										continue;
									}
									if (scriptEntry.IsManagedComponent) {
										scriptAttached |= AttachManagedComponentToEntity(entity, scene, scriptEntry);
									}
									else {
										scriptAttached |= AttachScriptToEntity(entity, scene, scriptEntry);
									}
								}
								// Invalidate the inspector cache so the new
								// component shows up immediately if the dropped-on
								// entity happens to be selected.
								if (scriptAttached) {
									++m_SelectionVersion;
									PushStructureUndo(scene, scriptRecorder.Finish(scene, std::span(&entityHandle, 1)));
								}
							}
						}
						ImGui::EndDragDropTarget();
					}

					// Pop tints before context menu: popups inherit the live style stack, so tints would bleed into menu item text.
					if (entityIsCutMarked)
						ImGui::PopStyleColor();
					if (entityIsPrefabTinted)
						ImGui::PopStyleColor();
					if (entityIsDisabled)
						ImGui::PopStyleColor();

					if (ImGui::BeginPopupContextItem())
					{
						if (scaleHierarchyFont) ImGui::PushFont(nullptr, hierarchyBaseFontSize); // popup at normal size; pop before EndPopup
						RenderCreateEntityMenu(scene, entity);


						ImGui::Separator();

						if (!(IsEntitySelected(entityHandle) && &scene == selectionScene)) {
							SetSingleEntitySelection(entityHandle, visibleIndex);
							if (!IsInPrefabEditMode()) m_LastInteractedSceneName = scene.GetName();
						}

						if (ImGui::MenuItem("Delete", EditorShortcuts::k_DeleteLabel))
						{
							RequestDeleteSelectedEntity(scene);
						}

						if (ImGui::MenuItem("Cut", EditorShortcuts::k_CutLabel))
						{
							CutSelectedEntities(scene);
						}

						if (ImGui::MenuItem("Duplicate", EditorShortcuts::k_DuplicateLabel))
						{
							DuplicateSelectedEntity(scene);
						}

						if (ImGui::MenuItem("Rename", EditorShortcuts::k_RenameLabel))
						{
							BeginRenameSelectedEntity(scene);
						}

						// "Unpack Prefab" only shows up when at least one
						// selected entity is currently a prefab instance —
						// no point cluttering the menu otherwise.
						bool anySelectedIsPrefabInstance = false;
						for (EntityHandle h : GetSelectedEntities(scene)) {
							if (scene.HasComponent<PrefabInstanceComponent>(h)) {
								anySelectedIsPrefabInstance = true;
								break;
							}
						}
						if (anySelectedIsPrefabInstance) {
							ImGui::Separator();
							if (ImGui::MenuItem("Unpack Prefab", "Shift + U")) {
								UnpackSelectedPrefabs(scene);
							}
						}

						if (scaleHierarchyFont) ImGui::PopFont();
						ImGui::EndPopup();
					}

					ImGui::PopID();

					if (rowIndent > 0.0f) ImGui::Unindent(rowIndent);
				}
				} // while (clipper.Step())
				clipper.End();

				ImGui::PopStyleVar(); // ItemSpacing

				// TreePop matches the TreeNodeEx above, which prefab-edit
				// mode skips — balance the call accordingly.
				if (!IsInPrefabEditMode()) {
					ImGui::TreePop();
				}
			}

			ImGui::PopID();
		}

		// Deferred scene removal (after iteration)
		if (!sceneToRemove.empty()) {
			RequestEditorAction(PendingEditorAction::RemoveScene, std::move(sceneToRemove));
		}

		if (pendingSceneReorder) {
			SceneManager::MoveLoadedScene(pendingSceneReorder->SourceName, pendingSceneReorder->TargetIndex);
			m_EntityOrderDirty = true;
		}

		// Drag-drop target: only present during an active drag so it doesn't block right-click menus
		if (ImGui::GetDragDropPayload() != nullptr) {
			ImVec2 avail = ImGui::GetContentRegionAvail();
			if (avail.y > 0) {
				ImGui::InvisibleButton("##SceneDropTarget", ImVec2(-1, avail.y));
				if (ImGui::BeginDragDropTarget()) {
					// Drop to bottom panel: detaches entity to root and moves it to end of m_EntityOrder. Disabled in prefab mode (would create second root).
					if (!IsInPrefabEditMode())
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
						auto* dragData = static_cast<const HierarchyDragData*>(payload->Data);
						EntityHandle primaryDraggedHandle = static_cast<EntityHandle>(dragData->EntityHandle);
						Scene* dropScene = GetContextScene();
						if (dropScene) {
							const uint64_t dropSceneIdValue = static_cast<uint64_t>(dropScene->GetSceneId());
							const bool sameScene = dragData->SourceSceneId == dropSceneIdValue;
							if (!sameScene) {
								Scene* sourceScene = nullptr;
								auto loadedScenes = SceneManager::GetLoadedScenes();
								for (auto& weakScene : loadedScenes) {
									if (auto scenePtr = weakScene.lock();
										scenePtr && static_cast<uint64_t>(scenePtr->GetSceneId()) == dragData->SourceSceneId) {
										sourceScene = scenePtr.get();
										break;
									}
								}
								if (sourceScene && sourceScene != dropScene) {
									std::vector<EntityHandle> sourceHandles = ResolveDraggedHierarchyEntities(*sourceScene, primaryDraggedHandle);
									std::vector<EntityHandle> migrated = MigrateEntitiesToScene(*sourceScene, *dropScene, sourceHandles, Entity::Null);
									if (!migrated.empty()) {
										ClearEntitySelection();
										m_SelectedEntities = migrated;
										RebuildSelectionSet();
										m_SelectedEntity = migrated.back();
										m_LastInteractedSceneName = dropScene->GetName();
										m_EntityOrderDirty = true;
									}
								}
							}
							else {
							std::vector<EntityHandle> draggedHandles = ResolveDraggedHierarchyEntities(*dropScene, primaryDraggedHandle);
							StructureEditRecorder recorder(*dropScene, draggedHandles, "Reparent");
							bool didMove = false;
							for (EntityHandle h : draggedHandles) {
								Entity draggedEntity = dropScene->GetEntity(h);
								if (draggedEntity.HasParent()) {
									if (SetEntityParentPreservingWorld(*dropScene, h, Entity::Null)) {
										didMove = true;
									}
								}
								auto orderIt = std::find(m_EntityOrder.begin(), m_EntityOrder.end(), h);
								if (orderIt != m_EntityOrder.end() && std::next(orderIt) != m_EntityOrder.end()) {
									m_EntityOrder.erase(orderIt);
									m_EntityOrder.push_back(h);
									m_EntityOrderDirty = true; // M30: re-DFS next render.
								}
							}
							if (didMove) {
								m_EntityOrderDirty = true;
								dropScene->MarkDirty();
								PushStructureUndo(*dropScene, recorder.Finish(*dropScene, draggedHandles));
							}
							}
						}
					}
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
						std::string droppedPath(static_cast<const char*>(payload->Data));
						std::string ext = std::filesystem::path(droppedPath).extension().string();
						std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
						// Scene-level drops create root entities; reject in prefab mode (violates single-root invariant; .prefab could recurse if same asset).
						if (IsInPrefabEditMode()) {
							// drop ignored
						}
						else if (ext == ".scene") {
							m_PendingSceneFileDrop = droppedPath;
						}
						else if (ext == ".prefab") {
							Scene* dropScene = GetContextScene();
							if (dropScene) {
								EntityHandle loaded = SceneSerializer::LoadEntityFromFile(*dropScene, droppedPath);
								if (loaded != entt::null) {
									EnsureEditorUniqueEntityNames(*dropScene, { loaded });
									RecordCreatedEntities(*dropScene, std::span(&loaded, 1), "Instantiate Prefab");
								}
								m_EntityOrder.clear(); m_EntityOrderDirty = true;
							}
						}
						else if ((ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
							&& !EngineModules::IsActive(EngineModule::TwoD)) {
							// A dropped image becomes a Sprite Renderer on a Transform2D entity.
							FNK_WARN_TAG("Editor", "Dropping an image creates a sprite, which needs the Funke.2D module (Project Settings > Build > Engine Modules).");
						}
						else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
							Scene* dropScene = GetContextScene();
							if (dropScene) {
								std::string entityName = std::filesystem::path(droppedPath).stem().string();
								Entity newEntity = dropScene->CreateEntity(entityName);
								auto& sr = newEntity.AddComponent<SpriteRendererComponent>();
								sr.TextureHandle = TextureManager::LoadTexture(droppedPath);

								Texture2D* tex = TextureManager::GetTexture(sr.TextureHandle);
								if (tex && tex->IsValid() && tex->GetHeight() > 0) {
									float aspect = (float)tex->GetWidth() / (float)tex->GetHeight();
									auto& transform = newEntity.GetComponent<Transform2DComponent>();
									transform.Scale = { aspect, 1.0f };
								}

								EnsureEditorUniqueEntityNames(*dropScene, { newEntity.GetHandle() });
								SelectEntity(newEntity.GetHandle());
								dropScene->MarkDirty();
								const EntityHandle createdHandle = newEntity.GetHandle();
								RecordCreatedEntities(*dropScene, std::span(&createdHandle, 1), "Create Sprite");
							}
						}
						else {
							// Script dropped on empty space → new root entity named after the script with the script attached. Scene/global systems aren't entity components (the Systems inspector handles those).
							Scene* dropScene = GetContextScene();
							if (dropScene) {
								std::vector<EditorScriptDiscovery::ScriptEntry> droppedScripts;
								EditorScriptDiscovery::CollectScriptFile(std::filesystem::path(droppedPath), droppedScripts);

								Entity newEntity = Entity::Null;
								bool created = false;
								for (const auto& scriptEntry : droppedScripts) {
									if (scriptEntry.IsSceneSystem || scriptEntry.IsGlobalSystem) {
										continue;
									}
									if (!created) {
										newEntity = dropScene->CreateEntity(scriptEntry.ClassName);
										created = true;
									}
									if (scriptEntry.IsManagedComponent) {
										AttachManagedComponentToEntity(newEntity, *dropScene, scriptEntry);
									}
									else {
										AttachScriptToEntity(newEntity, *dropScene, scriptEntry);
									}
								}

								if (created) {
									EnsureEditorUniqueEntityNames(*dropScene, { newEntity.GetHandle() });
									SelectEntity(newEntity.GetHandle());
									dropScene->MarkDirty();
									m_EntityOrder.clear();
									m_EntityOrderDirty = true;
									++m_SelectionVersion;
									const EntityHandle createdHandle = newEntity.GetHandle();
									RecordCreatedEntities(*dropScene, std::span(&createdHandle, 1), "Create Entity");
								}
							}
						}
					}
					ImGui::EndDragDropTarget();
				}
			}
		}

		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)
			&& ImGui::IsMouseReleased(ImGuiMouseButton_Left)
			&& !IsLeftMouseDragPastClickThreshold()
			&& !ImGui::IsAnyItemHovered()) {
			ClearEntitySelection();
		}

		ImGui::EndChild(); // ##HierarchyScroll

		// RootAndChildWindows so the scroll child counts as panel focus for shortcuts.
		m_IsEntitiesPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (scaleHierarchyFont) {
			ImGui::PopFont();
		}
		ImGui::End();
	}

	void ImGuiEditorLayer::RenderSceneSystemsInspector(Scene& scene) {
		const auto systemsBefore = SceneSystemsEditCommand::Capture(scene);
		bool structureChanged = false;
		ImGui::Text("Scene: %s", scene.GetName().c_str());
		ImGui::SeparatorText("Systems");

		auto acceptDroppedSceneSystems = [&]() {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
				// 1-arg form: 2-arg would embed trailing '\0' and break extension matching in CollectScriptFile.
				std::string droppedPath(static_cast<const char*>(payload->Data));
				std::vector<EditorScriptDiscovery::ScriptEntry> droppedScripts;
				EditorScriptDiscovery::CollectScriptFile(std::filesystem::path(droppedPath), droppedScripts);
				for (const auto& scriptEntry : droppedScripts) {
					if (scriptEntry.IsSceneSystem) {
						structureChanged |= scene.AddSceneSystem(scriptEntry.ClassName);
					}
				}
			}
		};

		const auto& systems = scene.GetSceneSystemClassNames();
		for (size_t i = 0; i < systems.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			const std::string className = systems[i];

			// AllowOverlap lets trailing buttons hit-test over the header widget.
			bool open = ImGui::CollapsingHeader((className + "##system_header").c_str(),
				ImGuiTreeNodeFlags_AllowOverlap);

			const ImGuiStyle& style = ImGui::GetStyle();
			const float btnW = ImGui::GetFrameHeight();
			const float spacing = style.ItemInnerSpacing.x;
			const float stripW = btnW * 4.0f + spacing * 3.0f;
			ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - stripW);

			bool enabled = scene.IsSceneSystemEnabled(className);
			if (ImGuiUtils::Switch("##enabled", &enabled)) {
				structureChanged |= scene.SetSceneSystemEnabled(className, enabled);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", enabled ? "Disable SceneSystem" : "Enable SceneSystem");
			}

			ImGui::SameLine(0, spacing);
			if (EditorUI::IconButton("##move_up", "arrow_up", "Move up", false, false, btnW) && i > 0) {
				structureChanged |= scene.MoveSceneSystem(i, i - 1);
			}

			ImGui::SameLine(0, spacing);
			if (EditorUI::IconButton("##move_down", "arrow_down", "Move down", false, false, btnW) && i + 1 < systems.size()) {
				structureChanged |= scene.MoveSceneSystem(i, i + 1);
			}

			ImGui::SameLine(0, spacing);
			const bool remove = EditorUI::IconButton("##remove", "delete", "Remove SceneSystem", false, false, btnW);
			if (remove) {
				structureChanged |= scene.RemoveSceneSystem(i);
				ImGui::PopID();
				break;
			}

			if (open) {
				ImGui::Indent(8.0f);
				DrawSceneSystemFields(scene, className);
				ImGui::Unindent(8.0f);
			}

			ImGui::PopID();
		}

		// Pre-collect available systems so the button can be disabled when nothing
		// can be added — avoids the empty-popup sliver that looks broken.
		std::vector<EditorScriptDiscovery::ScriptEntry> scriptEntries;
		EditorScriptDiscovery::CollectProjectScriptEntries(scriptEntries);
		size_t availableSystemCount = 0;
		for (const auto& entry : scriptEntries) {
			if (entry.IsSceneSystem && !scene.HasSceneSystem(entry.ClassName)) {
				++availableSystemCount;
			}
		}
		const bool hasAvailableSystem = availableSystemCount > 0;

		float buttonWidth = ImGui::GetContentRegionAvail().x;
		if (!hasAvailableSystem) ImGui::BeginDisabled();
		if (Icons::ButtonWithIcon(Icons::Type::Plus, "Add System", ImVec2(buttonWidth, 0), true)) {
			ImGui::OpenPopup("AddSystemPopup");
			m_SystemSearchBuffer[0] = '\0';
		}
		// Scoped drag-drop target: only the "+ Add System" button accepts
		// dropped scripts. Previously the whole inspector InnerRect was a
		// drop zone which was confusing.
		if (ImGui::BeginDragDropTarget()) {
			acceptDroppedSceneSystems();
			ImGui::EndDragDropTarget();
		}
		if (!hasAvailableSystem) {
			ImGui::EndDisabled();
			// AllowWhenDisabled so hover still fires after BeginDisabled.
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
				ImGui::SetTooltip("No game systems available.\nCreate one via Asset Browser > Create > Scripting > SceneSystem (C#).");
			}
		}

		if (ImGui::BeginPopup("AddSystemPopup")) {
			EditorUI::SearchField("##SystemSearch", m_SystemSearchBuffer, sizeof(m_SystemSearchBuffer),
				"Search systems...", ImGui::GetFontSize() * 18.0f);
			ImGui::Separator();

			std::string filter(m_SystemSearchBuffer);
			std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

			for (const auto& scriptEntry : scriptEntries) {
				if (!scriptEntry.IsSceneSystem || scene.HasSceneSystem(scriptEntry.ClassName)) {
					continue;
				}

				if (!filter.empty()) {
					std::string lowerClassName = EditorScriptDiscovery::ToLowerCopy(scriptEntry.ClassName);
					std::string lowerPath = EditorScriptDiscovery::ToLowerCopy(scriptEntry.Path.string());
					if (lowerClassName.find(filter) == std::string::npos
						&& lowerPath.find(filter) == std::string::npos) {
						continue;
					}
				}

				const std::string label = BuildScriptMenuLabel(scriptEntry);
				const std::string id = EditorScriptDiscovery::EntryId(scriptEntry);
				if (ImGuiUtils::MenuItemEllipsis(label, id.c_str(), nullptr, false, true, 260.0f)) {
					structureChanged |= scene.AddSceneSystem(scriptEntry.ClassName);
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}

		if (structureChanged) {
			PushStructureUndo(scene, std::make_unique<SceneSystemsEditCommand>(systemsBefore, SceneSystemsEditCommand::Capture(scene)));
		}
	}

	std::vector<std::string> ImGuiEditorLayer::CurrentEntityNames(Scene& scene, std::span<const EntityHandle> handles) {
		std::vector<std::string> names;
		names.reserve(handles.size());
		for (EntityHandle h : handles) {
			const NameComponent* name = scene.IsValid(h) ? scene.GetRegistry().try_get<NameComponent>(h) : nullptr;
			names.push_back(name ? name->Name : std::string{});
		}
		return names;
	}

	void ImGuiEditorLayer::FinishInspectorRename(Scene& scene) {
		if (!m_InspectorRename) return;
		if (m_InspectorRename->Recorder && CurrentEntityNames(scene, m_InspectorRename->Handles) != m_InspectorRename->NamesBefore) {
			PushStructureUndo(scene, m_InspectorRename->Recorder->Finish(scene, m_InspectorRename->Handles));
		}
		m_InspectorRename.reset();
	}

	void ImGuiEditorLayer::RenderInspectorPanel(Scene& scene) {
		ImGui::Begin("Inspector");

		// Cache avoids O(N×M) loops per frame over (selection × components) at large selections.
		// Gameplay can destroy a selected entity between OnUpdate's selection prune and this render.
		bool cacheHasStaleHandle = false;
		for (EntityHandle h : m_InspectorCache.Handles) {
			if (!scene.IsValid(h)) { cacheHasStaleHandle = true; break; }
		}
		if (cacheHasStaleHandle) {
			++m_SelectionVersion;
		}
		if (m_InspectorCache.Version != m_SelectionVersion) {
			RecomputeInspectorSelectionCache(scene);
			m_InspectorCache.Version = m_SelectionVersion;
		}
		const std::vector<EntityHandle>& selectedHandles = m_InspectorCache.Handles;
		// The name field is no longer drawn for that selection, so its deactivation never arrives.
		if (m_InspectorRename && m_InspectorRename->Handles != selectedHandles) {
			FinishInspectorRename(scene);
		}
		if (selectedHandles.empty()) {
			m_SelectedEntity = entt::null;
			if (m_IsSceneNodeSelected) {
				RenderSceneSystemsInspector(scene);
				// Render picker popup here too: this early return bypasses the later RenderPopup call.
				ReferencePicker::RenderPopup();
			}
			else {
				RenderAssetInspector();
				// DataAsset reference fields open the picker; same early-return bypass as above.
				ReferencePicker::RenderPopup();
			}
			m_IsInspectorPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			ImGui::End();
			return;
		}

		std::vector<Entity> selectedEntities;
		selectedEntities.reserve(selectedHandles.size());
		for (EntityHandle h : selectedHandles) {
			selectedEntities.push_back(scene.GetEntity(h));
		}
		std::span<const Entity> entitySpan(selectedEntities);
		Entity entity = selectedEntities[0]; // primary, used for the drag-source label and prefab buttons
		if (!scene.IsValid(m_SelectedEntity)) {
			m_SelectedEntity = entity.GetHandle();
		}

		// ── Entity Header: type icon, name + summary, enabled switch, options menu ──
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float frameH = ImGui::GetFrameHeight();
			const float iconBox = std::round(frameH * 1.9f);
			const ImVec2 boxMin = ImGui::GetCursorScreenPos();
			ImDrawList* headerDraw = ImGui::GetWindowDrawList();
			headerDraw->AddRectFilled(boxMin, ImVec2(boxMin.x + iconBox, boxMin.y + iconBox),
				ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding * 1.5f);
			const float glyph = std::round(iconBox * 0.55f);
			const char* headerIconName = EntityTypeIconName(scene, entity.GetHandle());
			if (const uint64_t icon = EditorIcons::Get(headerIconName, static_cast<int>(glyph))) {
				const ImVec2 g0(boxMin.x + (iconBox - glyph) * 0.5f, boxMin.y + (iconBox - glyph) * 0.5f);
				// Component icons are coloured; a tint would multiply them off-hue. Only the plain entity glyph follows the text colour.
				const ImU32 tint = std::string_view(headerIconName) == "entity"
					? ImGui::GetColorU32(ImGuiCol_Text) : IM_COL32_WHITE;
				headerDraw->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(icon)), g0, ImVec2(g0.x + glyph, g0.y + glyph),
					ImVec2(0, 1), ImVec2(1, 0), tint);
			}
			ImGui::Dummy(ImVec2(iconBox, iconBox));
			ImGui::SameLine();
			ImGui::BeginGroup();

			// Empty buffer + hint avoids the bug where Enter on a nameless entity (buffer pre-filled with "Entity") created a real NameComponent.
			const bool nameUniform = m_InspectorCache.NameUniform;
			char nameBuf[256]{};
			if (nameUniform && !m_InspectorCache.FirstName.empty()) {
				std::snprintf(nameBuf, sizeof(nameBuf), "%s", m_InspectorCache.FirstName.c_str());
			}
			const char* nameHint = nameUniform ? "Entity" : "-";
			const float switchW = std::round(std::round(frameH * 0.62f) * 1.8f);
			ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - switchW - frameH - style.ItemSpacing.x * 2.0f));
			const bool nameEdited = ImGui::InputTextWithHint("##EntityName", nameHint, nameBuf, sizeof(nameBuf));
			const bool nameActivated = ImGui::IsItemActivated();
			const bool nameDeactivated = ImGui::IsItemDeactivated();

			// Shows authored state, not effective in-hierarchy: InheritedDisabledTag doesn't flip the switch.
			ImGui::SameLine();
			bool isEnabled = m_InspectorCache.EnabledFirst;
			const bool enabledChanged = ImGuiUtils::ToggleSwitch("##EntityEnabled", &isEnabled, !m_InspectorCache.EnabledUniform);
			EditorUI::Tooltip("Enabled");

			ImGui::SameLine();
			if (EditorUI::IconButton("##EntityOptions", "settings", "Entity options")) {
				ImGui::OpenPopup("EntityHeaderMenu");
			}

			const bool isStaticNow = m_InspectorCache.StaticFirst;
			std::string summary = selectedEntities.size() > 1
				? std::to_string(selectedEntities.size()) + " entities selected"
				: std::string("Entity");
			if (m_InspectorCache.StaticUniform && isStaticNow) summary += "  -  Static";
			if (m_InspectorCache.EnabledUniform && !m_InspectorCache.EnabledFirst) summary += "  -  Disabled";
			ImGui::TextDisabled("%s", summary.c_str());
			ImGui::EndGroup();

			bool staticChanged = false;
			bool isStatic = isStaticNow;
			if (ImGui::BeginPopup("EntityHeaderMenu")) {
				if (ImGui::MenuItem("Static", nullptr, m_InspectorCache.StaticUniform && isStaticNow)) {
					isStatic = !(m_InspectorCache.StaticUniform && isStaticNow);
					staticChanged = true;
				}
				EditorUI::Tooltip("Static entities are excluded from per-frame transform updates.");
				ImGui::Separator();
				ImGui::TextDisabled("Component View");
				const InspectorComponentView currentView = EditorPreferences::GetInspectorComponentView();
				if (ImGui::MenuItem("Normal", nullptr, currentView == InspectorComponentView::Normal)) {
					EditorPreferences::SetInspectorComponentView(InspectorComponentView::Normal);
				}
				EditorUI::Tooltip("Show editable component fields only.");
				if (ImGui::MenuItem("Detailed", nullptr, currentView == InspectorComponentView::Detailed)) {
					EditorPreferences::SetInspectorComponentView(InspectorComponentView::Detailed);
				}
				EditorUI::Tooltip("Also show read-only / computed script values\n(get-only properties, const and readonly fields).");
				ImGui::EndPopup();
			}

			std::vector<EntityHandle> headerHandles;
			for (Entity& e : selectedEntities) headerHandles.push_back(e.GetHandle());
			if (nameActivated || (nameEdited && ImGui::IsItemActive() && !m_InspectorRename)) {
				FinishInspectorRename(scene);
				m_InspectorRename.emplace();
				m_InspectorRename->Handles = headerHandles;
				m_InspectorRename->NamesBefore = CurrentEntityNames(scene, headerHandles);
				m_InspectorRename->Recorder.emplace(scene, headerHandles, "Rename");
			}
			// On the deactivation frame ImGui writes the typed text back into the buffer, which by then may be
			// seeded from another selection (the click that ended the edit selected a different entity).
			const bool renameTargetsSelection = m_InspectorRename && m_InspectorRename->Handles == headerHandles;
			// A mixed-name selection shows an empty field; leaving it empty keeps every name.
			const bool nameChanged = nameEdited && renameTargetsSelection
				&& !(nameBuf[0] == '\0' && !m_InspectorCache.NameUniform);
			if (nameChanged) {
				std::string newName(nameBuf);
				for (Entity& e : selectedEntities) {
					if (newName.empty()) {
						if (e.HasComponent<NameComponent>()) {
							e.RemoveComponent<NameComponent>();
						}
					}
					else if (e.HasComponent<NameComponent>()) {
						e.GetComponent<NameComponent>().Name = newName;
					}
					else {
						scene.AddComponent<NameComponent>(e.GetHandle(), newName);
					}
				}
				scene.MarkDirty();
				// NameComponent can be added/removed and FirstName changes; the
				// next inspector frame must rebuild its cached header strings.
				++m_SelectionVersion;
			}
			if (nameDeactivated) {
				FinishInspectorRename(scene);
			}

			std::optional<StructureEditRecorder> headerRecorder;
			if (enabledChanged || staticChanged) {
				headerRecorder.emplace(scene, headerHandles,
					enabledChanged ? (isEnabled ? "Enable" : "Disable") : "Toggle Static");
			}
			if (enabledChanged) {
				for (Entity& e : selectedEntities) {
					e.SetEnabled(isEnabled);
				}
				scene.MarkDirty();
				// DisabledTag/InheritedDisabledTag presence changes — cached EnabledFirst/EnabledUniform are stale.
				++m_SelectionVersion;
			}
			if (staticChanged) {
				for (Entity& e : selectedEntities) {
					if (isStatic && !e.HasComponent<StaticTag>()) e.AddComponent<StaticTag>();
					else if (!isStatic && e.HasComponent<StaticTag>()) e.RemoveComponent<StaticTag>();
				}
				scene.MarkDirty();
				// StaticTag presence changes — cached StaticFirst/StaticUniform are stale.
				++m_SelectionVersion;
			}
			if (headerRecorder) PushStructureUndo(scene, headerRecorder->Finish(scene, headerHandles));
		}

		/* INFO(Ben-Scr): MetaData Doesn't need to be displayed
		if (const EntityMetaData* metaData = entity.GetMetaData()) {
			const char* originText = "Runtime";
			if (metaData->Origin == EntityOrigin::Scene) originText = "Scene";
			if (metaData->Origin == EntityOrigin::Prefab) originText = "Prefab";
			ImGui::TextDisabled("Origin: %s", originText);

			const uint64_t runtimeId = metaData->RuntimeID;
			ImGui::TextDisabled("RuntimeID: %llu", runtimeId);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to copy");
			if (ImGui::IsItemClicked()) ImGui::SetClipboardText(std::to_string(runtimeId).c_str());

			if (metaData->Origin == EntityOrigin::Scene && static_cast<uint64_t>(metaData->SceneGUID) != 0) {
				const uint64_t sceneGuid = static_cast<uint64_t>(metaData->SceneGUID);
				ImGui::TextDisabled("SceneGUID: %llu", sceneGuid);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to copy");
				if (ImGui::IsItemClicked()) ImGui::SetClipboardText(std::to_string(sceneGuid).c_str());
			}
			if (metaData->Origin == EntityOrigin::Prefab && static_cast<uint64_t>(metaData->PrefabGUID) != 0) {
				const uint64_t prefabGuid = static_cast<uint64_t>(metaData->PrefabGUID);
				ImGui::TextDisabled("PrefabGUID: %llu", prefabGuid);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to copy");
				if (ImGui::IsItemClicked()) ImGui::SetClipboardText(std::to_string(prefabGuid).c_str());

				if (ImGui::Button("Apply to Prefab")) {
					SceneSerializer::ApplyPrefabInstanceOverrides(scene, m_SelectedEntity);
				}
				ImGui::SameLine();
				if (ImGui::Button("Revert All")) {
					EntityHandle replacement = SceneSerializer::RevertPrefabInstanceOverride(scene, m_SelectedEntity, {});
					if (replacement != entt::null) {
						SelectEntity(replacement);
						m_EntityOrder.clear(); m_EntityOrderDirty = true;
					}
				}
			}
		}
		*/

		m_IsInspectorPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		ImGui::Separator();

		const auto& registry = SceneManager::GetComponentRegistry();

		std::type_index pendingRemoval = typeid(void);

		Json::Value prefabOverrides = Json::Value::MakeObject();
		const bool isSinglePrefabInstance = selectedEntities.size() == 1
			&& scene.GetEntityOrigin(entity.GetHandle()) == EntityOrigin::Prefab
			&& static_cast<uint64_t>(scene.GetPrefabGUID(entity.GetHandle())) != 0;
		bool prefabSourceResolvable = false;
		// Always resolved to the actual prefab root: applying a child as if it were root would overwrite the prefab file with that child's subtree.
		EntityHandle prefabInstanceRoot = entt::null;
		// True iff ANY entity in the subtree has overrides; per-entity prefabOverrides only captures this entity's diff.
		bool hasSubtreeOverrides = false;
		if (isSinglePrefabInstance) {
			prefabSourceResolvable = SceneSerializer::ComputeInstanceOverrides(
				scene, entity.GetHandle(), prefabOverrides);
			if (prefabSourceResolvable) {
				prefabInstanceRoot = SceneSerializer::GetPrefabInstanceRoot(scene, entity.GetHandle());
				hasSubtreeOverrides = SceneSerializer::HasPrefabInstanceOverrides(scene, entity.GetHandle());
			}
		}

		if (isSinglePrefabInstance && prefabSourceResolvable && prefabInstanceRoot != entt::null) {
			// Disable Apply/Revert when there are no overrides: Apply would still trigger disk write + live-instance refresh for no semantic change.
			ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors::PrefabInstance());
			ImGui::TextUnformatted(entity.GetHandle() == prefabInstanceRoot
				? "Prefab Instance"
				: "Prefab Instance (child)");
			ImGui::PopStyleColor();
			{
				const char* selectLabel = "Select Prefab";
				const float selectW = ImGui::CalcTextSize(selectLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
				const float afterLabelX = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetStyle().ItemSpacing.x;
				ImGui::SameLine(std::max(afterLabelX, ImGui::GetWindowContentRegionMax().x - selectW));
				if (ImGui::SmallButton(selectLabel)) {
					AssetBrowser::RequestRevealAsset(
						AssetRegistry::ResolvePath(static_cast<uint64_t>(scene.GetPrefabGUID(prefabInstanceRoot))));
				}
				EditorUI::Tooltip("Show the source prefab in the Asset Browser");
			}
			ImGui::BeginDisabled(!hasSubtreeOverrides || Application::GetIsPlaying()); // play mode: Apply would bake runtime state, Revert would destroy the running instance
			if (ImGui::SmallButton("Apply All")) {
				ApplyPrefabInstanceToSource(scene, prefabInstanceRoot);
				ImGui::EndDisabled();
				ImGui::Separator();
				ImGui::End();
				return;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Revert All")) {
				// RevertPrefabInstanceOverride destroys the root and returns a replacement; bail out of the inspector body this frame to avoid iterating destroyed handles.
				const EntityHandle revertedRoot = prefabInstanceRoot;
				StructureEditRecorder undo(scene, std::span(&revertedRoot, 1), "Revert Prefab");
				EntityHandle replacement = SceneSerializer::RevertPrefabInstanceOverride(scene, revertedRoot, {});
				if (replacement != entt::null) {
					PushStructureUndo(scene, undo.Finish(scene, std::span(&replacement, 1)));
					if (m_PrefabEditRootEntity == revertedRoot) {
						m_PrefabEditRootEntity = replacement;
					}
					SelectEntity(replacement); // also bumps m_SelectionVersion
					m_EntityOrder.clear(); m_EntityOrderDirty = true;
					ImGui::EndDisabled();
					ImGui::Separator();
					ImGui::End();
					return;
				}
			}
			ImGui::EndDisabled();
			ImGui::Separator();
		}
		else if (isSinglePrefabInstance) {
			ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors::PrefabOrphan());
			ImGui::TextUnformatted("Prefab Instance (orphaned — source missing)");
			ImGui::PopStyleColor();
			ImGui::Separator();
		}

		// The entity header and prefab bar stay pinned; only the components scroll.
		ImGui::BeginChild("##InspectorBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_None);

		// Bucket override paths by component name once per frame for O(1) per-component lookup (was O(components × paths) per render).
		std::unordered_map<std::string, std::vector<std::string>> overridesByComponent;
		if (prefabSourceResolvable) {
			for (const auto& [path, _] : prefabOverrides.GetObject()) {
				const std::size_t dot = path.find('.');
				const std::string componentKey = (dot == std::string::npos)
					? path
					: path.substr(0, dot);
				overridesByComponent[componentKey].push_back(path);
			}
		}

		// Lambda: do any override paths fall under this component's serializedName?
		auto componentHasOverrides = [&](const std::string& serializedName) -> bool {
			if (Application::GetIsPlaying() && !scene.IsDetached()) return false;
			if (!prefabSourceResolvable || serializedName.empty()) return false;
			auto it = overridesByComponent.find(serializedName);
			return it != overridesByComponent.end() && !it->second.empty();
		};

		const std::vector<std::string>& hiddenPartialComponents = m_InspectorCache.PartialComponents;

		// Latch: a Revert destroys+recreates `entity`; remaining ForEachComponentInfo iterations this frame must be no-ops to avoid use-after-destroy.
		bool selectionInvalidatedByRevert = false;
		const auto drawComponentSection = [&](const std::type_index& typeId, const ComponentInfo& info) {
			if (selectionInvalidatedByRevert) return;
			if (info.category != ComponentCategory::Component) return;
			if (info.displayName == "Name") return; // Shown in entity header

			if (m_InspectorCache.CommonComponentTypes.find(typeId)
				== m_InspectorCache.CommonComponentTypes.end()) { // O(1) cache gate vs per-frame O(N) info.has() sweep
				return;
			}

			// Scripts render their own per-script sections — skip the outer wrapper
			if (info.displayName == "Scripts") {
				DispatchComponentInspector(info, entitySpan);
				return;
			}

			// Dynamic components with no drawInspector/properties render as empty sections; their fields appear in the paired ScriptComponent.Scripts entry.
			if (info.isDynamic && !info.drawInspector && info.properties.empty()) {
				return;
			}

			bool removeRequested = false;
			bool copyRequested = false;
			bool pasteRequested = false;
			bool resetRequested = false;
			ComponentClipboardPayload clipboardPayload;
			const bool hasComponentClipboard = TryReadComponentClipboard(m_ComponentClipboardJson, clipboardPayload);
			const ComponentInfo* pastedComponentInfo = hasComponentClipboard
				? FindComponentInfoBySerializedName(registry, clipboardPayload.SerializedName)
				: nullptr;
			const bool canPasteComponent = hasComponentClipboard
				&& pastedComponentInfo
				&& pastedComponentInfo->add
				&& pastedComponentInfo->has
				&& clipboardPayload.Data.IsObject();
			const bool canSerializeComponent = !info.serializedName.empty();

			const std::string componentSerializedName = info.serializedName;
			const bool thisComponentOverridden = componentHasOverrides(componentSerializedName);

			bool revertComponentRequested = false;
			bool applyComponentRequested = false;
			std::string revertFieldRequested; // empty = none

			bool open = ImGuiUtils::BeginComponentSection(info.displayName.c_str(), removeRequested, [&]() {
				if (ImGui::MenuItem("Copy Component", nullptr, false, canSerializeComponent)) {
					copyRequested = true;
				}
				if (ImGui::MenuItem("Paste Component", nullptr, false, canPasteComponent)) {
					pasteRequested = true;
				}
				if (ImGui::MenuItem("Reset Component", nullptr, false, canSerializeComponent)) {
					resetRequested = true;
				}
				// Prefab override actions — only meaningful when this entity is a
				// prefab instance whose source resolves AND this component has
				// at least one diff against the source.
				if (isSinglePrefabInstance && prefabSourceResolvable && thisComponentOverridden) {
					ImGui::Separator();
					if (ImGui::BeginMenu("Revert Field", !Application::GetIsPlaying())) {
						const std::string prefix = componentSerializedName + ".";
						for (const auto& [path, _] : prefabOverrides.GetObject()) {
							if (path != componentSerializedName && path.compare(0, prefix.size(), prefix) != 0) continue;
							const char* leaf = path.c_str() + (path.size() > prefix.size() ? prefix.size() : 0);
							if (ImGui::MenuItem(leaf)) {
								revertFieldRequested = path;
							}
						}
						ImGui::EndMenu();
					}
					if (ImGui::MenuItem("Revert Component", nullptr, false, !Application::GetIsPlaying())) {
						revertComponentRequested = true;
					}
					if (ImGui::MenuItem("Apply Component", nullptr, false, !Application::GetIsPlaying())) {
						applyComponentRequested = true;
					}
				}
			}, info.icon, thisComponentOverridden);

			if (removeRequested) {
				pendingRemoval = typeId;
			}
			if (copyRequested && canSerializeComponent) {
				Json::Value componentValue = SceneSerializer::SerializeComponent(scene, entity.GetHandle(), info.serializedName);
				if (!componentValue.IsNull()) {
					Json::Value root = Json::Value::MakeObject();
					root.AddMember("type", Json::Value("ComponentClipboard"));
					root.AddMember("component", Json::Value(info.serializedName));
					root.AddMember("displayName", Json::Value(info.displayName));
					root.AddMember("data", std::move(componentValue));
					m_ComponentClipboardJson = Json::Stringify(root, false);
				}
			}
			if (pasteRequested && canPasteComponent) {
				const std::vector<EntityHandle> roots = selectedHandles;
				StructureEditRecorder undo(scene, roots, "Paste Component");
				bool anyApplied = false;
				bool anyAdded = false;
				for (const Entity& e : selectedEntities) {
					if (!pastedComponentInfo->has(e)) {
						// Refused (conflicts with a component on the entity): don't let the deserialize below add it anyway.
						if (!registry.AddWithDependencies(e, pastedComponentInfo->typeId)) continue;
						anyAdded = true;
					}
					if (SceneSerializer::DeserializeComponent(scene, e.GetHandle(),
						clipboardPayload.SerializedName, clipboardPayload.Data)) {
						anyApplied = true;
					}
				}
				if (anyApplied) scene.MarkDirty();
				if (anyApplied || anyAdded) PushStructureUndo(scene, undo.Finish(scene, roots));
				// AddWithDependencies may have inserted a new component type
				// on one or more selected entities — the cached intersection
				// has to refresh so the new section appears.
				if (anyAdded) ++m_SelectionVersion;
			}
			if (resetRequested && canSerializeComponent) {
				const std::vector<EntityHandle> roots = selectedHandles;
				StructureEditRecorder undo(scene, roots, "Reset Component");
				bool anyReset = false;
				for (const Entity& e : selectedEntities) {
					if (SceneSerializer::ResetComponent(scene, e.GetHandle(), info.serializedName)) {
						anyReset = true;
					}
				}
				if (anyReset) scene.MarkDirty();
				if (anyReset) PushStructureUndo(scene, undo.Finish(scene, roots));
			}

			if (!revertFieldRequested.empty()) {
				StructureEditRecorder undo(scene, std::span(&prefabInstanceRoot, 1), "Revert Prefab Field");
				EntityHandle replacement = SceneSerializer::RevertPrefabInstanceOverride(
					scene, entity.GetHandle(), revertFieldRequested);
				if (replacement != entt::null) {
					const EntityHandle root = SceneSerializer::GetPrefabInstanceRoot(scene, replacement);
					PushStructureUndo(scene, undo.Finish(scene, std::span(&root, 1)));
					SelectEntity(replacement);
					m_EntityOrder.clear(); m_EntityOrderDirty = true;
					// Entity was recreated — `entity`/`entitySpan` are now stale.
					// Stop iterating components this frame (see latch above).
					selectionInvalidatedByRevert = true;
					if (open) ImGuiUtils::EndComponentSection();
					return;
				}
			}
			if (revertComponentRequested) {
				StructureEditRecorder undo(scene, std::span(&prefabInstanceRoot, 1), "Revert Prefab Component");
				const std::string prefix = componentSerializedName + ".";
				std::vector<std::string> paths;
				for (const auto& [path, _] : prefabOverrides.GetObject()) {
					if (path == componentSerializedName || path.compare(0, prefix.size(), prefix) == 0) {
						paths.push_back(path);
					}
				}
				EntityHandle current = entity.GetHandle();
				for (const std::string& p : paths) {
					EntityHandle replacement = SceneSerializer::RevertPrefabInstanceOverride(scene, current, p);
					if (replacement != entt::null) current = replacement;
				}
				SelectEntity(current);
				const EntityHandle root = SceneSerializer::GetPrefabInstanceRoot(scene, current);
				PushStructureUndo(scene, undo.Finish(scene, std::span(&root, 1)));
				m_EntityOrder.clear(); m_EntityOrderDirty = true;
				// Entity may have been recreated by the reverts above — `entity`/
				// `entitySpan` are now stale. Stop iterating components this frame.
				selectionInvalidatedByRevert = true;
				if (open) ImGuiUtils::EndComponentSection();
				return;
			}
			if (applyComponentRequested) {
				ApplyPrefabInstanceToSource(scene, entity.GetHandle());
				selectionInvalidatedByRevert = true;
				if (open) ImGuiUtils::EndComponentSection();
				return;
			}

			if (open) {
				DispatchComponentInspector(info, entitySpan);
				ImGuiUtils::EndComponentSection();
			}
		};

		// Each C# component and script gets its own section among the built-ins, unless the selection's scripts differ.
		ScriptSectionPass scriptPass;
		const bool scriptSectionsActive = BeginScriptSections(entitySpan, scriptPass) && scriptPass.MultiEdit;
		const ComponentOrderComponent* savedOrder = entity.HasComponent<ComponentOrderComponent>()
			? &entity.GetComponent<ComponentOrderComponent>() : nullptr;
		const InspectorSectionList sections = CollectInspectorSections(registry,
			scriptSectionsActive ? &entity.GetComponent<ScriptComponent>() : nullptr,
			savedOrder ? &savedOrder->Order : nullptr,
			[&](const std::type_index& typeId, const ComponentInfo&) {
				return m_InspectorCache.CommonComponentTypes.contains(typeId);
			});

		// COMPONENT_REF payloads carry the persistent UUID (not RuntimeID): reference fields write it to disk, RuntimeIDs are reallocated on reload.
		uint64_t refEntityId = scene.GetEntityPersistentID(entity.GetHandle());
		if (refEntityId == 0) refEntityId = entity.GetRuntimeID(); // fallback for entities with no UUIDComponent
		const std::string refPrefix = std::to_string(refEntityId) + ":";
		for (std::size_t i = 0; i < sections.Items.size() && !selectionInvalidatedByRevert; ++i) {
			const InspectorSection& section = sections.Items[i];
			if (!section.WholeScriptBlock) {
				ImGuiUtils::SetNextComponentSectionDrag({
					refPrefix + section.RefName,
					"Ref: " + entity.GetName() + "." + section.RefName,
					true,
					i > sections.Pinned,
					i >= sections.Pinned && i + 1 < sections.Items.size() });
			}
			if (section.ScriptKind == 1) DrawManagedComponentSection(scriptPass, section.ScriptIndex);
			else if (section.ScriptKind == 2) DrawScriptSection(scriptPass, section.ScriptIndex);
			else drawComponentSection(section.TypeId, *section.Info);
		}
		if (selectionInvalidatedByRevert) {
			ImGui::EndChild();
			ImGui::End();
			return;
		}
		if (scriptSectionsActive) EndScriptSections(scriptPass);

		// Copy: the inspector cache behind selectedHandles is rebuilt when components change.
		const std::vector<EntityHandle> undoHandles = selectedHandles;

		if (std::optional<ImGuiUtils::ComponentSectionMove> move = ImGuiUtils::TakeComponentSectionMove()) {
			const auto indexOf = [&](const std::string& payload) -> std::ptrdiff_t {
				for (std::size_t i = 0; i < sections.Items.size(); ++i) {
					if (refPrefix + sections.Items[i].RefName == payload) return static_cast<std::ptrdiff_t>(i);
				}
				return -1;
			};
			const std::ptrdiff_t target = indexOf(move->Target);
			const std::ptrdiff_t moved = move->Dragged.empty() ? target : indexOf(move->Dragged);
			// Move Up / Move Down go past the neighbouring section shown here (entities of a multi-selection may have more).
			const std::ptrdiff_t anchor = move->Dragged.empty() ? target + move->Delta : target;
			const bool after = move->Dragged.empty() ? move->Delta > 0 : move->After;
			const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(sections.Items.size());
			if (target >= 0 && moved >= 0 && anchor >= 0 && anchor < count && moved != anchor) {
				const std::string& movedKey = sections.Items[moved].Key;
				const std::string& anchorKey = sections.Items[anchor].Key;
				StructureEditRecorder recorder(scene, undoHandles, "Reorder Components");
				bool changed = false;
				for (const Entity& e : selectedEntities) {
					Entity subject = e;
					const ComponentOrderComponent* own = subject.HasComponent<ComponentOrderComponent>()
						? &subject.GetComponent<ComponentOrderComponent>() : nullptr;
					const InspectorSectionList ownSections = CollectInspectorSections(registry,
						subject.HasComponent<ScriptComponent>() ? &subject.GetComponent<ScriptComponent>() : nullptr,
						own ? &own->Order : nullptr,
						[&](const std::type_index&, const ComponentInfo& info) { return info.has && info.has(subject); });
					std::vector<std::string> keys;
					for (const InspectorSection& item : ownSections.Items) keys.push_back(item.Key);
					std::vector<std::string> reordered = ComponentOrder::Move(keys, movedKey, anchorKey, after, ownSections.Pinned);
					if (reordered == keys) continue;
					if (!subject.HasComponent<ComponentOrderComponent>()) subject.AddComponent<ComponentOrderComponent>();
					subject.GetComponent<ComponentOrderComponent>().Order = std::move(reordered);
					changed = true;
				}
				if (changed) {
					scene.MarkDirty();
					PushStructureUndo(scene, recorder.Finish(scene, undoHandles));
					++m_SelectionVersion;
				}
			}
		}

		if (pendingRemoval != typeid(void)) {
			StructureEditRecorder recorder(scene, undoHandles, "Remove Component");
			// Remove from EVERY selected entity that has it.
			registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
				if (typeId != pendingRemoval || !info.remove) return;
				for (const Entity& e : selectedEntities) {
					if (info.has(e)) info.remove(e);
				}
			});
			scene.MarkDirty();
			PushStructureUndo(scene, recorder.Finish(scene, undoHandles));
			// Component-presence map changed — the cached
			// CommonComponentTypes / PartialComponents must rebuild.
			++m_SelectionVersion;
		}

		if (!hiddenPartialComponents.empty()) {
			std::string text = "Hidden: ";
			for (std::size_t i = 0; i < hiddenPartialComponents.size(); ++i) {
				if (i > 0) text += ", ";
				text += hiddenPartialComponents[i];
			}
			text += " - not on all selected";
			ImGui::TextDisabled("%s", text.c_str());
		}

		ReferencePicker::RenderPopup();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		float buttonWidth = ImGui::GetContentRegionAvail().x;
		if (Icons::ButtonWithIcon(Icons::Type::Plus, "Add Component", ImVec2(buttonWidth, 0), true)) {
			ImGui::OpenPopup("AddComponentPopup");
			m_ComponentSearchBuffer[0] = '\0';
		}

		// MUST sit immediately after the button: placing it after RenderAddComponentPopup binds to a different LastItemData and silently no-ops.
		bool scriptDroppedSomething = false;
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
				StructureEditRecorder scriptRecorder(scene, undoHandles, "Add Script");
				// 1-arg form: see entity-row drop comment above.
				std::string droppedPath(static_cast<const char*>(payload->Data));
				std::vector<EditorScriptDiscovery::ScriptEntry> droppedScripts;
				EditorScriptDiscovery::CollectScriptFile(std::filesystem::path(droppedPath), droppedScripts);
				for (const auto& scriptEntry : droppedScripts) {
					if (scriptEntry.IsSceneSystem || scriptEntry.IsGlobalSystem) {
						continue;
					}
					for (const Entity& e : selectedEntities) {
						if (scriptEntry.IsManagedComponent) {
							scriptDroppedSomething |= AttachManagedComponentToEntity(const_cast<Entity&>(e), scene, scriptEntry);
						}
						else {
							scriptDroppedSomething |= AttachScriptToEntity(const_cast<Entity&>(e), scene, scriptEntry);
						}
					}
				}
				if (scriptDroppedSomething) PushStructureUndo(scene, scriptRecorder.Finish(scene, undoHandles));
			}
			ImGui::EndDragDropTarget();
		}

		bool addComponentChanged = false;
		std::optional<StructureEditRecorder> addRecorder;
		RenderAddComponentPopup("AddComponentPopup", scene, entitySpan,
			m_ComponentSearchBuffer, sizeof(m_ComponentSearchBuffer),
			&addComponentChanged,
			[&]() { addRecorder.emplace(scene, undoHandles, "Add Component"); });
		if (addComponentChanged && addRecorder) {
			PushStructureUndo(scene, addRecorder->Finish(scene, undoHandles));
		}
		if (addComponentChanged || scriptDroppedSomething) {
			++m_SelectionVersion;
		}
		ImGui::EndChild();

		// IsAnyItemActive fires on focus/click; ActiveIdHasBeenEditedThisFrame only fires on real value edits (drag step, keystroke).
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
			const ImGuiContext& g = *ImGui::GetCurrentContext();
			if (g.ActiveId != 0 && g.ActiveIdHasBeenEditedThisFrame) {
				scene.MarkDirty();
			}
		}
		m_IsInspectorPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		ImGui::End();
	}
}
