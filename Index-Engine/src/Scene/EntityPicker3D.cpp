#include "pch.hpp"
#include "Scene/EntityPicker3D.hpp"

#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Tags.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Index {
	namespace {
		constexpr glm::vec3 k_ProxyHalfExtents{ 0.25f, 0.25f, 0.25f };
		// The plane is flat; give it a sliver of thickness so edge-on clicks still land.
		constexpr float k_PlaneHalfThickness = 0.01f;
	}

	bool EntityPicker3D::IntersectRayBox(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& halfExtents, float& outT) {
		float tMin = -std::numeric_limits<float>::infinity();
		float tMax = std::numeric_limits<float>::infinity();
		for (int axis = 0; axis < 3; ++axis) {
			if (std::abs(direction[axis]) < 1e-12f) {
				if (origin[axis] < -halfExtents[axis] || origin[axis] > halfExtents[axis]) {
					return false;
				}
				continue;
			}
			const float inverse = 1.0f / direction[axis];
			float t0 = (-halfExtents[axis] - origin[axis]) * inverse;
			float t1 = (halfExtents[axis] - origin[axis]) * inverse;
			if (t0 > t1) std::swap(t0, t1);
			tMin = std::max(tMin, t0);
			tMax = std::min(tMax, t1);
			if (tMin > tMax) {
				return false;
			}
		}
		if (tMax < 0.0f) {
			return false;
		}
		outT = tMin >= 0.0f ? tMin : tMax;
		return true;
	}

	LocalBounds3D EntityPicker3D::GetLocalBounds(const Scene& scene, EntityHandle entity) {
		const auto* mesh = scene.GetRegistry().try_get<MeshRendererComponent>(entity);
		if (!mesh) {
			return { glm::vec3(0.0f), k_ProxyHalfExtents };
		}
		if (mesh->CustomMesh != 0) {
			const MeshAsset* asset = MeshLibrary::Get(mesh->CustomMesh);
			if (!asset) {
				return { glm::vec3(0.0f), k_ProxyHalfExtents };
			}
			// Flat meshes get the same sliver of thickness as the plane.
			const glm::vec3 half = glm::max((asset->BoundsMax - asset->BoundsMin) * 0.5f, glm::vec3(k_PlaneHalfThickness));
			return { (asset->BoundsMax + asset->BoundsMin) * 0.5f, half };
		}
		if (mesh->Mesh == PrimitiveMeshType::Plane) {
			return { glm::vec3(0.0f), { 0.5f, k_PlaneHalfThickness, 0.5f } };
		}
		return { glm::vec3(0.0f), { 0.5f, 0.5f, 0.5f } };
	}

	EntityHandle EntityPicker3D::Pick(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float* outDistance) {
		EntityHandle best = entt::null;
		float bestT = std::numeric_limits<float>::infinity();

		const auto view = scene.GetRegistry().view<const Transform3DComponent>(entt::exclude<DisabledTag>);
		for (EntityHandle entity : view) {
			const glm::mat4& world = view.get<const Transform3DComponent>(entity).WorldMatrix;
			const float determinant = glm::determinant(world);
			if (std::abs(determinant) < 1e-12f) {
				continue;
			}
			// Affine inverse keeps the ray parameter t identical in local and world space.
			const glm::mat4 inverse = glm::inverse(world);
			const glm::vec3 localOrigin = glm::vec3(inverse * glm::vec4(origin, 1.0f));
			const glm::vec3 localDirection = glm::vec3(inverse * glm::vec4(direction, 0.0f));

			float t = 0.0f;
			const LocalBounds3D bounds = GetLocalBounds(scene, entity);
			if (IntersectRayBox(localOrigin - bounds.Center, localDirection, bounds.HalfExtents, t) && t < bestT) {
				bestT = t;
				best = entity;
			}
		}

		if (outDistance && best != entt::null) {
			*outDistance = bestT * glm::length(direction);
		}
		return best;
	}
}
