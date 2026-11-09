#include "pch.hpp"
#include "Systems/UIFocusSystem.hpp"
#include "Systems/UIEventSystem.hpp"

#include "Collections/Viewport.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/Tags.hpp"
#include "Components/UI/InputFieldComponent.hpp"
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

		// ── 3. Mouse-focus promotion ────────────────────────────────
		const bool mouseDownThisFrame = input.GetMouseDown(MouseButton::Left);
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
				const EntityHandle hitFocusable = PickFocusable(scene, mouseUi);
				// A click on anything else lets go: otherwise a focused input field kept its focus (and the phone
				// keyboard) because UIEventSystem's click-outside defocus was undone here next frame.
				m_FocusedEntity = hitFocusable;
			}
		}

		// ── 4. Read input actions ───────────────────────────────────
		const bool shiftDown = input.GetKey(KeyCode::LeftShift) || input.GetKey(KeyCode::RightShift);

		bool inputFieldOwnsArrows = false;
		if (m_FocusedEntity != entt::null && registry.valid(m_FocusedEntity)) {
			if (auto* field = registry.try_get<InputFieldComponent>(m_FocusedEntity)) {
				inputFieldOwnsArrows = field->IsFocused;
			}
		}

		auto navKeyDown = [&](KeyCode k) {
			return !inputFieldOwnsArrows && input.GetKeyDown(k);
		};

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

		const bool tabPressed = input.GetKeyDown(KeyCode::Tab);
		const bool wantNext = (tabPressed && !shiftDown)
			|| navKeyDown(KeyCode::Bottom) || navKeyDown(KeyCode::Right)
			|| padDownEdge || padRightEdge;
		const bool wantPrev = (tabPressed && shiftDown)
			|| navKeyDown(KeyCode::Up) || navKeyDown(KeyCode::Left)
			|| padUpEdge || padLeftEdge;

		const bool wantActivate = input.GetKeyDown(KeyCode::Enter)
			|| input.GetKeyDown(KeyCode::KpEnter)
			|| input.GetKeyDown(KeyCode::Space)
			|| AnyPadButtonDown(input, GamepadButton::A);

		const bool wantCancel = input.GetKeyDown(KeyCode::Esc)
			|| AnyPadButtonDown(input, GamepadButton::B);

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
