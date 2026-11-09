#include <pch.hpp>
#include "Gui/GlassBackdrop.hpp"
#include "Gui/EditorTheme.hpp"
#include "Editor/EditorPreferences.hpp"

#include "Core/Platform/Win32Titlebar.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Funke::GlassBackdrop {

	namespace {
		struct Blob {
			ImVec2 Center; // main-viewport UV
			ImVec2 Drift;  // UV amplitude
			ImVec2 Speed;  // rad/s; periods of one to two minutes keep the motion out of the way
			float Phase;
			float Radius;  // fraction of the main viewport's longer edge
			ImVec4 Color;
		};

		// Kept to the edges: the Editor View covers the middle of the main window.
		constexpr Blob k_Blobs[] = {
			{ { 0.16f, 0.20f }, { 0.10f, 0.08f }, { 0.071f, 0.053f }, 0.0f, 0.50f, { 0.23f, 0.42f, 1.00f, 0.82f } },
			{ { 0.84f, 0.18f }, { 0.08f, 0.10f }, { 0.047f, 0.083f }, 1.7f, 0.44f, { 0.54f, 0.30f, 1.00f, 0.72f } },
			{ { 0.66f, 0.88f }, { 0.12f, 0.06f }, { 0.061f, 0.039f }, 3.1f, 0.46f, { 0.10f, 0.76f, 0.79f, 0.58f } },
			{ { 0.22f, 0.84f }, { 0.09f, 0.07f }, { 0.043f, 0.067f }, 4.4f, 0.38f, { 0.91f, 0.31f, 0.56f, 0.48f } },
			// Funke amber, kept faint: the warm logo colours are for branding, not for anything interactive.
			{ { 0.97f, 0.98f }, { 0.05f, 0.05f }, { 0.057f, 0.049f }, 5.6f, 0.30f, { 1.00f, 0.59f, 0.34f, 0.32f } },
		};

		// Base wash corners: top-left, top-right, bottom-right, bottom-left.
		constexpr ImVec4 k_BaseCorners[4] = {
			{ 0.045f, 0.060f, 0.125f, 1.0f },
			{ 0.075f, 0.050f, 0.140f, 1.0f },
			{ 0.060f, 0.040f, 0.110f, 1.0f },
			{ 0.035f, 0.070f, 0.095f, 1.0f },
		};

		constexpr ImVec4 k_LightCorners[4] = {
			{ 0.76f, 0.87f, 0.96f, 1.0f },
			{ 0.88f, 0.83f, 0.96f, 1.0f },
			{ 0.80f, 0.92f, 0.95f, 1.0f },
			{ 0.92f, 0.89f, 0.96f, 1.0f },
		};

		constexpr int k_Segments = 64;
		constexpr int k_Rings = 8;
		constexpr float k_TwoPi = 6.28318530718f;

		// Ringed fan with a Gaussian alpha falloff; a plain fan's linear falloff shows a cone tip in the middle.
		void DrawBlob(ImDrawList* dl, ImVec2 center, ImVec2 radius, const ImVec4& color) {
			constexpr int vtxCount = 1 + k_Rings * k_Segments;
			constexpr int idxCount = k_Segments * 3 + (k_Rings - 1) * k_Segments * 6;
			dl->PrimReserve(idxCount, vtxCount);

			const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
			const unsigned int base = dl->_VtxCurrentIdx;
			dl->PrimWriteVtx(center, uv, ImGui::ColorConvertFloat4ToU32(color));
			for (int ring = 1; ring <= k_Rings; ++ring) {
				const float t = static_cast<float>(ring) / k_Rings;
				const float alpha = ring == k_Rings ? 0.0f : color.w * std::exp(-3.5f * t * t);
				const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, alpha));
				for (int s = 0; s < k_Segments; ++s) {
					const float angle = k_TwoPi * static_cast<float>(s) / k_Segments;
					dl->PrimWriteVtx(ImVec2(center.x + std::cos(angle) * radius.x * t, center.y + std::sin(angle) * radius.y * t), uv, col);
				}
			}

			auto ringVtx = [base](int ring, int segment) {
				return static_cast<ImDrawIdx>(base + 1 + (ring - 1) * k_Segments + segment % k_Segments);
			};
			for (int s = 0; s < k_Segments; ++s) {
				dl->PrimWriteIdx(static_cast<ImDrawIdx>(base));
				dl->PrimWriteIdx(ringVtx(1, s));
				dl->PrimWriteIdx(ringVtx(1, s + 1));
			}
			for (int ring = 1; ring < k_Rings; ++ring) {
				for (int s = 0; s < k_Segments; ++s) {
					const ImDrawIdx a = ringVtx(ring, s);
					const ImDrawIdx b = ringVtx(ring, s + 1);
					const ImDrawIdx c = ringVtx(ring + 1, s);
					const ImDrawIdx d = ringVtx(ring + 1, s + 1);
					dl->PrimWriteIdx(a); dl->PrimWriteIdx(b); dl->PrimWriteIdx(d);
					dl->PrimWriteIdx(a); dl->PrimWriteIdx(d); dl->PrimWriteIdx(c);
				}
			}
		}

		void DrawRim(ImDrawList* dl, const ImRect& rect, float rounding, float response, float press) {
			if (rect.GetWidth() < 4.0f || rect.GetHeight() < 4.0f) return;
			const bool dark = EditorTheme::IsDark();
			const float scale = ImGui::GetFontSize() / 15.0f;
			if (rect.GetHeight() <= ImGui::GetFrameHeight() * 1.5f) {
			const int sheenStart = dl->VtxBuffer.Size;
			dl->AddRectFilled(rect.Min, rect.Max, IM_COL32_WHITE, rounding);
			for (int i = sheenStart; i < dl->VtxBuffer.Size; ++i) {
				ImDrawVert& vertex = dl->VtxBuffer[i];
				const float coverage = (vertex.col >> IM_COL32_A_SHIFT) / 255.0f;
				const float t = std::clamp((vertex.pos.y - rect.Min.y) / rect.GetHeight(), 0.0f, 1.0f);
				const float alpha = (dark ? 0.045f : 0.10f) * (1.0f - t) * (1.0f - press * 0.7f);
				vertex.col = ImGui::GetColorU32(ImVec4(0.90f, 0.96f, 1.0f, alpha * coverage));
			}
			}
			const int first = dl->VtxBuffer.Size;
			dl->AddRect(rect.Min, rect.Max, IM_COL32_WHITE, rounding, 0, std::max(1.0f, scale));
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			const float reach = std::max(50.0f * scale, std::min(rect.GetWidth(), 240.0f * scale));
			for (int i = first; i < dl->VtxBuffer.Size; ++i) {
				ImDrawVert& vertex = dl->VtxBuffer[i];
				const float edgeAlpha = (vertex.col >> IM_COL32_A_SHIFT) / 255.0f;
				const float t = std::clamp((vertex.pos.y - rect.Min.y) / rect.GetHeight(), 0.0f, 1.0f);
				float reflection = 0.0f;
				if (response > 0.001f && ImGui::IsMousePosValid(&mouse)) {
					const float dx = (vertex.pos.x - mouse.x) / reach;
					const float dy = (vertex.pos.y - mouse.y) / reach;
					reflection = std::exp(-2.0f * (dx * dx + dy * dy)) * response;
				}
				const float alpha = ((dark ? 0.36f : 0.70f) * (1.0f - t) + 0.08f
					+ reflection * 0.35f) * (1.0f - press * 0.40f);
				vertex.col = ImGui::GetColorU32(ImVec4(0.80f + 0.20f * (1.0f - t),
					0.90f + 0.10f * (1.0f - t), 1.0f, alpha * edgeAlpha));
			}
			const float inset = std::max(1.0f, scale);
			dl->AddRect(ImVec2(rect.Min.x + inset, rect.Min.y + inset),
				ImVec2(rect.Max.x - inset, rect.Max.y - inset),
				ImGui::GetColorU32(ImVec4(0.05f, 0.16f, 0.30f, (dark ? 0.16f : 0.12f) + press * 0.12f)),
				std::max(0.0f, rounding - inset));
		}

	}

	ImVec4 TitlebarColor(bool focused) {
		if (!EditorTheme::IsDark()) {
			return focused ? ImVec4(0.86f, 0.92f, 0.98f, 1) : ImVec4(0.91f, 0.94f, 0.98f, 1);
		}
		return focused ? ImVec4(0.055f, 0.085f, 0.16f, 1) : ImVec4(0.05f, 0.07f, 0.12f, 1);
	}

	void SyncViewportCaptions(bool glass) {
		// Viewport ID -> HWND it was tinted on; a recreated platform window needs the colour again.
		static std::unordered_map<ImGuiID, void*> s_Tinted;
		static bool s_WasDark = true;
		const bool dark = EditorTheme::IsDark();
		if (!glass && s_Tinted.empty()) return;

		const ImVec4 c = TitlebarColor(true);
		const Color caption(c.x, c.y, c.z, glass ? 1.0f : 0.0f);
		const Color none(0.0f, 0.0f, 0.0f, 0.0f);
		const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		const Color captionText(text.x, text.y, text.z, 1.0f);
		const ImGuiViewport* main = ImGui::GetMainViewport();

		std::unordered_map<ImGuiID, void*> live;
		for (ImGuiViewport* viewport : ImGui::GetPlatformIO().Viewports) {
			if (viewport == main || !viewport->PlatformHandleRaw || (viewport->Flags & ImGuiViewportFlags_NoDecoration)) {
				continue;
			}
			const auto it = s_Tinted.find(viewport->ID);
			const bool tinted = it != s_Tinted.end() && it->second == viewport->PlatformHandleRaw;
			if (glass != tinted || (glass && dark != s_WasDark)) {
				Win32Titlebar::SetColors(viewport->PlatformHandleRaw, caption, glass ? captionText : none, caption);
			}
			if (glass) {
				live.emplace(viewport->ID, viewport->PlatformHandleRaw);
			}
		}
		s_Tinted = std::move(live);
		s_WasDark = dark;
	}

	void Draw() {
		ImGuiViewport* main = ImGui::GetMainViewport();
		if (!main || main->Size.x <= 0.0f || main->Size.y <= 0.0f || (main->Flags & ImGuiViewportFlags_IsMinimized)) {
			return;
		}

		ImDrawList* dl = ImGui::GetBackgroundDrawList(main);
		const bool dark = EditorTheme::IsDark();
		const ImVec4* corners = dark ? k_BaseCorners : k_LightCorners;
		const ImVec2 min = main->Pos;
		const ImVec2 max(min.x + main->Size.x, min.y + main->Size.y);
		dl->AddRectFilledMultiColor(min, max,
			ImGui::ColorConvertFloat4ToU32(corners[0]),
			ImGui::ColorConvertFloat4ToU32(corners[1]),
			ImGui::ColorConvertFloat4ToU32(corners[2]),
			ImGui::ColorConvertFloat4ToU32(corners[3]));

		const ImGuiIO& io = ImGui::GetIO();
		static ImVec2 s_Pointer(0.5f, 0.5f);
		static float s_Response = 0.0f;
		const bool inside = !io.AppFocusLost && ImGui::IsMousePosValid()
			&& ImRect(min, max).Contains(io.MousePos);
		const float ease = 1.0f - std::exp(-std::min(io.DeltaTime, 0.1f) * 7.0f);
		const ImVec2 target = inside ? ImVec2((io.MousePos.x - min.x) / main->Size.x,
			(io.MousePos.y - min.y) / main->Size.y) : ImVec2(0.5f, 0.5f);
		s_Pointer.x += (target.x - s_Pointer.x) * ease;
		s_Pointer.y += (target.y - s_Pointer.y) * ease;
		s_Response += ((inside ? 1.0f : 0.0f) - s_Response) * ease;
		const double time = ImGui::GetTime();
		const float extent = std::max(main->Size.x, main->Size.y);
		for (const Blob& blob : k_Blobs) {
			const float sx = static_cast<float>(std::sin(time * blob.Speed.x + blob.Phase));
			const float sy = static_cast<float>(std::sin(time * blob.Speed.y + blob.Phase * 1.3));
			const ImVec2 center(
				min.x + (blob.Center.x + blob.Drift.x * sx + (s_Pointer.x - 0.5f) * 0.035f) * main->Size.x,
				min.y + (blob.Center.y + blob.Drift.y * sy + (s_Pointer.y - 0.5f) * 0.035f) * main->Size.y);
			// Radii breathe out of phase, so the blobs morph instead of only sliding.
			const float r = blob.Radius * extent;
			const ImVec2 radius(
				r * (1.0f + 0.14f * static_cast<float>(std::sin(time * blob.Speed.y * 1.7 + blob.Phase))),
				r * (1.0f + 0.14f * static_cast<float>(std::cos(time * blob.Speed.x * 1.3 + blob.Phase))));
			ImVec4 color = blob.Color;
			color.w *= dark ? 0.72f : 0.22f;
			DrawBlob(dl, center, radius, color);
		}
		for (int i = 0; i < 3; ++i) {
			const float phase = static_cast<float>(time * 0.045) + i * 2.1f;
			const ImVec2 center(min.x + main->Size.x * (0.20f + i * 0.30f + 0.06f * std::sin(phase)),
				min.y + main->Size.y * (0.50f + 0.28f * std::cos(phase)));
			DrawBlob(dl, center, ImVec2(extent * 0.42f, extent * 0.055f),
				ImVec4(0.72f, 0.90f, 1.0f, dark ? 0.12f : 0.32f));
		}
		DrawBlob(dl, ImVec2(min.x + s_Pointer.x * main->Size.x, min.y + s_Pointer.y * main->Size.y),
			ImVec2(extent * 0.22f, extent * 0.18f), ImVec4(0.72f, 0.88f, 1.0f, s_Response * (dark ? 0.18f : 0.32f)));

	}

	bool IsEnabled() {
		const EditorThemeMode mode = EditorPreferences::GetThemeMode();
		return mode == EditorThemeMode::Glass || mode == EditorThemeMode::GlassLight;
	}

	void DrawWindowEdges() {
		if (!IsEnabled()) return;
		ImGuiContext& context = *ImGui::GetCurrentContext();
		for (ImGuiWindow* window : context.Windows) {
			if (!window->Active || window->Hidden || window->IsFallbackWindow || window->DockNodeAsHost
				|| (window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_Tooltip))) continue;
			const ImRect rect(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y));
			window->DrawList->PushClipRect(rect.Min, rect.Max, false);
			DrawRim(window->DrawList, ImRect(ImVec2(rect.Min.x + 0.5f, rect.Min.y + 0.5f),
				ImVec2(rect.Max.x - 0.5f, rect.Max.y - 0.5f)), window->WindowRounding,
				context.HoveredWindow == window ? 1.0f : 0.0f, 0.0f);
			window->DrawList->PopClipRect();
		}
	}

	void DrawLastItemSurface(ImVec2 min, ImVec2 max) {
		if (!IsEnabled() || !ImGui::IsItemVisible()) return;
		const ImGuiContext& context = *ImGui::GetCurrentContext();
		const ImGuiID id = ImGui::GetItemID();
		if (!id) return;
		const bool disabled = (context.LastItemData.ItemFlags & ImGuiItemFlags_Disabled) != 0;
		const bool hovered = !disabled && (ImGui::IsItemHovered() || ImGui::IsItemFocused());
		const bool held = !disabled && ImGui::IsItemActive();
		ImGuiStorage* storage = ImGui::GetStateStorage();
		const ImGuiID hoverId = ImHashStr("##GlassHover", 0, id);
		const ImGuiID pressId = ImHashStr("##GlassPress", 0, id);
		const ImGuiID frameId = ImHashStr("##GlassFrame", 0, id);
		const bool continuous = storage->GetInt(frameId, -2) == ImGui::GetFrameCount() - 1;
		const float ease = 1.0f - std::exp(-std::min(ImGui::GetIO().DeltaTime, 0.1f) * 16.0f);
		float response = continuous ? storage->GetFloat(hoverId) : 0.0f;
		float press = continuous ? storage->GetFloat(pressId) : 0.0f;
		response += ((hovered ? 1.0f : 0.0f) - response) * ease;
		press += ((held ? 1.0f : 0.0f) - press) * ease;
		storage->SetFloat(hoverId, response);
		storage->SetFloat(pressId, press);
		storage->SetInt(frameId, ImGui::GetFrameCount());
		DrawRim(ImGui::GetWindowDrawList(), ImRect(min, max),
			ImGui::GetStyle().FrameRounding, response, press);
	}

	void DrawLastItemSurface() {
		DrawLastItemSurface(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
	}

}
