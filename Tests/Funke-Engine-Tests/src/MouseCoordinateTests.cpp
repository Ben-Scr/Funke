#include <doctest/doctest.h>

#include "Core/Application.hpp"
#include "Core/Window.hpp"
#include "Scripting/ScriptBindings.hpp"

using namespace Funke;

namespace {
	class UIRegionScope {
	public:
		UIRegionScope()
			: m_Previous(Window::GetUIRegion()) {}

		~UIRegionScope() {
			if (m_Previous.Source == Window::UIRegionSource::Editor) {
				Window::SetUIRegion(m_Previous.OffsetX, m_Previous.OffsetY,
					m_Previous.Width, m_Previous.Height, m_Previous.WindowToRegionScale);
			}
			else if (m_Previous.Source == Window::UIRegionSource::AspectLock) {
				Window::PublishAspectLockUIRegion(1.0f, m_Previous);
			}
			else {
				Window::ClearUIRegion();
			}
		}

	private:
		Window::UIRegion m_Previous;
	};

	void CheckPosition(Vec2 actual, float expectedX, float expectedY) {
		CHECK(actual.x == doctest::Approx(expectedX));
		CHECK(actual.y == doctest::Approx(expectedY));
	}

	Vec2 MapPosition(Vec2 position, const Window::UIRegion& region,
		Vec2 framebufferScale = { 1.0f, 1.0f }) {
		return Window::MapToViewportLocalPosition(position, region, framebufferScale);
	}
}

TEST_CASE("Mouse coordinates remove aspect-lock offsets") {
	SUBCASE("pillarbox") {
		const Window::UIRegion region{ 200, 0, 1200, 900 };

		CheckPosition(MapPosition({ 800.0f, 450.0f }, region), 600.0f, 450.0f);
		CheckPosition(MapPosition({ 200.0f, 0.0f }, region), 0.0f, 0.0f);
	}

	SUBCASE("letterbox") {
		const Window::UIRegion region{ 0, 150, 1600, 900 };

		CheckPosition(MapPosition({ 800.0f, 600.0f }, region), 800.0f, 450.0f);
		CheckPosition(MapPosition({ 0.0f, 150.0f }, region), 0.0f, 0.0f);
	}
}

TEST_CASE("Mouse coordinates scale window positions before HiDPI offsets") {
	const Vec2 scale = Window::CalculateWindowToFramebufferScale({ 800, 600 }, { 1600, 1200 });
	Window::UIRegion region{ 0, 150, 1600, 900, scale };

	CheckPosition(MapPosition({ 400.0f, 300.0f }, region), 800.0f, 450.0f);
	CheckPosition(MapPosition({ 0.0f, 75.0f }, region), 0.0f, 0.0f);
	CheckPosition(MapPosition({ 0.0f, 50.0f }, region), 0.0f, -50.0f);

	const Vec2 asymmetricScale = Window::CalculateWindowToFramebufferScale(
		{ 1000, 500 }, { 1500, 1000 });
	region = Window::UIRegion{ 150, 100, 1200, 900, asymmetricScale };
	CheckPosition(MapPosition({ 200.0f, 75.0f }, region), 150.0f, 50.0f);

	CheckPosition(MapPosition({ 400.0f, 300.0f }, {}, scale), 800.0f, 600.0f);
	CheckPosition(Window::CalculateWindowToFramebufferScale({ 0, 600 }, { 1600, 1200 }), 1.0f, 2.0f);
}

TEST_CASE("Mouse coordinate regions preserve editor scale and clear when aspect lock is disabled") {
	const Window::UIRegion editorRegion{ 100, 50, 800, 450 };
	CheckPosition(MapPosition({ 100.0f, 50.0f }, editorRegion, { 2.0f, 2.0f }), 0.0f, 0.0f);

	const Window::UIRegion lockedRegion = Window::ResolveAspectLockUIRegion(4.0f / 3.0f, {},
		Window::UIRegion{ 240, 0, 1440, 1080, { 2.0f, 2.0f } });
	CHECK(lockedRegion.Source == Window::UIRegionSource::AspectLock);
	CHECK_FALSE(Window::ResolveAspectLockUIRegion(0.0f, lockedRegion, {}).IsActive());

	Window::UIRegion ownedEditorRegion = editorRegion;
	ownedEditorRegion.Source = Window::UIRegionSource::Editor;
	const Window::UIRegion preservedEditorRegion = Window::ResolveAspectLockUIRegion(
		0.0f, ownedEditorRegion, lockedRegion);
	CHECK(preservedEditorRegion.Source == Window::UIRegionSource::Editor);
	CHECK(preservedEditorRegion.OffsetX == 100);
	CHECK(preservedEditorRegion.OffsetY == 50);

	const UIRegionScope restoreRegion;
	Window::PublishAspectLockUIRegion(4.0f / 3.0f,
		Window::UIRegion{ 240, 0, 1440, 1080, { 2.0f, 2.0f } });
	CHECK(Window::GetUIRegion().IsActive());

	Window::PublishAspectLockUIRegion(0.0f, Window::GetUIRegion());
	CHECK_FALSE(Window::GetUIRegion().IsActive());

	Window::SetUIRegion(100, 50, 800, 450);
	Window::PublishAspectLockUIRegion(0.0f,
		Window::UIRegion{ 240, 0, 1440, 1080, { 2.0f, 2.0f } });
	const Window::UIRegion publishedEditorRegion = Window::GetUIRegion();
	CHECK(publishedEditorRegion.Source == Window::UIRegionSource::Editor);
	CHECK(publishedEditorRegion.OffsetX == 100);
	CHECK(publishedEditorRegion.OffsetY == 50);
}

TEST_CASE("Managed mouse position binding preserves native viewport coordinates") {
	const UIRegionScope restoreRegion;
	Application app;
	Window::SetUIRegion(100, 50, 800, 450);

	const Vec2 nativePosition = Input::GetMousePosition();
	CheckPosition(nativePosition, -100.0f, -50.0f);

	NativeBindings bindings{};
	ScriptBindings::PopulateNativeBindings(bindings);
	REQUIRE(bindings.Input_GetMousePosition != nullptr);

	float managedX = 0.0f;
	float managedY = 0.0f;
	bindings.Input_GetMousePosition(&managedX, &managedY);
	CheckPosition({ managedX, managedY }, nativePosition.x, nativePosition.y);
}
