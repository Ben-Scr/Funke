#pragma once

#include "Graphics/RenderStatistics.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>

// NOT FUNKE_API: each consumer (Editor, Runtime) compiles the .cpp directly; the engine DLL excludes it (see premake5.lua) so FUNKE_API would cause LNK2019.
struct ImVec2;

namespace Funke::Diagnostics {

	class StatsOverlay {
	public:
		struct Snapshot {
			float Fps              = 0.0f;
			float CpuMainMs        = 0.0f;
			float RenderThreadMs   = 0.0f;
			// Per frame: 2D sprites and particles plus 3D meshes and particles; shadow passes separately.
			std::uint64_t Tris       = 0;
			std::uint64_t Verts      = 0;
			std::uint64_t ShadowTris = 0;
			int   ScreenWidth      = 0;
			int   ScreenHeight     = 0;
			// Whole-process OS totals include DLLs and drivers, not just engine allocations.
			std::size_t ProcessWorkingSetBytes    = 0;
			std::size_t ProcessPrivateCommitBytes = 0;
			std::uint32_t AudioPlaying   = 0;
			std::size_t Entities         = 0;
		};

		// frameGeometry: what one frame of this view drew (the editor's Game View measures its own render).
		// Null: the whole application's average per frame since the previous refresh.
		bool RefreshIfDue(int screenWidth, int screenHeight, const RenderGeometryStats* frameGeometry = nullptr);

		const Snapshot& GetCached() const { return m_Cached; }

		float RenderInRect(const ImVec2& imageMin, const ImVec2& imageMax, float yOffset = 0.0f) const;
		float RenderInMainViewport(float yOffset = 0.0f) const;

	private:
		void RenderBody() const; // shared body of RenderInRect / RenderInMainViewport
		float RenderOverlayWindow(const char* uniqueId, const ImVec2& topRight, float yOffset) const;

		Snapshot m_Cached{};
		std::chrono::steady_clock::time_point m_LastRefresh{};
		RenderGeometryStats m_LastGeometryTotals{};
		int m_LastGeometryFrame = -1;
	};

} // namespace Funke::Diagnostics
