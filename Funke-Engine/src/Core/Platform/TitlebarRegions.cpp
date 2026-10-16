#include "pch.hpp"
#include "Core/Platform/TitlebarRegions.hpp"

namespace Funke::TitlebarRegions {

	namespace {
		struct Rect {
			int X = 0, Y = 0, W = 0, H = 0;
			bool Contains(int px, int py) const {
				return px >= X && px < X + W && py >= Y && py < Y + H;
			}
			bool Empty() const { return W <= 0 || H <= 0; }
		};

		int g_HeightLogical = 32;
		Rect g_CaptionRect{};
		std::vector<Rect> g_NonClientRects;
	}

	void SetHeight(int logicalPx) {
		g_HeightLogical = logicalPx;
	}

	int GetHeight() {
		return g_HeightLogical;
	}

	void SetCaptionRect(int x, int y, int w, int h) {
		g_CaptionRect = Rect{ x, y, w, h };
	}

	void AddNonClientRect(int x, int y, int w, int h) {
		if (w <= 0 || h <= 0) return;
		g_NonClientRects.push_back(Rect{ x, y, w, h });
	}

	void ResetNonClientRects() {
		g_NonClientRects.clear();
	}

	Hit HitTest(int x, int y, int windowWidth, int windowHeight, int resizeBorder, bool resizable, bool maximized) {
		if (resizable && !maximized && resizeBorder > 0) {
			const bool onLeft   = x < resizeBorder;
			const bool onRight  = x >= windowWidth - resizeBorder;
			const bool onTop    = y < resizeBorder;
			const bool onBottom = y >= windowHeight - resizeBorder;
			if (onTop    && onLeft)  return Hit::ResizeTopLeft;
			if (onTop    && onRight) return Hit::ResizeTopRight;
			if (onBottom && onLeft)  return Hit::ResizeBottomLeft;
			if (onBottom && onRight) return Hit::ResizeBottomRight;
			if (onLeft)   return Hit::ResizeLeft;
			if (onRight)  return Hit::ResizeRight;
			if (onTop)    return Hit::ResizeTop;
			if (onBottom) return Hit::ResizeBottom;
		}

		if (!g_CaptionRect.Empty() && g_CaptionRect.Contains(x, y)) {
			for (const Rect& r : g_NonClientRects) {
				if (r.Contains(x, y)) {
					return Hit::Client;
				}
			}
			return Hit::Caption;
		}
		return Hit::Client;
	}

}
