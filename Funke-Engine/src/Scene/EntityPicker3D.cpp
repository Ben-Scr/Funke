#include "pch.hpp"
#include "Scene/EntityPicker3D.hpp"

#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Graphics/SkinnedMeshRendererComponent.hpp"
#include "Components/Tags.hpp"
#include "Graphics/CustomShaderLibrary.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/SkeletalAnimation.hpp"
#include "Scene/Scene.hpp"
#include "Systems/SkinnedMeshAnimationSystem.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Funke {
	namespace {
		constexpr glm::vec3 k_ProxyHalfExtents{ 0.25f, 0.25f, 0.25f };
		// The plane is flat; give it a sliver of thickness so edge-on clicks still land.
		constexpr float k_PlaneHalfThickness = 0.01f;

		// Flat meshes get the same sliver of thickness as the plane.
		LocalBounds3D FromMinMax(const glm::vec3& minPoint, const glm::vec3& maxPoint) {
			return { (maxPoint + minPoint) * 0.5f, glm::max((maxPoint - minPoint) * 0.5f, glm::vec3(k_PlaneHalfThickness)) };
		}
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
		const auto* skinned = mesh ? nullptr : scene.GetRegistry().try_get<SkinnedMeshRendererComponent>(entity);
		if (!mesh && !skinned) {
			return { glm::vec3(0.0f), k_ProxyHalfExtents };
		}
		LocalBounds3D bounds = mesh ? MeshBounds(mesh) : SkinnedBounds(*skinned);
		// A shader's fn vertex moves the drawn surface by up to its @displacement.
		if (const Material* material = MaterialLibrary::Get(mesh ? mesh->Material : skinned->Material); material && material->Shader != 0) {
			if (const ShaderDescription* description = CustomShaderLibrary::GetDescription(material->Shader);
				description && description->HasVertexFunction && description->Displacement > 0.0f) {
				bounds.HalfExtents += glm::vec3(description->Displacement);
			}
		}
		return bounds;
	}

	LocalBounds3D EntityPicker3D::GetHierarchyBounds(const Scene& scene, EntityHandle entity) {
		const auto& registry = scene.GetRegistry();
		const auto* root = registry.try_get<Transform3DComponent>(entity);
		if (!root || std::abs(glm::determinant(root->WorldMatrix)) < 1e-12f) {
			return GetLocalBounds(scene, entity);
		}
		const glm::mat4 toRoot = glm::inverse(root->WorldMatrix);
		// A disabled selection still shows its whole extent; under an enabled one, disabled children draw nothing.
		const bool skipDisabled = !registry.all_of<DisabledTag>(entity);
		glm::vec3 minPoint(std::numeric_limits<float>::max());
		glm::vec3 maxPoint(std::numeric_limits<float>::lowest());
		bool any = false;
		std::vector<EntityHandle> pending{ entity };
		while (!pending.empty()) {
			const EntityHandle current = pending.back();
			pending.pop_back();
			if (!registry.valid(current) || (current != entity && skipDisabled && registry.all_of<DisabledTag>(current))) continue;
			if (const auto* hierarchy = registry.try_get<HierarchyComponent>(current)) {
				pending.insert(pending.end(), hierarchy->Children.begin(), hierarchy->Children.end());
			}
			const auto* transform = registry.try_get<Transform3DComponent>(current);
			if (!transform || !registry.any_of<MeshRendererComponent, SkinnedMeshRendererComponent>(current)) continue;

			const LocalBounds3D local = GetLocalBounds(scene, current);
			const glm::mat4 m = current == entity ? glm::mat4(1.0f) : toRoot * transform->WorldMatrix;
			const glm::vec3 center(m * glm::vec4(local.Center, 1.0f));
			const glm::vec3 half = glm::abs(glm::vec3(m[0])) * local.HalfExtents.x + glm::abs(glm::vec3(m[1])) * local.HalfExtents.y
				+ glm::abs(glm::vec3(m[2])) * local.HalfExtents.z;
			minPoint = glm::min(minPoint, center - half);
			maxPoint = glm::max(maxPoint, center + half);
			any = true;
		}
		if (!any) {
			return GetLocalBounds(scene, entity);
		}
		return { (minPoint + maxPoint) * 0.5f, (maxPoint - minPoint) * 0.5f };
	}

	LocalBounds3D EntityPicker3D::AssetBounds(uint64_t meshGuid) {
		const MeshAsset* asset = MeshLibrary::Get(meshGuid);
		if (!asset) {
			return { glm::vec3(0.0f), k_ProxyHalfExtents };
		}
		return FromMinMax(asset->BoundsMin, asset->BoundsMax);
	}

	LocalBounds3D EntityPicker3D::SkinnedBounds(const SkinnedMeshRendererComponent& skinned) {
		const MeshAsset* asset = MeshLibrary::Get(skinned.Model);
		if (!asset || !asset->Skin) {
			return AssetBounds(skinned.Model);
		}
		// The rest-pose box misses limbs once a clip moves them, including the clip frame edit mode shows.
		const SkinnedMeshData& data = *asset->Skin;
		thread_local std::vector<glm::mat4> t_Nodes;
		thread_local std::vector<glm::mat4> t_Joints;
		SkeletalAnimation::EvaluatePose(data, SkinnedMeshAnimationSystem::GetDrawnPose(skinned, data), t_Nodes);
		SkeletalAnimation::ComputeJointMatrices(data, t_Nodes, t_Joints);
		glm::vec3 minPoint, maxPoint;
		if (!SkeletalAnimation::PosedBounds(data, t_Joints, minPoint, maxPoint)) {
			return AssetBounds(skinned.Model);
		}
		return FromMinMax(minPoint, maxPoint);
	}

	LocalBounds3D EntityPicker3D::MeshBounds(const MeshRendererComponent* mesh) {
		if (mesh->CustomMesh != 0) {
			return AssetBounds(mesh->CustomMesh);
		}
		if (mesh->Mesh == PrimitiveMeshType::Plane) {
			return { glm::vec3(0.0f), { 0.5f, k_PlaneHalfThickness, 0.5f } };
		}
		return { glm::vec3(0.0f), { 0.5f, 0.5f, 0.5f } };
	}

	namespace {
		template<typename OnHit>
		void ForEachRayHit(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, OnHit&& onHit) {
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
				const LocalBounds3D bounds = EntityPicker3D::GetLocalBounds(scene, entity);
				if (EntityPicker3D::IntersectRayBox(localOrigin - bounds.Center, localDirection, bounds.HalfExtents, t)) {
					onHit(entity, t);
				}
			}
		}
	}

	EntityHandle EntityPicker3D::Pick(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float* outDistance) {
		EntityHandle best = entt::null;
		float bestT = std::numeric_limits<float>::infinity();
		ForEachRayHit(scene, origin, direction, [&](EntityHandle entity, float t) {
			if (t < bestT) {
				bestT = t;
				best = entity;
			}
		});

		if (outDistance && best != entt::null) {
			*outDistance = bestT * glm::length(direction);
		}
		return best;
	}

	void EntityPicker3D::PickAll(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, std::vector<EntityHandle>& outEntities) {
		std::vector<std::pair<float, EntityHandle>> hits;
		ForEachRayHit(scene, origin, direction, [&](EntityHandle entity, float t) { hits.emplace_back(t, entity); });
		std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
			if (a.first != b.first) return a.first < b.first;
			return static_cast<uint32_t>(a.second) > static_cast<uint32_t>(b.second);
		});
		outEntities.clear();
		outEntities.reserve(hits.size());
		for (const auto& hit : hits) outEntities.push_back(hit.second);
	}
}
