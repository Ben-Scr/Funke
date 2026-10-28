#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Funke {

	enum class EditorCamera3DNavigationMode : uint8_t {
		Orbit = 0,
		Fly,
	};

	struct EditorCamera3DSettings {
		static constexpr float k_MinMoveSpeed = 0.01f;
		static constexpr float k_MaxMoveSpeed = 1000.0f;
		static constexpr float k_MinBoostMultiplier = 1.0f;
		static constexpr float k_MaxBoostMultiplier = 20.0f;
		static constexpr float k_MinSensitivity = 0.01f;
		static constexpr float k_MaxSensitivity = 10.0f;
		static constexpr float k_MinFieldOfView = 10.0f;
		static constexpr float k_MaxFieldOfView = 120.0f;

		EditorCamera3DNavigationMode NavigationMode = EditorCamera3DNavigationMode::Orbit;
		float MoveSpeed = 10.0f;
		float BoostMultiplier = 3.0f;
		float LookSensitivity = 1.0f;
		float PanSensitivity = 1.0f;
		float ZoomSensitivity = 1.0f;
		float FieldOfView = 60.0f;
		bool ScaleSpeedWithDistance = true;
		bool InvertLookX = false;
		bool InvertLookY = false;
		bool FlyWorldUp = false;

		void Sanitize() {
			const EditorCamera3DSettings defaults;
			if (NavigationMode != EditorCamera3DNavigationMode::Orbit && NavigationMode != EditorCamera3DNavigationMode::Fly) {
				NavigationMode = defaults.NavigationMode;
			}
			auto clampFinite = [](float value, float minimum, float maximum, float fallback) {
				return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
			};
			MoveSpeed = clampFinite(MoveSpeed, k_MinMoveSpeed, k_MaxMoveSpeed, defaults.MoveSpeed);
			BoostMultiplier = clampFinite(BoostMultiplier, k_MinBoostMultiplier, k_MaxBoostMultiplier, defaults.BoostMultiplier);
			LookSensitivity = clampFinite(LookSensitivity, k_MinSensitivity, k_MaxSensitivity, defaults.LookSensitivity);
			PanSensitivity = clampFinite(PanSensitivity, k_MinSensitivity, k_MaxSensitivity, defaults.PanSensitivity);
			ZoomSensitivity = clampFinite(ZoomSensitivity, k_MinSensitivity, k_MaxSensitivity, defaults.ZoomSensitivity);
			FieldOfView = clampFinite(FieldOfView, k_MinFieldOfView, k_MaxFieldOfView, defaults.FieldOfView);
		}

		bool operator==(const EditorCamera3DSettings&) const = default;
	};

}
