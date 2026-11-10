#include "pch.hpp"
#include "Systems/UIFocusSystem.hpp"
#include "Systems/UIEventSystem.hpp"

#include "Collections/Viewport.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/Tags.hpp"
#include "Components/UI/InputFieldComponent.hpp"
#include "Components/UI/DropdownComponent.hpp"
#include "Components/UI/SliderComponent.hpp"
#include "Components/UI/CircularSliderComponent.hpp"
#include "DevTools/DevConsole.hpp"
#include "Components/UI/InteractableComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Input.hpp"
#include "Core/KeyCode.hpp"
#include "Core/MouseButton.hpp"
#include "Core/Window.hpp"
#include "Gui/UIDrawOrder.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <utility>
#include <vector>

namespace Funke {

	namespace {
		// Keep mouse-focus hit-tests in the same centered, Y-up space as UI events.
		Vec2 ScreenPixelToUiSpace(const Vec2& mousePosition, int viewportWidth, int viewportHeight) {
			return Vec2{
				mousePosition.x - static_cast<float>(viewportWidth) * 0.5f,
				static_cast<float>(viewportHeight) * 0.5f - mousePosition.y
			};
		}

		constexpr float k_StickNavThreshold = 0.5f;

		template<typename TSlider>
		void ApplySliderNavigation(TSlider& slider, int direction, const UIFocusSystem::NavigationInput& navigation) {
			if (!navigation.Enabled || navigation.Tab || slider.IsReadOnly || slider.IsDragging
				|| !(direction || navigation.Home || navigation.End)) return;
			if (!slider.ValueObserved) {
				slider.LastObservedValue = slider.Value;
				slider.ValueObserved = true;
			}
			const float range = slider.MaxValue - slider.MinValue;
			const float step = slider.WholeNumbers ? (range < 0.0f ? -1.0f : 1.0f) : range * 0.01f;
			float value = slider.Value + direction * step;
			if (navigation.Home) value = slider.MinValue;
			if (navigation.End) value = slider.MaxValue;
			if (slider.WholeNumbers) value = std::round(value);
			slider.Value = std::clamp(value, std::min(slider.MinValue, slider.MaxValue), std::max(slider.MinValue, slider.MaxValue));
		}

		bool AnyPadPushed(const Input& input, GamepadAxis axis, float sign,
			GamepadButton dpad)
		{
			for (int i = 0; i < Input::k_MaxGamepads; ++i) {
				if (!input.IsGamepadConnected(i)) continue;
				if (input.GetGamepadButton(dpad, i)) return true;
				const float v = input.GetGamepadAxis(axis, i);
				if (sign > 0.f && v >  k_StickNavThreshold) return true;
				if (sign < 0.f && v < -k_StickNavThreshold) return true;
			}
			return false;
		}

		bool AnyPadButtonDown(const Input& input, GamepadButton button) {
			for (int i = 0; i < Input::k_MaxGamepads; ++i) {
				if (!input.IsGamepadConnected(i)) continue;
				if (input.GetGamepadButtonDown(button, i)) return true;
			}
			return false;
		}
	}

	std::vector<EntityHandle> UIFocusSystem::NavigationOrder(Scene& scene) {
		auto& registry = scene.GetRegistry();
		std::vector<std::pair<EntityHandle, int>> drawOrder;
		UIDrawOrder::Build(registry, drawOrder);
		std::vector<EntityHandle> focusables;
		for (const auto& [entity, index] : drawOrder) {
			const auto* interact = registry.try_get<InteractableComponent>(entity);
			if (interact && interact->Focusable && interact->Interactable) focusables.push_back(entity);
		}
		return focusables;
	}

	EntityHandle UIFocusSystem::PickFocusable(Scene& scene, const Vec2& screenUi) {
		const auto hit = UIEventSystem::PickInteractable(scene, screenUi);
		const auto* interact = scene.GetRegistry().try_get<InteractableComponent>(hit);
		return interact && interact->Focusable ? hit : entt::null;
	}

	void UIFocusSystem::Update(Scene& scene) {
		Application* app = Application::GetInstance();
		if (!app) return;
		FUNKE_PROFILE_SCOPE("UIFocus");
		Input& input = app->GetInput();

		NavigationInput navigation;
		navigation.Enabled = app->IsGameInputEnabled() && !DevConsole::IsOpen();
		// ── 3. Mouse-focus promotion ────────────────────────────────
		const bool mouseDownThisFrame = navigation.Enabled && input.GetMouseDown(MouseButton::Left);
		if (mouseDownThisFrame) {
			const Window::UIRegion uiRegion = Window::GetUIRegion();
			const Vec2 mousePosition = input.GetMousePosition();
			int vpW = 0, vpH = 0;
			if (uiRegion.IsActive()) {
				vpW = uiRegion.Width;
				vpH = uiRegion.Height;
			}
			else if (Viewport* viewport = Window::GetMainViewport()) {
				vpW = viewport->GetWidth();
				vpH = viewport->GetHeight();
			}
			const bool cursorOverUiSurface = vpW > 0 && vpH > 0
				&& (!Application::IsEditor() || uiRegion.IsActive())
				&& mousePosition.x >= 0.0f && mousePosition.y >= 0.0f
				&& mousePosition.x < static_cast<float>(vpW)
				&& mousePosition.y < static_cast<float>(vpH);
			if (cursorOverUiSurface) {
				const Vec2 mouseUi = ScreenPixelToUiSpace(mousePosition, vpW, vpH);
				navigation.PointerPressed = true;
				navigation.PointerTarget = PickFocusable(scene, mouseUi);
			}
		}

		// Stick / D-pad rising edges — fire once per push.
		const bool padUpPushed    = AnyPadPushed(input, GamepadAxis::LeftY, -1.f, GamepadButton::DPadUp);
		const bool padDownPushed  = AnyPadPushed(input, GamepadAxis::LeftY,  1.f, GamepadButton::DPadDown);
		const bool padLeftPushed  = AnyPadPushed(input, GamepadAxis::LeftX, -1.f, GamepadButton::DPadLeft);
		const bool padRightPushed = AnyPadPushed(input, GamepadAxis::LeftX,  1.f, GamepadButton::DPadRight);

		const bool padUpEdge    = padUpPushed    && !m_PrevAxisUpPushed;
		const bool padDownEdge  = padDownPushed  && !m_PrevAxisDownPushed;
		const bool padLeftEdge  = padLeftPushed  && !m_PrevAxisLeftPushed;
		const bool padRightEdge = padRightPushed && !m_PrevAxisRightPushed;

		m_PrevAxisUpPushed    = padUpPushed;
		m_PrevAxisDownPushed  = padDownPushed;
		m_PrevAxisLeftPushed  = padLeftPushed;
		m_PrevAxisRightPushed = padRightPushed;

		navigation.Tab = input.GetKeyDown(KeyCode::Tab);
		navigation.Shift = input.GetKey(KeyCode::LeftShift) || input.GetKey(KeyCode::RightShift);
		navigation.Left = input.GetKeyDown(KeyCode::Left) || padLeftEdge;
		navigation.Right = input.GetKeyDown(KeyCode::Right) || padRightEdge;
		navigation.Up = input.GetKeyDown(KeyCode::Up) || padUpEdge;
		navigation.Down = input.GetKeyDown(KeyCode::Bottom) || padDownEdge;
		navigation.Home = input.GetKeyDown(KeyCode::Home);
		navigation.End = input.GetKeyDown(KeyCode::End);
		navigation.Activate = input.GetKeyDown(KeyCode::Enter) || input.GetKeyDown(KeyCode::KpEnter)
			|| input.GetKeyDown(KeyCode::Space) || AnyPadButtonDown(input, GamepadButton::A);
		navigation.Cancel = input.GetKeyDown(KeyCode::Esc) || AnyPadButtonDown(input, GamepadButton::B);
		UpdateNavigation(scene, navigation);
	}

	void UIFocusSystem::UpdateNavigation(Scene& scene, const NavigationInput& navigation) {
		auto& registry = scene.GetRegistry();
		const auto focusables = NavigationOrder(scene);

		// ── 1. Validate the persisted focus target ──────────────────
		auto IsValidFocusTarget = [&](EntityHandle e) {
			if (e == entt::null) return false;
			if (!registry.valid(e)) return false;
			if (std::find(focusables.begin(), focusables.end(), e) == focusables.end()) return false;
			auto* interact = registry.try_get<InteractableComponent>(e);
			if (!interact) return false;
			return interact->Focusable && interact->Interactable;
		};
		if (!IsValidFocusTarget(m_FocusedEntity)) {
			m_FocusedEntity = entt::null;
		}

		// ── 1b. Honour programmatic focus writes ────────────────────
		// Detect IsFocused writes from scripts/inspector before we overwrite it below: a new IsFocused=true adopts focus; tracked-target IsFocused=false relinquishes it.
		{
			EntityHandle externallyFocused = entt::null;
			for (EntityHandle entity : focusables) {
				const auto& interact = registry.get<InteractableComponent>(entity);
				if (interact.IsFocused && entity != m_FocusedEntity) {
					externallyFocused = entity;
					break;
				}
			}
			if (externallyFocused != entt::null) {
				m_FocusedEntity = externallyFocused;
			}
			else if (m_FocusedEntity != entt::null) {
				if (auto* interact = registry.try_get<InteractableComponent>(m_FocusedEntity);
					interact && !interact->IsFocused)
				{
					m_FocusedEntity = entt::null;
				}
			}
		}

		if (navigation.Enabled && navigation.PointerPressed) {
			m_FocusedEntity = IsValidFocusTarget(navigation.PointerTarget) ? navigation.PointerTarget : entt::null;
		}

		bool left = navigation.Enabled && navigation.Left;
		bool right = navigation.Enabled && navigation.Right;
		bool up = navigation.Enabled && navigation.Up;
		bool down = navigation.Enabled && navigation.Down;
		bool wantActivate = navigation.Enabled && navigation.Activate;
		bool wantCancel = navigation.Enabled && navigation.Cancel;
		const bool tabPressed = navigation.Enabled && navigation.Tab;
		if (tabPressed) wantActivate = false;

		for (auto&& [entity, dropdown] : registry.view<DropdownComponent>().each()) {
			if (dropdown.NavigationIndex >= 0 && (!IsValidFocusTarget(entity) || dropdown.IsReadOnly
				|| dropdown.Options.empty() || (entity != m_FocusedEntity && !navigation.PointerPressed))) {
				dropdown.IsOpen = false;
			}
			if (!dropdown.IsOpen) dropdown.NavigationIndex = -1;
		}

		if (m_FocusedEntity != entt::null) {
			if (auto* field = registry.try_get<InputFieldComponent>(m_FocusedEntity); field && field->IsFocused) {
				left = right = up = down = false;
			}
			if (auto* dropdown = registry.try_get<DropdownComponent>(m_FocusedEntity)) {
				const bool activate = std::exchange(wantActivate, false);
				if (dropdown->IsOpen) {
					if (wantCancel || tabPressed) {
						dropdown->IsOpen = false;
						dropdown->NavigationIndex = -1;
						wantCancel = false;
					}
					else if (navigation.Enabled && !dropdown->IsReadOnly && !dropdown->Options.empty()) {
						const int last = static_cast<int>(dropdown->Options.size()) - 1;
						int index = std::clamp(dropdown->NavigationIndex < 0 ? dropdown->SelectedIndex : dropdown->NavigationIndex, 0, last);
						if (up || down || navigation.Home || navigation.End || activate) {
							index = std::clamp(index + static_cast<int>(down) - static_cast<int>(up), 0, last);
							if (navigation.Home) index = 0;
							if (navigation.End) index = last;
							dropdown->NavigationIndex = index;
						}
						if (activate) {
							if (!dropdown->SelectionObserved) {
								dropdown->LastObservedSelectedIndex = dropdown->SelectedIndex;
								dropdown->SelectionObserved = true;
							}
							dropdown->SelectedIndex = index;
							dropdown->IsOpen = false;
							dropdown->NavigationIndex = -1;
						}
					}
					left = right = up = down = false;
				}
				else if (activate && !dropdown->IsReadOnly && !dropdown->Options.empty()) {
					dropdown->IsOpen = true;
					dropdown->NavigationIndex = std::clamp(dropdown->SelectedIndex, 0, static_cast<int>(dropdown->Options.size()) - 1);
				}
			}

			SliderComponent* slider = registry.try_get<SliderComponent>(m_FocusedEntity);
			if (!slider) {
				for (auto&& [entity, candidate] : registry.view<SliderComponent>().each()) {
					if (candidate.HandleEntity == m_FocusedEntity && IsValidFocusTarget(entity)) {
						slider = &candidate;
						break;
					}
				}
			}
			if (slider) {
				// A synthetic pointer press would jump the value to the mouse position.
				wantActivate = false;
				const bool horizontal = slider->Direction == SliderDirection::LeftToRight || slider->Direction == SliderDirection::RightToLeft;
				const bool reversed = slider->Direction == SliderDirection::RightToLeft || slider->Direction == SliderDirection::TopToBottom;
				int direction = horizontal ? static_cast<int>(right) - static_cast<int>(left) : static_cast<int>(up) - static_cast<int>(down);
				if (horizontal) left = right = false;
				else up = down = false;
				if (reversed) direction = -direction;
				ApplySliderNavigation(*slider, direction, navigation);
			}

			CircularSliderComponent* circular = registry.try_get<CircularSliderComponent>(m_FocusedEntity);
			if (!circular) {
				for (auto&& [entity, candidate] : registry.view<CircularSliderComponent>().each()) {
					if (candidate.HandleEntity == m_FocusedEntity && IsValidFocusTarget(entity)) {
						circular = &candidate;
						break;
					}
				}
			}
			if (circular) {
				wantActivate = false;
				const int direction = static_cast<int>(right || up) - static_cast<int>(left || down);
				left = right = up = down = false;
				ApplySliderNavigation(*circular, direction, navigation);
			}
		}
		const bool wantNext = (tabPressed && !navigation.Shift) || (!tabPressed && (down || right));
		const bool wantPrev = (tabPressed && navigation.Shift) || (!tabPressed && (up || left));

		// ── 5. Apply navigation actions ─────────────────────────────
		if (!focusables.empty() && (wantNext || wantPrev)) {
			int currentIdx = -1;
			if (m_FocusedEntity != entt::null) {
				for (size_t i = 0; i < focusables.size(); ++i) {
					if (focusables[i] == m_FocusedEntity) {
						currentIdx = static_cast<int>(i);
						break;
					}
				}
			}
			const int n = static_cast<int>(focusables.size());
			int nextIdx = currentIdx;
			if (wantNext) {
				nextIdx = (currentIdx < 0) ? 0 : (currentIdx + 1) % n;
			}
			else if (wantPrev) {
				nextIdx = (currentIdx < 0) ? (n - 1) : (currentIdx - 1 + n) % n;
			}
			m_FocusedEntity = focusables[nextIdx];
		}

		if (wantCancel) {
			m_FocusedEntity = entt::null;
		}

		// ── 6. Apply Activate ───────────────────────────────────────
		// Write ActivatedThisFrame (not IsClicked/IsMouseDown) so UIEventSystem's subsequent hit-test doesn't overwrite it.
		if (wantActivate && m_FocusedEntity != entt::null) {
			if (auto* interact = registry.try_get<InteractableComponent>(m_FocusedEntity)) {
				interact->ActivatedThisFrame = true;
			}
		}

		// ── 7. Sync InputField.IsFocused with the navigation focus ──
		auto fieldView = registry.view<InteractableComponent, InputFieldComponent>();
		for (auto&& [entity, interact, field] : fieldView.each()) {
			const bool shouldBeFocused = (entity == m_FocusedEntity);
			if (shouldBeFocused && !field.IsFocused) {
				field.IsFocused = true;
				// Place the caret at the end on tab-in — common UX, and
				// keeps the existing click-to-place-caret path untouched
				// (mouse focus still uses ByteFromMouseX in UIEventSystem).
				field.CaretBytePos = static_cast<int>(field.Text.size());
				field.SelectionAnchorBytePos = field.CaretBytePos;
			}
			else if (!shouldBeFocused && field.IsFocused) {
				// UIEventSystem's defocus loop only fires on click-outside; clear here on tab-away.
				field.IsFocused = false;
			}
		}

		// ── 8. Write the IsFocused flag every frame ─────────────────
		// Reconcile unconditionally to clear stale IsFocused=true if Focusable was flipped off mid-session.
		for (auto&& [entity, interact] : registry.view<InteractableComponent>().each()) {
			const bool nowFocused = interact.Focusable
				&& interact.Interactable
				&& (entity == m_FocusedEntity);
			interact.IsFocused = nowFocused;
		}
	}

}
