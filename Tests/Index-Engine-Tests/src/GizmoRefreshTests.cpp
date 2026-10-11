#include <doctest/doctest.h>

#include "Graphics/Gizmo.hpp"
#include "Graphics/GizmoRenderer.hpp"

using namespace Index;

namespace {
	struct GizmoStateGuard {
		bool Enabled = Gizmo::IsEnabled();
		GizmoLayer Layer = Gizmo::GetLayer();

		~GizmoStateGuard() {
			Gizmo::Clear();
			Gizmo::SetEnabled(Enabled);
			Gizmo::SetLayer(Layer);
		}
	};
}

TEST_CASE("Gizmo refresh frames retain update commands and discard PreRender commands") {
	[[maybe_unused]] GizmoStateGuard guard;
	Gizmo::Clear();
	Gizmo::SetEnabled(true);
	Gizmo::SetLayer(GizmoLayer::Shared);

	Gizmo::DrawLine({ 0.0f, 0.0f }, { 1.0f, 1.0f });
	GizmoRenderer2D::CaptureRefreshBase();

	Gizmo::SetLayer(GizmoLayer::EditorOnly);
	Gizmo::DrawLine({ 1.0f, 0.0f }, { 0.0f, 1.0f });
	CHECK(Gizmo::GetRegisteredVertices() == 2);

	GizmoRenderer2D::EndFrame();
	CHECK(Gizmo::GetRegisteredVertices() == 1);

	Gizmo::DrawLine({ 1.0f, 0.0f }, { 0.0f, 1.0f });
	CHECK(Gizmo::GetRegisteredVertices() == 2);
	GizmoRenderer2D::EndFrame();
	CHECK(Gizmo::GetRegisteredVertices() == 1);
}

TEST_CASE("Gizmo logical frames replace the retained base without losing event commands") {
	[[maybe_unused]] GizmoStateGuard guard;
	Gizmo::Clear();
	Gizmo::SetEnabled(true);
	Gizmo::SetLayer(GizmoLayer::Shared);

	Gizmo::DrawLine({ 0.0f, 0.0f }, { 1.0f, 1.0f });
	GizmoRenderer2D::CaptureRefreshBase();
	GizmoRenderer2D::EndFrame();

	Gizmo::DrawWireSquare({ 0.0f, 0.0f }, { 1.0f, 1.0f });
	CHECK(Gizmo::GetRegisteredVertices() == 5);

	GizmoRenderer2D::BeginCommandFrame();
	CHECK(Gizmo::GetRegisteredVertices() == 4);

	GizmoRenderer2D::CaptureRefreshBase();
	GizmoRenderer2D::EndFrame();
	CHECK(Gizmo::GetRegisteredVertices() == 4);
}

TEST_CASE("Gizmo refresh retention covers every geometry command queue") {
	[[maybe_unused]] GizmoStateGuard guard;
	Gizmo::Clear();
	Gizmo::SetEnabled(true);
	Gizmo::SetLayer(GizmoLayer::Shared);

	Gizmo::DrawWireSquare({ 0.0f, 0.0f }, { 1.0f, 1.0f });
	Gizmo::DrawWireCircle({ 0.0f, 0.0f }, 1.0f, 3);
	Gizmo::DrawLine({ 0.0f, 0.0f }, { 1.0f, 1.0f });
	Gizmo::DrawThickLine({ 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.1f);
	Gizmo::DrawSquare({ 0.0f, 0.0f }, { 1.0f, 1.0f });
	Gizmo::DrawCircle({ 0.0f, 0.0f }, 1.0f, 3);
	CHECK(Gizmo::GetRegisteredVertices() == 21);

	GizmoRenderer2D::CaptureRefreshBase();
	GizmoRenderer2D::EndFrame();
	CHECK(Gizmo::GetRegisteredVertices() == 21);

	GizmoRenderer2D::BeginCommandFrame();
	CHECK(Gizmo::GetRegisteredVertices() == 0);
}

TEST_CASE("Gizmo refresh base survives paused frames until simulation resumes") {
	[[maybe_unused]] GizmoStateGuard guard;
	Gizmo::Clear();
	Gizmo::SetEnabled(true);
	Gizmo::SetLayer(GizmoLayer::Shared);

	Gizmo::DrawLine({ 0.0f, 0.0f }, { 1.0f, 1.0f });
	GizmoRenderer2D::CaptureRefreshBase();
	GizmoRenderer2D::EndFrame();

	for (int pausedFrame = 0; pausedFrame < 3; ++pausedFrame) {
		Gizmo::SetLayer(GizmoLayer::EditorOnly);
		Gizmo::DrawWireSquare({ 0.0f, 0.0f }, { 1.0f, 1.0f });
		CHECK(Gizmo::GetRegisteredVertices() == 5);

		GizmoRenderer2D::EndFrame();
		CHECK(Gizmo::GetRegisteredVertices() == 1);
	}

	GizmoRenderer2D::BeginCommandFrame();
	CHECK(Gizmo::GetRegisteredVertices() == 0);
}
