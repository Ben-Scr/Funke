// NOT compiled into engine.dll — Funke-Editor and Funke-Runtime each include this .cpp via their own premake files{} entries.

#include "Diagnostics/StatsOverlay.hpp"

#include "Audio/AudioManager.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Core/Window.hpp"
#if FUNKE_WITH_2D
#include "Graphics/Renderer2D.hpp"
#endif
#include "Scene/SceneManager.hpp"

#include <imgui.h>

#include <cstdio>
#include <iterator>
#include <string>

#ifdef FNK_PLATFORM_WINDOWS
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <psapi.h>
#  pragma comment(lib, "psapi.lib")
#endif

namespace Funke::Diagnostics {

	namespace {
		std::string FormatBytesAsMb(std::size_t bytes) {
			const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%.1f MB", mb);
			return std::string(buf);
		}

		// Thousands separators keep seven-digit triangle counts readable.
		std::string FormatCount(std::uint64_t value) {
			std::string digits = std::to_string(value);
			for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(digits.size()) - 3; i > 0; i -= 3) {
				digits.insert(static_cast<std::size_t>(i), 1, ',');
			}
			return digits;
		}

		std::size_t CountLoadedEntities() {
			std::size_t count = 0;
			SceneManager::ForeachLoadedScene([&](const Scene& scene) {
				count += scene.GetEntityCount();
			});
			return count;
		}
	}

	bool StatsOverlay::RefreshIfDue(int screenWidth, int screenHeight, const RenderGeometryStats* frameGeometry) {
		const auto now = std::chrono::steady_clock::now();
		constexpr auto k_RefreshInterval = std::chrono::milliseconds(33); // ~30 Hz
		const bool firstCall = m_LastRefresh.time_since_epoch().count() == 0;
		if (!firstCall && (now - m_LastRefresh) < k_RefreshInterval) {
			// Always honour the latest screen size even on a no-refresh tick
			// so the displayed "Screen" line tracks resizes immediately
			// instead of lagging by up to 33 ms.
			m_Cached.ScreenWidth  = screenWidth;
			m_Cached.ScreenHeight = screenHeight;
			return false;
		}
		m_LastRefresh = now;

		auto* app = Application::GetInstance();
		// Stats reflect real wall-clock performance — use unscaled dt so TimeScale
		// doesn't distort the FPS / CPU readouts.
		const float dt = Time::GetUnscaledDeltaTime();
		m_Cached.Fps       = (dt > 0.0f) ? (1.0f / dt) : 0.0f;
		m_Cached.CpuMainMs = dt * 1000.0f;

#if FUNKE_WITH_2D
		if (auto* renderer = app ? app->GetRenderer2D() : nullptr) {
			m_Cached.RenderThreadMs = renderer->GetRenderLoopDuration();
		}
		else
#endif
		{
			m_Cached.RenderThreadMs = 0.0f;
		}

		if (frameGeometry) {
			m_Cached.Tris       = frameGeometry->Triangles;
			m_Cached.Verts      = frameGeometry->Vertices;
			m_Cached.ShadowTris = frameGeometry->ShadowTriangles;
		}
		else {
			// The renderers count across frames: average the frames since the last refresh. A frame that has not
			// advanced (first refresh, paused loop) keeps the previous numbers.
			const RenderGeometryStats totals = CaptureRenderGeometryTotals();
			const int frame = Time::GetFrameCount();
			if (m_LastGeometryFrame >= 0 && frame > m_LastGeometryFrame) {
				const RenderGeometryStats perFrame = (totals - m_LastGeometryTotals).PerFrame(static_cast<std::uint64_t>(frame - m_LastGeometryFrame));
				m_Cached.Tris       = perFrame.Triangles;
				m_Cached.Verts      = perFrame.Vertices;
				m_Cached.ShadowTris = perFrame.ShadowTriangles;
			}
			if (frame != m_LastGeometryFrame) {
				m_LastGeometryTotals = totals;
				m_LastGeometryFrame = frame;
			}
		}

		m_Cached.ScreenWidth  = screenWidth;
		m_Cached.ScreenHeight = screenHeight;

#ifdef FNK_PLATFORM_WINDOWS
		PROCESS_MEMORY_COUNTERS_EX pmc{};
		pmc.cb = sizeof(pmc);
		if (GetProcessMemoryInfo(
			GetCurrentProcess(),
			reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
			sizeof(pmc))) {
			m_Cached.ProcessWorkingSetBytes    = static_cast<std::size_t>(pmc.WorkingSetSize);
			m_Cached.ProcessPrivateCommitBytes = static_cast<std::size_t>(pmc.PrivateUsage);
		}
#else
		m_Cached.ProcessWorkingSetBytes    = 0;
		m_Cached.ProcessPrivateCommitBytes = 0;
#endif

		m_Cached.AudioPlaying = AudioManager::IsInitialized()
			? AudioManager::GetActiveSoundCount() : 0u;
		m_Cached.Entities = CountLoadedEntities();

		return true;
	}

	void StatsOverlay::RenderBody() const {
		const auto& s = m_Cached;
		ImGui::TextUnformatted("Statistics");
		ImGui::Separator();
		ImGui::Text("FPS:             %.1f", s.Fps);
		ImGui::Text("CPU main:        %.1f ms", s.CpuMainMs);
		ImGui::Text("Render thread:   %.1f ms", s.RenderThreadMs);
		ImGui::Text("Tris:            %s", FormatCount(s.Tris).c_str());
		ImGui::Text("Verts:           %s", FormatCount(s.Verts).c_str());
		if (s.ShadowTris > 0) {
			ImGui::Text("Shadow tris:     %s", FormatCount(s.ShadowTris).c_str());
		}
		ImGui::Separator();
		ImGui::Text("Screen:          %d x %d", s.ScreenWidth, s.ScreenHeight);
		ImGui::Separator();
		ImGui::Text("Working set:     %s", FormatBytesAsMb(s.ProcessWorkingSetBytes).c_str());
		ImGui::Text("Private commit:  %s", FormatBytesAsMb(s.ProcessPrivateCommitBytes).c_str());
		ImGui::Separator();
		ImGui::Text("Audio Playing:   %u", s.AudioPlaying);
		ImGui::Text("Entities:        %zu", s.Entities);
	}

	namespace {
		constexpr ImGuiWindowFlags k_OverlayFlags =
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoMove;
	}

	float StatsOverlay::RenderOverlayWindow(const char* uniqueId, const ImVec2& topRight, float yOffset) const {
		const ImVec2 overlayPos(topRight.x, topRight.y + yOffset);
		ImGui::SetNextWindowPos(overlayPos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
		ImGui::SetNextWindowBgAlpha(0.78f);

		float consumedHeight = 0.0f;
		if (ImGui::Begin(uniqueId, nullptr, k_OverlayFlags)) {
			RenderBody();
			consumedHeight = ImGui::GetWindowSize().y;
		}
		ImGui::End();
		return consumedHeight;
	}

	float StatsOverlay::RenderInRect(const ImVec2& imageMin, const ImVec2& imageMax, float yOffset) const {
		constexpr float k_Pad = 8.0f;
		return RenderOverlayWindow("##FunkeStatsOverlayRect",
			ImVec2(imageMax.x - k_Pad, imageMin.y + k_Pad), yOffset);
	}

	float StatsOverlay::RenderInMainViewport(float yOffset) const {
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		if (!vp) return 0.0f;
		constexpr float k_Pad = 10.0f;
		return RenderOverlayWindow("##FunkeStatsOverlayViewport",
			ImVec2(vp->WorkPos.x + vp->WorkSize.x - k_Pad, vp->WorkPos.y + k_Pad), yOffset);
	}

} // namespace Funke::Diagnostics
