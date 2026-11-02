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
		// Keep pixel offsets exact before division, including when the compiler contracts arithmetic.
		const double offsetX = static_cast<double>(x) - viewportX;
		const double offsetY = static_cast<double>(y) - viewportY;
		const float left = static_cast<float>((2.0 * offsetX - viewportWidth) / viewportWidth);
		const float right = static_cast<float>((2.0 * (offsetX + width) - viewportWidth) / viewportWidth);
		const float top = static_cast<float>((viewportHeight - 2.0 * offsetY) / viewportHeight);
		const float bottom = static_cast<float>((viewportHeight - 2.0 * (offsetY + height)) / viewportHeight);
		return { { std::max(-1.0f, left), std::max(-1.0f, bottom) },
			{ std::min(1.0f, right), std::min(1.0f, top) } };
	}

}
