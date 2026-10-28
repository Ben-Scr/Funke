#pragma once
#include "Collections/Vec2.hpp"
#include <glm/mat4x4.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Funke::QuadVisibility {

	struct Clip {
		Vec2 Min{ -1.0f, -1.0f };
		Vec2 Max{ 1.0f, 1.0f };
	};

	enum class Visibility { Outside, Intersecting, Inside };

	inline Visibility Classify(const std::array<Vec2, 4>& corners, const glm::mat4& mvp, const Clip& clip) {
		bool left = true, right = true, below = true, above = true;
		bool inside = true;
		for (const Vec2& corner : corners) {
			const glm::vec4 p = mvp * glm::vec4(corner.x, corner.y, 0.0f, 1.0f);
			// Perspective crossings and non-finite inputs stay with the GPU's clipping rules.
			if (!(p.w > 0.0f) || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.w)) return Visibility::Intersecting;
			left &= p.x < clip.Min.x * p.w;
			right &= p.x > clip.Max.x * p.w;
			below &= p.y < clip.Min.y * p.w;
			above &= p.y > clip.Max.y * p.w;
			inside &= p.x >= clip.Min.x * p.w && p.x <= clip.Max.x * p.w
				&& p.y >= clip.Min.y * p.w && p.y <= clip.Max.y * p.w;
		}
		return left || right || below || above ? Visibility::Outside
			: inside ? Visibility::Inside : Visibility::Intersecting;
	}

	inline bool IsVisible(const std::array<Vec2, 4>& corners, const glm::mat4& mvp, const Clip& clip) {
		return Classify(corners, mvp, clip) != Visibility::Outside;
	}

	inline Clip FromScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
		uint32_t viewportX, uint32_t viewportY, uint32_t viewportWidth, uint32_t viewportHeight) {
		if (viewportWidth == 0 || viewportHeight == 0) return {};
		const float scaleX = 2.0f / static_cast<float>(viewportWidth);
		const float scaleY = 2.0f / static_cast<float>(viewportHeight);
		const float left = (static_cast<float>(x) - static_cast<float>(viewportX)) * scaleX - 1.0f;
		const float top = 1.0f - (static_cast<float>(y) - static_cast<float>(viewportY)) * scaleY;
		return { { std::max(-1.0f, left), std::max(-1.0f, top - height * scaleY) },
			{ std::min(1.0f, left + width * scaleX), std::min(1.0f, top) } };
	}

}
