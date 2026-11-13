#pragma once

#include "Graphics/RenderViewOptions.hpp"
#include "Serialization/Json.hpp"
#include <array>
#include <utility>

namespace Funke {

	struct EditorViewSettings {
		RenderViewOptions Rendering;
		bool ShowUI = true;
		bool ShowGrid = true;

		bool operator==(const EditorViewSettings&) const = default;

		Json::Value ToJson() const {
			Json::Value value = Json::Value::MakeObject();
			for (const auto& [name, member] : RenderingMembers())
				value.AddMember(name, Json::Value(Rendering.*member));
			value.AddMember("ShowUI", Json::Value(ShowUI));
			value.AddMember("ShowGrid", Json::Value(ShowGrid));
			// Shader exclusions are project-specific and intentionally last only for this editor session.
			return value;
		}

		static EditorViewSettings FromJson(const Json::Value& value) {
			EditorViewSettings settings;
			if (!value.IsObject()) return settings;
			for (const auto& [name, member] : RenderingMembers())
				if (const auto* flag = value.FindMember(name); flag && flag->IsBool()) settings.Rendering.*member = flag->AsBoolOr(true);
			if (const auto* flag = value.FindMember("ShowUI"); flag && flag->IsBool()) settings.ShowUI = flag->AsBoolOr(true);
			if (const auto* flag = value.FindMember("ShowGrid"); flag && flag->IsBool()) settings.ShowGrid = flag->AsBoolOr(true);
			return settings;
		}

	private:
		using Member = std::pair<const char*, bool RenderViewOptions::*>;
		static const std::array<Member, 7>& RenderingMembers() {
			static const std::array<Member, 7> members = {{
				{ "Lighting", &RenderViewOptions::Lighting }, { "Shadows", &RenderViewOptions::Shadows },
				{ "Fog", &RenderViewOptions::Fog }, { "Skybox", &RenderViewOptions::Skybox },
				{ "CustomShaders", &RenderViewOptions::CustomShaders }, { "Particles", &RenderViewOptions::Particles },
				{ "SceneEffects", &RenderViewOptions::SceneEffects },
			}};
			return members;
		}
	};

}
