#pragma once

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string_view>

namespace Funke::EditorWindowIcons {

	enum class Type {
		None, Entities, Inspector, Scene, Game, Project, Log, Animation, Animator,
		Sprite, Build, BuildProfiles, Settings, Preferences, Package, Profiler, Script
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

	inline float Draw(const char* name, ImDrawList* drawList, const ImVec2& pos, float size, ImU32 color) {
		const Type type = ForWindow(name ? name : "");
		if (type == Type::None) return 0.0f;
		const float advance = size + size * 0.3f;
		if (!drawList) return advance;

		const float stroke = std::max(1.0f, std::floor(size / 12.0f));
		const float offset = std::fmod(stroke, 2.0f) * 0.5f;
		const auto point = [&](float x, float y) {
			return ImVec2(
				std::round(pos.x + size * x / 16.0f - offset) + offset,
				std::round(pos.y + size * y / 16.0f - offset) + offset);
		};
		// PathStroke uses these coordinates directly; AddLine would add another half pixel.
		const auto path = [&](std::initializer_list<ImVec2> vertices, bool closed = false) {
			for (const ImVec2& vertex : vertices) drawList->PathLineTo(point(vertex.x, vertex.y));
			drawList->PathStroke(color, closed ? ImDrawFlags_Closed : 0, stroke);
		};
		const auto rect = [&](float left, float top, float right, float bottom) {
			path({ {left, top}, {right, top}, {right, bottom}, {left, bottom} }, true);
		};
		const auto fill = [&](std::initializer_list<ImVec2> vertices) {
			for (const ImVec2& vertex : vertices) drawList->PathLineTo(point(vertex.x, vertex.y));
			drawList->PathFillConvex(color);
		};

		switch (type) {
		case Type::Entities:
			rect(2, 2, 6, 6);
			rect(10, 10, 14, 14);
			path({ {4, 6}, {4, 12}, {10, 12} });
			path({ {10, 3}, {14, 3} });
			path({ {10, 6}, {14, 6} });
			break;
		case Type::Inspector:
			path({ {2, 4}, {14, 4} });
			path({ {2, 8}, {14, 8} });
			path({ {2, 12}, {14, 12} });
			path({ {5, 2}, {5, 6} });
			path({ {11, 6}, {11, 10} });
			path({ {6, 10}, {6, 14} });
			break;
		case Type::Scene:
			path({ {8, 1}, {14, 4}, {14, 11}, {8, 14}, {2, 11}, {2, 4} }, true);
			path({ {2, 4}, {8, 7}, {14, 4} });
			path({ {8, 7}, {8, 14} });
			break;
		case Type::Game:
			rect(1, 2, 15, 11);
			path({ {8, 11}, {8, 14} });
			path({ {5, 14}, {11, 14} });
			fill({ {6, 4}, {10, 6.5f}, {6, 9} });
			break;
		case Type::Project:
			path({ {2, 3}, {6, 3}, {8, 5}, {14, 5}, {14, 13}, {2, 13} }, true);
			break;
		case Type::Log:
			path({ {6, 3}, {2, 7}, {6, 11} });
			path({ {8, 12}, {14, 12} });
			break;
		case Type::Animation:
			path({ {2, 2}, {2, 13}, {14, 13} });
			path({ {8, 4}, {11, 7}, {8, 10}, {5, 7} }, true);
			break;
		case Type::Animator:
			rect(1, 2, 6, 6);
			rect(10, 10, 15, 14);
			path({ {6, 4}, {12, 4}, {12, 8} });
			break;
		case Type::Sprite:
			rect(2, 2, 14, 14);
			fill({ {4, 10}, {7, 7}, {10, 10} });
			fill({ {7, 12}, {11, 7}, {14, 12} });
			break;
		case Type::Build:
			path({ {2, 13}, {8, 7} });
			path({ {6, 4}, {9, 1}, {15, 7}, {12, 10} }, true);
			break;
		case Type::BuildProfiles:
			rect(4, 4, 13, 14);
			path({ {1, 11}, {1, 1}, {10, 1} });
			path({ {7, 7}, {10, 7} });
			path({ {7, 10}, {10, 10} });
			break;
		case Type::Settings: {
			const ImVec2 center = point(8, 8);
			for (int tooth = 0; tooth < 6; ++tooth) {
				for (int corner = 0; corner < 4; ++corner) {
					const float angle = (tooth * 4 + corner) * 6.2831853f / 24.0f;
					const float radius = (corner == 1 || corner == 2 ? 6.5f : 5.0f) * size / 16.0f;
					drawList->PathLineTo(ImVec2(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius));
				}
			}
			drawList->PathStroke(color, ImDrawFlags_Closed, stroke);
			drawList->AddCircle(center, size * 2.0f / 16.0f + 0.5f, color, 16, stroke);
			break;
		}
		case Type::Preferences:
			path({ {4, 1}, {4, 5} });
			rect(2, 5, 6, 8);
			path({ {4, 8}, {4, 14} });
			path({ {12, 1}, {12, 9} });
			rect(10, 9, 14, 12);
			path({ {12, 12}, {12, 14} });
			break;
		case Type::Package:
			rect(2, 4, 14, 14);
			path({ {2, 4}, {4, 1}, {12, 1}, {14, 4} });
			path({ {6, 7}, {10, 7} });
			break;
		case Type::Profiler:
			path({ {1, 8}, {4, 8}, {6, 2}, {9, 13}, {12, 6}, {14, 8} });
			break;
		case Type::Script:
			path({ {5, 3}, {1, 7}, {5, 11} });
			path({ {11, 3}, {15, 7}, {11, 11} });
			path({ {9, 1}, {7, 13} });
			break;
		case Type::None: break;
		}
		return advance;
	}
}
