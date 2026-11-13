#include <doctest/doctest.h>

#include "Graphics/Gizmo.hpp"
#include "Graphics/GizmoRenderer.hpp"

using namespace Funke;

namespace {
	struct GizmoStateGuard {
		bool Enabled = Gizmo::IsEnabled();
		GizmoLayer Layer = Gizmo::GetLayer();
		Color SavedColor = Gizmo::GetColor();
		size_t Budget = Gizmo::GetMaxVertices();

		~GizmoStateGuard() {
			Gizmo::Clear();
			Gizmo::SetEnabled(Enabled);
			Gizmo::SetLayer(Layer);
			Gizmo::SetColor(SavedColor);
			Gizmo::SetMaxVertices(Budget);
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

TEST_CASE("3D gizmos preserve depth, rotation, color and closed sphere rings") {
	GizmoStateGuard guard;
	Gizmo::Clear();
	Gizmo::SetEnabled(true);
	Gizmo::SetLayer(GizmoLayer::Shared);
	Gizmo::SetMaxVertices(1000);
	Gizmo::SetColor({ 0.2f, 0.3f, 0.4f, 0.5f });
	// A zero matrix disables clipping, so geometry can be inspected without a GPU.
	const glm::mat4 unbounded(0.0f);
	Gizmo::DrawBox({ 1, 2, 3 }, { 2, 4, 6 }, { 0, 0, 90 });
	auto lines = GizmoRenderer2D::BuildLines3D(unbounded);
	REQUIRE(lines.size() == 12);
	CHECK(lines[0].Start.x == doctest::Approx(3.0f));
	CHECK(lines[0].Start.y == doctest::Approx(1.0f));
	CHECK(lines[0].Start.z == doctest::Approx(0.0f));
	CHECK(lines[0].End.x == doctest::Approx(3.0f));
	CHECK(lines[0].End.y == doctest::Approx(3.0f));
	CHECK(lines[0].Color.a == doctest::Approx(0.5f));
	Gizmo::Clear();
	Gizmo::DrawSphere({ 1, 2, 3 }, 2.0f, 8);
	lines = GizmoRenderer2D::BuildLines3D(unbounded);
	REQUIRE(lines.size() == 24);
	for (const auto& line : lines) {
		CHECK(glm::length(line.Start - Vec3(1, 2, 3)) == doctest::Approx(2.0f));
		CHECK(glm::length(line.End - Vec3(1, 2, 3)) == doctest::Approx(2.0f));
	}
	for (int ring = 0; ring < 3; ++ring)
		CHECK((lines[ring * 8].Start == lines[ring * 8 + 7].End));
}

TEST_CASE("3D gizmos cull before budgeting and filter layers independently per view") {
	GizmoStateGuard guard;
	Gizmo::Clear();
	Gizmo::SetEnabled(true);
	Gizmo::SetLayer(GizmoLayer::Shared);
	Gizmo::SetMaxVertices(1);
	Gizmo::DrawLine(Vec3(10, 0, 0.5f), Vec3(11, 0, 0.5f));
	Gizmo::DrawLine(Vec3(0, 0, 0.5f), Vec3(0.5f, 0, 0.5f));
	auto lines = GizmoRenderer2D::BuildLines3D(glm::mat4(1.0f));
	REQUIRE(lines.size() == 1);
	CHECK(lines[0].Start.x == 0.0f);
	lines = GizmoRenderer2D::BuildLines3D(glm::mat4(1.0f), GizmoLayerMask::Shared, Vec3(10, 0, 0));
	REQUIRE(lines.size() == 1);
	CHECK(lines[0].Start.x == 10.0f);
	CHECK(GizmoRenderer2D::BuildLines3D(glm::mat4(1.0f), GizmoLayerMask::EditorOnly).empty());
	Gizmo::SetMaxVertices(0);
	CHECK(GizmoRenderer2D::BuildLines3D(glm::mat4(1.0f)).empty());
	Gizmo::Clear();
	Gizmo::SetEnabled(false);
	Gizmo::DrawBox(Vec3(0), Vec3(1));
	Gizmo::DrawSphere(Vec3(0), 1.0f);
	Gizmo::DrawLine(Vec3(0), Vec3(1));
	CHECK(Gizmo::GetRegisteredVertices() == 0);
	Gizmo::SetLayer(GizmoLayer::EditorOnly);
	Gizmo::DrawBox(Vec3(0), Vec3(1));
	CHECK(Gizmo::GetRegisteredVertices() == 12);
}

TEST_CASE("3D gizmos survive refresh and clear at the next logical frame") {
	GizmoStateGuard guard;
	Gizmo::Clear();
	Gizmo::SetEnabled(true);
	Gizmo::SetLayer(GizmoLayer::Shared);
	Gizmo::SetMaxVertices(1000);
	Gizmo::DrawSphere(Vec3(0), 1.0f, -1); // clamps to three edges per ring
	Gizmo::DrawBox(Vec3(0), Vec3(1));
	Gizmo::DrawLine(Vec3(0), Vec3(1));
	CHECK(Gizmo::GetRegisteredVertices() == 22);
	GizmoRenderer2D::CaptureRefreshBase();
	Gizmo::DrawSphere(Vec3(0), 1.0f);
	GizmoRenderer2D::EndFrame();
	CHECK(GizmoRenderer2D::BuildLines3D(glm::mat4(0.0f)).size() == 22);
	GizmoRenderer2D::BeginCommandFrame();
	CHECK(GizmoRenderer2D::BuildLines3D(glm::mat4(0.0f)).empty());
	CHECK(Gizmo::GetRegisteredVertices() == 0);
	Gizmo::DrawSphere(Vec3(0), -1.0f);
	Gizmo::DrawSphere(Vec3(0), 0.0f);
	Gizmo::DrawSphere(Vec3(0), 1.0f, 2147483647);
	CHECK(Gizmo::GetRegisteredVertices() == 0);
}
