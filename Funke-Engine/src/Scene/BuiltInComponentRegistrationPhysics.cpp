#include "pch.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"

#include "Components/General/General.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Physics/PhysicsTypes.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>
#include <vector>

// Box2D-backed bodies and colliders.
// Split out of BuiltInComponentRegistration.cpp, which owns the call order.
namespace Funke::BuiltInComponentRegistrationInternal {
	namespace {
		constexpr int k_InspectorPhysicsLayerCount = 16;

		std::shared_ptr<EnumDescriptor> MakePhysicsLayerOptions() {
			auto options = std::make_shared<EnumDescriptor>();
			options->IsFlags = true;
			for (int i = 0; i < k_InspectorPhysicsLayerCount; ++i) {
				options->Options.push_back({ i == 0 ? std::string("Layer 0 (Default)") : "Layer " + std::to_string(i), int64_t{ 1 } << i });
			}
			return options;
		}

		// Collider layer bitmask as a checkbox dropdown instead of a raw integer.
		template <typename TCollider>
		PropertyDescriptor MakeColliderLayerMask(const std::string& name, const std::string& displayName,
			uint64_t (Collider2D::*getter)() const, void (Collider2D::*setter)(uint64_t))
		{
			static const std::shared_ptr<EnumDescriptor> s_Options = MakePhysicsLayerOptions();
			PropertyDescriptor desc;
			desc.Name = name;
			desc.DisplayName = displayName;
			desc.Type = PropertyType::FlagEnum;
			desc.Metadata.Enum = s_Options;
			desc.Get = [getter](const Entity& e) {
				PropertyValue v;
				v.Type = PropertyType::FlagEnum;
				v.IntValue = static_cast<int64_t>((e.GetComponent<TCollider>().*getter)());
				return v;
			};
			desc.Set = [setter](Entity& e, const PropertyValue& v) {
				(e.GetComponent<TCollider>().*setter)(static_cast<uint64_t>(v.IntValue));
			};
			return desc;
		}
		// Box/Circle colliders inherit Collider2D (virtual dtor -> non-trivially-destructible), so the
		// auto byte-emplacers are skipped and any prefab containing one was rejected as unbakeable.
		// They own no heap (all-POD data), and their on_construct hook rebuilds the Box2D body/shape
		// from the logical state. So bake them: copy the data members (NOT the vtable pointer), and
		// null the stale Box2D handles so OnXColliderConstruct recreates a fresh body/shape on hydrate.
		// NOTE: not usable for PolygonCollider2DComponent — it owns std::vector vertices (heap).
		template <typename T>
		void RegisterColliderByteEmplacer(ComponentRegistry& registry) {
			static_assert(std::is_base_of_v<Collider2D, T>,
				"RegisterColliderByteEmplacer is only for Collider2D-derived components");
			static_assert(std::is_polymorphic_v<T>, "expected a polymorphic collider (vptr at offset 0)");
			const std::type_index targetId(typeid(T));
			registry.ForEachComponentInfo(
				[targetId](const std::type_index& id, ComponentInfo& info) {
					if (id != targetId) return;
					info.writeBytes = [](const entt::registry& registry, EntityHandle entity,
						std::vector<uint8_t>& out) -> bool {
						const T* component = registry.try_get<T>(entity);
						if (component == nullptr) return false;
						const auto* bytes = reinterpret_cast<const uint8_t*>(component);
						out.insert(out.end(), bytes, bytes + sizeof(T));
						return true;
					};
					info.emplaceFromBytes = [](entt::registry& registry, EntityHandle entity,
						const void* bytes, std::size_t size) {
						T component; // fresh vtable ptr + null Box2D handles
						// Restore data members WITHOUT overwriting the freshly-constructed vtable
						// pointer (single inheritance -> vptr at offset 0; writing it is UB).
						constexpr std::size_t kVptr = sizeof(void*);
						const std::size_t total = std::min(size, sizeof(T));
						if (total > kVptr) {
							std::memcpy(reinterpret_cast<uint8_t*>(&component) + kVptr,
								static_cast<const uint8_t*>(bytes) + kVptr, total - kVptr);
						}
						// Drop the bake tree's stale handles; the construct hook rebuilds them from the
						// restored logical state (size/radius/center/material) and shares the body.
						Collider2D& base = component;
						base.m_BodyId = b2_nullBodyId;
						base.m_ShapeId = b2_nullShapeId;
						base.m_EntityHandle = entity;
						registry.emplace_or_replace<T>(entity, std::move(component));
					};
				});
		}
	}

	void RegisterPhysics(ComponentRegistry& registry) {
		using Funke::Codegen::RegisterComponent;

		// ── Physics (Box2D-backed) ──────────────────────────────────

		RegisterComponent<Rigidbody2DComponent>(registry, "Rigidbody 2D",
			ComponentCategory::Component, "Physics", "Rigidbody2D",
			{
				Properties::MakeWith<Vec2>("Position", "Position",
					[](const Entity& e) { return e.GetComponent<Rigidbody2DComponent>().GetPosition(); },
					[](Entity& e, const Vec2& v) {
						e.GetComponent<Rigidbody2DComponent>().SetPosition(v);
					},
					Properties::Meta::ReadOnly()),
				Properties::MakeWith<Vec2>("Velocity", "Velocity",
					[](const Entity& e) { return e.GetComponent<Rigidbody2DComponent>().GetVelocity(); },
					[](Entity& e, const Vec2& v) {
						e.GetComponent<Rigidbody2DComponent>().SetVelocity(v);
					}),
				Properties::MakeWith<float>("Rotation", "Rotation",
					[](const Entity& e) { return e.GetComponent<Rigidbody2DComponent>().GetRotation(); },
					[](Entity& e, float v) {
						e.GetComponent<Rigidbody2DComponent>().SetRotation(v);
					},
					Properties::Meta::ReadOnly().WithDragSpeed(0.01f)),
				Properties::MakeWith<float>("Mass", "Mass",
					[](const Entity& e) { return e.GetComponent<Rigidbody2DComponent>().GetMass(); },
					[](Entity& e, float v) {
						e.GetComponent<Rigidbody2DComponent>().SetMass(v);
					},
					Properties::Meta::DragSpeed(0.01f).WithDragMin(0.0)),
				Properties::MakeWith<float>("GravityScale", "Gravity Scale",
					[](const Entity& e) { return e.GetComponent<Rigidbody2DComponent>().GetGravityScale(); },
					[](Entity& e, float v) {
						e.GetComponent<Rigidbody2DComponent>().SetGravityScale(v);
					},
					Properties::Meta::DragSpeed(0.01f)),
				Properties::MakeWith<BodyType>("BodyType", "Body Type",
					[](const Entity& e) {
						return e.GetComponent<Rigidbody2DComponent>().GetBodyType();
					},
					[](Entity& e, BodyType v) {
						e.GetComponent<Rigidbody2DComponent>().SetBodyType(v);
					}),
				// Constraints — route through Box2D motion-locks. The flags
				// are stored on the live body, so changes are picked up by
				// the solver on the next step without an extra dirty bit.
				Properties::MakeWith<bool>("FreezePositionX", "Freeze Position X",
					[](const Entity& e) { return e.GetComponent<Rigidbody2DComponent>().GetFreezePositionX(); },
					[](Entity& e, bool v) {
						e.GetComponent<Rigidbody2DComponent>().SetFreezePositionX(v);
					}),
				Properties::MakeWith<bool>("FreezePositionY", "Freeze Position Y",
					[](const Entity& e) { return e.GetComponent<Rigidbody2DComponent>().GetFreezePositionY(); },
					[](Entity& e, bool v) {
						e.GetComponent<Rigidbody2DComponent>().SetFreezePositionY(v);
					}),
				Properties::MakeWith<bool>("FreezeRotation", "Freeze Rotation",
					[](const Entity& e) { return e.GetComponent<Rigidbody2DComponent>().GetFreezeRotation(); },
					[](Entity& e, bool v) {
						e.GetComponent<Rigidbody2DComponent>().SetFreezeRotation(v);
					}),
			});

		RegisterComponent<BoxCollider2DComponent>(registry, "Box Collider 2D",
			ComponentCategory::Component, "Physics", "BoxCollider2D",
			{
				Properties::MakeWith<Vec2>("Offset", "Offset",
					[](const Entity& e) { return e.GetComponent<BoxCollider2DComponent>().GetCenter(); },
					[](Entity& e, const Vec2& v) {
						if (Scene* s = e.GetScene()) {
							e.GetComponent<BoxCollider2DComponent>().SetCenter(v, *s);
						}
					},
					Properties::Meta::DragSpeed(0.05f).WithHideStepperButtons(true)),
				Properties::MakeWith<Vec2>("Size", "Size",
					[](const Entity& e) {
						const Scene* s = e.GetScene();
						if (!s) return Vec2{ 1.0f, 1.0f };
						return e.GetComponent<BoxCollider2DComponent>().GetLocalScale(*s);
					},
					[](Entity& e, const Vec2& localSize) {
						Scene* s = e.GetScene();
						if (!s) return;
						const Vec2 clamped{ std::max(localSize.x, 0.001f), std::max(localSize.y, 0.001f) };
						e.GetComponent<BoxCollider2DComponent>().SetScale(clamped, *s);
					},
					Properties::Meta::Clamp(0.001, 1000.0, 0.05f).WithHideStepperButtons(true)),
				Properties::MakeWith<bool>("Sensor", "Sensor",
					[](const Entity& e) { return e.GetComponent<BoxCollider2DComponent>().IsSensor(); },
					[](Entity& e, bool v) {
						auto& col = e.GetComponent<BoxCollider2DComponent>();
						if (Scene* s = e.GetScene()) col.SetSensor(v, *s);
					}),
				Properties::MakeWith<bool>("ContactEvents", "Contact Events",
					[](const Entity& e) { return e.GetComponent<BoxCollider2DComponent>().CanRegisterContacts(); },
					[](Entity& e, bool v) {
						e.GetComponent<BoxCollider2DComponent>().SetRegisterContacts(v);
					}),
				Properties::MakeWith<bool>("Enabled", "Collider Enabled",
					[](const Entity& e) { return e.GetComponent<BoxCollider2DComponent>().IsEnabled(); },
					[](Entity& e, bool v) {
						e.GetComponent<BoxCollider2DComponent>().SetEnabled(v);
					}),
				Properties::MakeWith<float>("Friction", "Friction",
					[](const Entity& e) { return e.GetComponent<BoxCollider2DComponent>().GetFriction(); },
					[](Entity& e, float v) {
						e.GetComponent<BoxCollider2DComponent>().SetFriction(v);
					},
					Properties::Meta::Clamp(0.0, 10.0, 0.01f)),
				Properties::MakeWith<float>("Bounciness", "Bounciness",
					[](const Entity& e) { return e.GetComponent<BoxCollider2DComponent>().GetBounciness(); },
					[](Entity& e, float v) {
						e.GetComponent<BoxCollider2DComponent>().SetBounciness(v);
					},
					Properties::Meta::Clamp(0.0, 1.0, 0.01f)),
				MakeColliderLayerMask<BoxCollider2DComponent>("LayerMask", "Layer", &Collider2D::GetLayer, &Collider2D::SetLayer),
				MakeColliderLayerMask<BoxCollider2DComponent>("CollisionMask", "Collides With", &Collider2D::GetCollisionMask, &Collider2D::SetCollisionMask),
			});

		RegisterComponent<CircleCollider2DComponent>(registry, "Circle Collider 2D",
			ComponentCategory::Component, "Physics", "CircleCollider2D",
			{
				Properties::MakeWith<Vec2>("Offset", "Offset",
					[](const Entity& e) { return e.GetComponent<CircleCollider2DComponent>().GetCenter(); },
					[](Entity& e, const Vec2& v) {
						if (Scene* s = e.GetScene()) {
							e.GetComponent<CircleCollider2DComponent>().SetCenter(v, *s);
						}
					},
					Properties::Meta::DragSpeed(0.05f).WithHideStepperButtons(true)),
				Properties::MakeWith<float>("Radius", "Radius",
					[](const Entity& e) {
						const Scene* s = e.GetScene();
						if (!s) return 0.5f;
						return e.GetComponent<CircleCollider2DComponent>().GetLocalRadius(*s);
					},
					[](Entity& e, float radius) {
						Scene* s = e.GetScene();
						if (!s) return;
						e.GetComponent<CircleCollider2DComponent>().SetRadius(radius, *s);
					},
					Properties::Meta::DragSpeed(0.05f)),
				Properties::MakeWith<bool>("Sensor", "Sensor",
					[](const Entity& e) { return e.GetComponent<CircleCollider2DComponent>().IsSensor(); },
					[](Entity& e, bool v) {
						auto& col = e.GetComponent<CircleCollider2DComponent>();
						if (Scene* s = e.GetScene()) col.SetSensor(v, *s);
					}),
				Properties::MakeWith<bool>("ContactEvents", "Contact Events",
					[](const Entity& e) { return e.GetComponent<CircleCollider2DComponent>().CanRegisterContacts(); },
					[](Entity& e, bool v) {
						e.GetComponent<CircleCollider2DComponent>().SetRegisterContacts(v);
					}),
				Properties::MakeWith<bool>("Enabled", "Collider Enabled",
					[](const Entity& e) { return e.GetComponent<CircleCollider2DComponent>().IsEnabled(); },
					[](Entity& e, bool v) {
						e.GetComponent<CircleCollider2DComponent>().SetEnabled(v);
					}),
				Properties::MakeWith<float>("Friction", "Friction",
					[](const Entity& e) { return e.GetComponent<CircleCollider2DComponent>().GetFriction(); },
					[](Entity& e, float v) {
						e.GetComponent<CircleCollider2DComponent>().SetFriction(v);
					},
					Properties::Meta::Clamp(0.0, 10.0, 0.01f)),
				Properties::MakeWith<float>("Bounciness", "Bounciness",
					[](const Entity& e) { return e.GetComponent<CircleCollider2DComponent>().GetBounciness(); },
					[](Entity& e, float v) {
						e.GetComponent<CircleCollider2DComponent>().SetBounciness(v);
					},
					Properties::Meta::Clamp(0.0, 1.0, 0.01f)),
				MakeColliderLayerMask<CircleCollider2DComponent>("LayerMask", "Layer", &Collider2D::GetLayer, &Collider2D::SetLayer),
				MakeColliderLayerMask<CircleCollider2DComponent>("CollisionMask", "Collides With", &Collider2D::GetCollisionMask, &Collider2D::SetCollisionMask),
			});

		// Make Box/Circle colliders bakeable so prefabs with physics spawn via the ECB fast path.
		// PolygonCollider2D stays on the slow path (Entity.Instantiate) — it owns heap vertex data.
		RegisterColliderByteEmplacer<BoxCollider2DComponent>(registry);
		RegisterColliderByteEmplacer<CircleCollider2DComponent>(registry);

		RegisterComponent<PolygonCollider2DComponent>(registry, "Polygon Collider 2D",
			ComponentCategory::Component, "Physics", "PolygonCollider2D",
			{
				Properties::MakeWith<Vec2>("Offset", "Offset",
					[](const Entity& e) { return e.GetComponent<PolygonCollider2DComponent>().GetCenter(); },
					[](Entity& e, const Vec2& v) {
						if (Scene* s = e.GetScene()) {
							e.GetComponent<PolygonCollider2DComponent>().SetCenter(v, *s);
						}
					},
					Properties::Meta::DragSpeed(0.05f).WithHideStepperButtons(true)),
				Properties::MakeWith<int>("Sides", "Sides",
					[](const Entity& e) { return e.GetComponent<PolygonCollider2DComponent>().GetSides(); },
					[](Entity& e, int v) {
						Scene* s = e.GetScene();
						if (!s) return;
						const int clamped = std::clamp(v,
							PolygonCollider2DComponent::k_MinVertices,
							PolygonCollider2DComponent::k_MaxVertices);
						e.GetComponent<PolygonCollider2DComponent>().SetSides(clamped, *s);
					},
					Properties::Meta::Clamp(
						static_cast<double>(PolygonCollider2DComponent::k_MinVertices),
						static_cast<double>(PolygonCollider2DComponent::k_MaxVertices),
						1.0f)),
				Properties::MakeWith<Vec2>("Size", "Size",
					[](const Entity& e) {
						const Scene* s = e.GetScene();
						if (!s) return Vec2{ 1.0f, 1.0f };
						return e.GetComponent<PolygonCollider2DComponent>().GetLocalSize(*s);
					},
					[](Entity& e, const Vec2& localSize) {
						Scene* s = e.GetScene();
						if (!s) return;
						e.GetComponent<PolygonCollider2DComponent>().SetSize(localSize, *s);
					},
					Properties::Meta::DragSpeed(0.05f).WithHideStepperButtons(true)),
				Properties::MakeWith<bool>("Sensor", "Sensor",
					[](const Entity& e) { return e.GetComponent<PolygonCollider2DComponent>().IsSensor(); },
					[](Entity& e, bool v) {
						auto& col = e.GetComponent<PolygonCollider2DComponent>();
						if (Scene* s = e.GetScene()) col.SetSensor(v, *s);
					}),
				Properties::MakeWith<bool>("ContactEvents", "Contact Events",
					[](const Entity& e) { return e.GetComponent<PolygonCollider2DComponent>().CanRegisterContacts(); },
					[](Entity& e, bool v) {
						e.GetComponent<PolygonCollider2DComponent>().SetRegisterContacts(v);
					}),
				Properties::MakeWith<bool>("Enabled", "Collider Enabled",
					[](const Entity& e) { return e.GetComponent<PolygonCollider2DComponent>().IsEnabled(); },
					[](Entity& e, bool v) {
						e.GetComponent<PolygonCollider2DComponent>().SetEnabled(v);
					}),
				Properties::MakeWith<float>("Friction", "Friction",
					[](const Entity& e) { return e.GetComponent<PolygonCollider2DComponent>().GetFriction(); },
					[](Entity& e, float v) {
						e.GetComponent<PolygonCollider2DComponent>().SetFriction(v);
					},
					Properties::Meta::Clamp(0.0, 10.0, 0.01f)),
				Properties::MakeWith<float>("Bounciness", "Bounciness",
					[](const Entity& e) { return e.GetComponent<PolygonCollider2DComponent>().GetBounciness(); },
					[](Entity& e, float v) {
						e.GetComponent<PolygonCollider2DComponent>().SetBounciness(v);
					},
					Properties::Meta::Clamp(0.0, 1.0, 0.01f)),
				MakeColliderLayerMask<PolygonCollider2DComponent>("LayerMask", "Layer", &Collider2D::GetLayer, &Collider2D::SetLayer),
				MakeColliderLayerMask<PolygonCollider2DComponent>("CollisionMask", "Collides With", &Collider2D::GetCollisionMask, &Collider2D::SetCollisionMask),
			});
	}
}
