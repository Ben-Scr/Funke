#include "Gui/GrabCursor.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <array>
#include <string_view>

namespace Funke::GrabCursor {

	namespace {
		constexpr int k_Size = 24;
		constexpr int k_HotspotX = 12;
		constexpr int k_HotspotY = 11;
		using Bitmap = std::array<std::string_view, k_Size>;
		// Original monochrome cursor artwork: X = outline, o = white, . = transparent.
		constexpr Bitmap k_OpenHand = {
			"........................",
			"...........XX...........",
			"..........XooX.XX.......",
			".......XX.XooXXooX......",
			"......XooXXooXXooX......",
			"......XooXXooXXooX.XX...",
			"......XooXXooXXooXXooX..",
			"......XooXXooXXooXXooX..",
			"......XooXXooXXooXXooX..",
			"......XooXXooXXooXXooX..",
			"..XX..XooooooooooXXooX..",
			".XooX.XooooooooooooooX..",
			".XoooXXooooooooooooooX..",
			"..XoooXooooooooooooooX..",
			"...XoooooooooooooooooX..",
			"....XooooooooooooooooX..",
			".....XoooooooooooooooX..",
			"......XoooooooooooooX...",
			".......XooooooooooooX...",
			"........XooooooooooX....",
			"........XooooooooooX....",
			"........XXXXXXXXXXXX....",
			"........................",
			"........................",
		};
		constexpr Bitmap k_ClosedHand = {
			"........................",
			"........................",
			"........................",
			"........................",
			"........................",
			".........XX.XX..........",
			"......XXXooXooXXX.......",
			".....XooXooXooXooX......",
			".....XooXooXooXooX......",
			".....XooXooXooXooX......",
			".....XoooooooooooX......",
			"...XXXoooooooooooX......",
			"..XoooXooooooooooX......",
			"..XooooXoooooooooX......",
			"...XoooXoooooooooX......",
			"....XooooooooooooX......",
			".....XoooooooooooX......",
			"......XoooooooooX.......",
			".......XooooooooX.......",
			".......XooooooooX.......",
			".......XXXXXXXXXX.......",
			"........................",
			"........................",
			"........................",
		};
		static_assert([] {
			for (const auto& bitmap : { k_OpenHand, k_ClosedHand }) {
				for (const auto row : bitmap) if (row.size() != k_Size) return false;
			}
			return true;
		}());

		SDL_Cursor* s_Open = nullptr;
		SDL_Cursor* s_Closed = nullptr;
		SDL_Cursor* s_Previous = nullptr;
		bool s_Initialized = false;
		bool s_Override = false;
		bool s_Grabbing = false;
		int s_RequestFrame = -1;

		SDL_Surface* MakeSurface(const Bitmap& bitmap, int size) {
			SDL_Surface* surface = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32);
			if (!surface) return nullptr;
			for (int y = 0; y < size; ++y) {
				auto* row = static_cast<Uint8*>(surface->pixels) + y * surface->pitch;
				for (int x = 0; x < size; ++x) {
					const char pixel = bitmap[y * k_Size / size][x * k_Size / size];
					row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = pixel == 'o' ? 255 : 0;
					row[x * 4 + 3] = pixel == '.' ? 0 : 255;
				}
			}
			const SDL_PropertiesID properties = SDL_GetSurfaceProperties(surface);
			SDL_SetNumberProperty(properties, SDL_PROP_SURFACE_HOTSPOT_X_NUMBER, k_HotspotX * size / k_Size);
			SDL_SetNumberProperty(properties, SDL_PROP_SURFACE_HOTSPOT_Y_NUMBER, k_HotspotY * size / k_Size);
			return surface;
		}

		SDL_Cursor* MakeCursor(const Bitmap& bitmap) {
			SDL_Surface* surface = MakeSurface(bitmap, k_Size);
			if (!surface) return nullptr;
			// Exact alternate sizes preserve the hotspot on Windows at fractional DPI scales.
			for (const int size : { 30, 36, 42, 48, 54, 60, 72, 84, 96 }) {
				if (SDL_Surface* alternate = MakeSurface(bitmap, size)) {
					SDL_AddSurfaceAlternateImage(surface, alternate);
					SDL_DestroySurface(alternate);
				}
			}
			SDL_Cursor* cursor = SDL_CreateColorCursor(surface, k_HotspotX, k_HotspotY);
			SDL_DestroySurface(surface);
			return cursor;
		}

		void Restore() {
			if (!s_Override) return;
			// Keep the SDL backend's cached cursor consistent with the actual native cursor.
			if (SDL_GetCursor() == s_Open || SDL_GetCursor() == s_Closed) {
				SDL_SetCursor(s_Previous ? s_Previous : SDL_GetDefaultCursor());
			}
			ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
			s_Previous = nullptr;
			s_Override = false;
		}
	}

	void Request(bool grabbing) {
		const int frame = ImGui::GetFrameCount();
		s_Grabbing = grabbing || (s_RequestFrame == frame && s_Grabbing);
		s_RequestFrame = frame;
	}

	void Apply() {
		ImGuiIO& io = ImGui::GetIO();
		if (s_RequestFrame != ImGui::GetFrameCount() || io.AppFocusLost || io.MouseDrawCursor
			|| (!s_Override && (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange))) {
			Restore();
			return;
		}
		if (!s_Initialized) {
			s_Open = MakeCursor(k_OpenHand);
			s_Closed = MakeCursor(k_ClosedHand);
			s_Initialized = true;
		}
		SDL_Cursor* cursor = s_Grabbing ? s_Closed : s_Open;
		if (!cursor) { Restore(); return; }
		SDL_Cursor* previous = SDL_GetCursor();
		if (previous != cursor && !SDL_SetCursor(cursor)) { Restore(); return; }
		if (!s_Override) {
			s_Previous = previous;
			// Avoid the backend replacing the hand every NewFrame (visible flicker at low FPS).
			io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
			s_Override = true;
			SDL_ShowCursor();
		}
	}

	void Shutdown() {
		Restore();
		SDL_DestroyCursor(s_Open);
		SDL_DestroyCursor(s_Closed);
		s_Open = s_Closed = nullptr;
		s_Initialized = false;
		s_RequestFrame = -1;
		s_Grabbing = false;
	}
}
