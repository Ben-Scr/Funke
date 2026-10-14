#include <doctest/doctest.h>

#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Graphics/PrimitiveMesh.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Entity.hpp"
#include "Scene/EntityPicker3D.hpp"
#include "Scene/Scene.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"

#include <cmath>

using namespace Index;

namespace {
	bool Near(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-4f) {
		return glm::length(a - b) <= epsilon;
	}
}

TEST_CASE("Primitive meshes are closed CCW triangle lists with outward normals") {
	for (size_t i = 0; i < k_PrimitiveMeshTypeCount; ++i) {
		const auto type = static_cast<PrimitiveMeshType>(i);
		CAPTURE(PrimitiveMeshTypeName(type));
		const PrimitiveMeshData mesh = BuildPrimitiveMesh(type);
		REQUIRE_FALSE(mesh.Vertices.empty());
		REQUIRE(mesh.Indices.size() % 3 == 0);

		for (size_t t = 0; t < mesh.Indices.size(); t += 3) {
			const uint32_t ia = mesh.Indices[t], ib = mesh.Indices[t + 1], ic = mesh.Indices[t + 2];
			REQUIRE(ia < mesh.Vertices.size());
			REQUIRE(ib < mesh.Vertices.size());
			REQUIRE(ic < mesh.Vertices.size());
			const auto position = [&](uint32_t index) {
				const MeshVertex& v = mesh.Vertices[index];
				return glm::vec3(v.Position[0], v.Position[1], v.Position[2]);
			};
			const glm::vec3 faceNormal = glm::cross(position(ib) - position(ia), position(ic) - position(ia));
			if (glm::length(faceNormal) < 1e-7f) {
				continue; // sphere poles collapse to degenerate slivers
			}
			const MeshVertex& v = mesh.Vertices[ia];
			const glm::vec3 vertexNormal(v.Normal[0], v.Normal[1], v.Normal[2]);
			CHECK(glm::dot(faceNormal, vertexNormal) > 0.0f);
		}
		for (const MeshVertex& v : mesh.Vertices) {
			CHECK(std::abs(glm::length(glm::vec3(v.Normal[0], v.Normal[1], v.Normal[2])) - 1.0f) < 1e-4f);
		}
	}
}

TEST_CASE("Transform3D local matrix decomposes back to its fields") {
	Transform3DComponent transform;
	transform.Position = { 1.5f, -2.0f, 3.25f };
	transform.Rotation = { 20.0f, -35.0f, 10.0f };
	transform.Scale = { 2.0f, 0.5f, 1.25f };

	Vec3 position, rotation, scale;
	Transform3DComponent::Decompose(transform.GetLocalMatrix(), position, rotation, scale);
	CHECK(Near(position, transform.Position));
	CHECK(Near(scale, transform.Scale));
	CHECK(Near(rotation, transform.Rotation, 1e-2f));
}

TEST_CASE("Camera3D perspective maps near and far clip to WebGPU depth 0 and 1") {
	Camera3DComponent camera;
	camera.NearClip = 0.5f;
	camera.FarClip = 50.0f;
	const glm::mat4 projection = camera.GetProjectionMatrix(16.0f / 9.0f);

	const glm::vec4 nearClip = projection * glm::vec4(0.0f, 0.0f, -0.5f, 1.0f);
	const glm::vec4 farClip = projection * glm::vec4(0.0f, 0.0f, -50.0f, 1.0f);
	CHECK(nearClip.z / nearClip.w == doctest::Approx(0.0f).epsilon(1e-4));
	CHECK(farClip.z / farClip.w == doctest::Approx(1.0f).epsilon(1e-4));

	glm::mat4 world(1.0f);
	world[3] = glm::vec4(4.0f, 5.0f, 6.0f, 1.0f);
	const glm::vec4 eyeOrigin = Camera3DComponent::ViewFromWorld(world) * glm::vec4(4.0f, 5.0f, 6.0f, 1.0f);
	CHECK(Near(glm::vec3(eyeOrigin), glm::vec3(0.0f)));
}

TEST_CASE("Transform3D hierarchy passes the parent matrix through entities without a Transform3D") {
	auto scene = Scene::CreateDetachedScene("ThreeDHierarchy");
	Entity root = scene->CreateEmptyEntity();
	root.AddComponent<Transform3DComponent>().Position = { 10.0f, 0.0f, 0.0f };
	Entity group = scene->CreateEmptyEntity();
	group.SetParent(root);
	Entity child = scene->CreateEmptyEntity();
	child.AddComponent<Transform3DComponent>().Position = { 0.0f, 2.0f, 0.0f };
	child.SetParent(group);

	Transform3DHierarchySystem::Propagate(*scene);
	CHECK(Near(child.GetComponent<Transform3DComponent>().GetWorldPosition(), { 10.0f, 2.0f, 0.0f }));

	root.GetComponent<Transform3DComponent>().Rotation = { 0.0f, 90.0f, 0.0f };
	child.GetComponent<Transform3DComponent>().Position = { 0.0f, 0.0f, -1.0f };
	Transform3DHierarchySystem::Propagate(*scene);
	// Yaw +90 turns local -Z into world -X.
	CHECK(Near(child.GetComponent<Transform3DComponent>().GetWorldPosition(), { 9.0f, 0.0f, 0.0f }));
}

TEST_CASE("EntityPicker3D returns the nearest mesh along the ray") {
	auto scene = Scene::CreateDetachedScene("ThreeDPicking");
	Entity nearCube = scene->CreateEmptyEntity();
	nearCube.AddComponent<Transform3DComponent>().Position = { 0.0f, 0.0f, -3.0f };
	nearCube.AddComponent<MeshRendererComponent>();
	Entity farCube = scene->CreateEmptyEntity();
	farCube.AddComponent<Transform3DComponent>().Position = { 0.0f, 0.0f, -8.0f };
	farCube.AddComponent<MeshRendererComponent>();
	Entity offAxis = scene->CreateEmptyEntity();
	offAxis.AddComponent<Transform3DComponent>().Position = { 5.0f, 0.0f, -1.0f };
	offAxis.AddComponent<MeshRendererComponent>();
	Transform3DHierarchySystem::Propagate(*scene);

	float distance = 0.0f;
	CHECK(EntityPicker3D::Pick(*scene, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, &distance) == nearCube.GetHandle());
	CHECK(distance == doctest::Approx(2.5f));
	CHECK(EntityPicker3D::Pick(*scene, { 0.0f, 3.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }) == entt::null);

	nearCube.GetComponent<Transform3DComponent>().Scale = { 0.1f, 0.1f, 0.1f };
	Transform3DHierarchySystem::Propagate(*scene);
	CHECK(EntityPicker3D::Pick(*scene, { 0.0f, 0.2f, 0.0f }, { 0.0f, 0.0f, -1.0f }) == farCube.GetHandle());
}

TEST_CASE("3D components register after every 2D component and conflict with 2D transforms") {
	ComponentRegistry registry;
	RegisterBuiltInComponents(registry);

	const ComponentInfo* transform3D = registry.FindBySerializedName("Transform3D");
	const ComponentInfo* transform2D = registry.FindBySerializedName("Transform2D");
	REQUIRE(transform3D);
	REQUIRE(transform2D);
	CHECK(registry.FindBySerializedName("Camera3D"));
	CHECK(registry.FindBySerializedName("MeshRenderer"));
	CHECK(registry.FindBySerializedName("DirectionalLight"));
	CHECK(transform3D->serialize);
	CHECK(transform3D->deserialize);
	CHECK(transform3D->typeIdU32 > transform2D->typeIdU32);
	CHECK(registry.TypesConflict(transform3D->typeId, transform2D->typeId));
}
