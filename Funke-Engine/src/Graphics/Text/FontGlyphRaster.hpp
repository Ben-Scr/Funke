#pragma once

#include "Graphics/Text/Font.hpp"

#include <algorithm>
#include <span>

namespace Funke::FontRaster {

	struct Glyph {
		GlyphMetrics Metrics;
		std::vector<uint8_t> Bitmap;
		uint32_t Codepoint = 0;
		int Width = 0;
		int Height = 0;
	};

	FUNKE_API bool Rasterize(std::span<const uint8_t> bytes, float pixelSize, uint32_t codepoint, Glyph& out);

	class AtlasShelf {
	public:
		explicit AtlasShelf(int side) : m_Side(side) {}

		bool Place(int width, int height, int& x, int& y) {
			if (width <= 0 || height <= 0 || width > m_Side - 2 || height > m_Side - 2) return false;
			int nextX = m_X;
			int nextY = m_Y;
			int rowHeight = m_RowHeight;
			if (nextX + width + 2 > m_Side) {
				nextX = 0;
				nextY += rowHeight;
				rowHeight = 0;
			}
			if (nextY + height + 2 > m_Side) return false;
			x = nextX + 1;
			y = nextY + 1;
			m_X = nextX + width + 2;
			m_Y = nextY;
			m_RowHeight = std::max(rowHeight, height + 2);
			return true;
		}

		int GetSide() const { return m_Side; }

	private:
		int m_Side;
		int m_X = 0;
		int m_Y = 0;
		int m_RowHeight = 0;
	};
}
