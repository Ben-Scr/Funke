#pragma once

#include <imgui.h>

#include <cmath>
#include <string_view>

namespace Funke::EditorWindowIcons {

	enum class Type {
		None, Entities, Inspector, Scene, Game, Project, Log, Animation, Animator,
		Sprite, Build, BuildProfiles, Settings, Preferences, Package, Profiler, Script,
		Save, Quit, Reload, Undo, Redo, Search, View, Layout, Add, Entity2D, UI, Camera, Audio, File, Data, SceneFile, Paste,
		Json, Material, TextFile, BinaryFile
	};

	inline Type ForWindow(std::string_view name) {
		if (name.find("###ScriptWindow_") != std::string_view::npos) return Type::Script;
		if (name == "Entities") return Type::Entities;
		if (name == "Inspector") return Type::Inspector;
		if (name == "Editor View") return Type::Scene;
		if (name == "Game View") return Type::Game;
		if (name == "Project") return Type::Project;
		if (name == "Log") return Type::Log;
		if (name == "Animation") return Type::Animation;
		if (name == "Animator") return Type::Animator;
		if (name == "Sprite Editor") return Type::Sprite;
		if (name == "Build") return Type::Build;
		if (name == "Build Profiles") return Type::BuildProfiles;
		if (name == "Project Settings") return Type::Settings;
		if (name == "Editor Preferences") return Type::Preferences;
		if (name == "Profiler") return Type::Profiler;
		if (name == "Package Manager" || name == "Create new package" ||
			name == "Install package from git URL" || name == "Install package from NuGet") return Type::Package;
		return Type::None;
	}

	using TextureProvider = ImTextureID(*)(const char*, int);
	inline TextureProvider g_TextureProvider = nullptr;

	inline void SetTextureProvider(TextureProvider provider) {
		g_TextureProvider = provider;
	}

	inline const char* AssetName(Type type) {
		switch (type) {
		case Type::Entities: return "ui_entities";
		case Type::Inspector: return "ui_inspector";
		case Type::Scene: return "entity";
		case Type::Game: return "ui_game";
		case Type::Project: return "ui_folder";
		case Type::Log: return "ui_log";
		case Type::Animation: return "ui_animation";
		case Type::Animator: return "ui_animator";
		case Type::Sprite: return "image";
		case Type::Build: return "hammer";
		case Type::BuildProfiles: return "ui_build_profiles";
		case Type::Settings: return "settings_gear";
		case Type::Preferences: return "settings";
		case Type::Package: return "ui_package";
		case Type::Profiler: return "ui_profiler";
		case Type::Script: return "ui_script";
		case Type::Save: return "save";
		case Type::Quit: return "ui_quit";
		case Type::Reload: return "reset";
		case Type::Undo: return "undo";
		case Type::Redo: return "redo";
		case Type::Search: return "search";
		case Type::View: return "eye_open";
		case Type::Layout: return "ui_layout";
		case Type::Add: return "plus";
		case Type::Entity2D: return "ui_entity_2d";
		case Type::UI: return "ui_layout";
		case Type::Camera: return "ui_camera";
		case Type::Audio: return "ui_audio";
		case Type::File: return "ui_file";
		case Type::Data: return "ui_data";
		case Type::SceneFile: return "ui_scene_file";
		case Type::Paste: return "ui_paste";
		case Type::Json: return "ui_json";
		case Type::Material: return "ui_material";
		case Type::TextFile: return "ui_text";
		case Type::BinaryFile: return "ui_binary";
		case Type::None: return nullptr;
		}
		return nullptr;
	}

	inline float DrawIcon(Type type, ImDrawList* drawList, const ImVec2& pos, float size, ImU32 color) {
		const char* asset = AssetName(type);
		if (!asset || size <= 0.0f) return 0.0f;
		const float advance = size * 1.3f;
		if (drawList && g_TextureProvider) {
			const ImTextureID texture = g_TextureProvider(asset, static_cast<int>(std::ceil(size)));
			if (texture) drawList->AddImage(texture, pos, ImVec2(pos.x + size, pos.y + size),
				ImVec2(0, 1), ImVec2(1, 0), color);
		}
		return advance;
	}

	inline float Draw(const char* name, ImDrawList* drawList, const ImVec2& pos, float size, ImU32 color) {
		return DrawIcon(ForWindow(name ? name : ""), drawList, pos, size, color);
	}
}
