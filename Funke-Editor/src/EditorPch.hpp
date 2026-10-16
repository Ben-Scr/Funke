#pragma once
// Precompiled header for Funke-Editor.
//
// The editor used to precompile Funke-Engine's pch.hpp directly. That PCH is
// shared with the engine DLL, so it deliberately carries only the STL + the
// Core/ headers and stops short of ImGui and the ECS surface — the engine
// itself must not pull ImGui into every TU. The editor has the opposite
// profile: 23 of its 32 TUs include <imgui.h>, 24 include Collections/Vec2.hpp
// and 18 reach Scene/Scene.hpp (and through it entt::registry). Every one of
// those TUs was reparsing that surface on its own.
//
// Measured over the transitive include closure of all 32 editor TUs, moving
// the headers below into the PCH takes the editor from ~1,056k lines parsed
// per full build to ~422k, a 60% reduction. The PCH itself grows from ~22k to
// ~53k lines, which is paid once per build instead of once per TU.
//
// Rule for adding to this list: the header must be (a) included by a large
// share of editor TUs and (b) stable. Editor-local headers that churn during
// normal feature work — Gui/ImGuiUtils.hpp is the tempting one — are
// deliberately excluded: ImGuiUtils.hpp would buy only a further ~0.7% while
// making every edit to it invalidate the PCH and rebuild all 32 TUs.
#include "pch.hpp"

// ── ImGui ─────────────────────────────────────────────────────────────
// 23/32 editor TUs. The editor is an ImGui application; this is the single
// most widely shared non-STL header it has.
#include <imgui.h>

// ── Math / small value types ──────────────────────────────────────────
// Vec2.hpp is the carrier for <glm/vec2.hpp> + <glm/vec3.hpp> (24/32 TUs);
// Color.hpp rides along at near-zero marginal cost (25/32).
#include <Collections/Color.hpp>
#include <Collections/Vec2.hpp>
// vec4 is not reached through Vec2.hpp; 11/32 TUs include it themselves.
// Vendored and never edited, so it costs no PCH invalidations.
#include <glm/vec4.hpp>

// ── ECS / scene surface ───────────────────────────────────────────────
// SceneManager.hpp pulls Scene.hpp -> Entity.hpp and, with it, the complete
// entt::registry (~12.4k lines on its own). This is the largest single win in
// the list. Note the engine's own pch.hpp intentionally does NOT include this.
#include <Scene/SceneManager.hpp>

// ── Application / project / assets ────────────────────────────────────
#include <Assets/AssetRegistry.hpp>
#include <Core/Application.hpp>
#include <Project/FunkeProject.hpp>
