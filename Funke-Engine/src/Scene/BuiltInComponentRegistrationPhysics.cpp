#include "pch.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"

#include "Components/General/General.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Physics/Joint2DComponents.hpp"
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

		constexpr float k_RadToDeg = 57.29577951308232f;
		constexpr float k_DegToRad = 0.017453292519943295f;

		// Structural fields rebuild the live joint on the next physics step; the others are pushed into it at once.
		template <typename T, typename C, typename V>
		PropertyDescriptor JointField(const std::string& name, const std::string& displayName, V C::* member,
			bool structural, PropertyMetadata meta = {})
		{
			return Properties::MakeWith<V>(name, displayName,
				[member](const Entity& e) { return e.GetComponent<T>().*member; },
				[member, structural](Entity& e, const V& v) {
					auto& joint = e.GetComponent<T>();
					joint.*member = v;
					if (structural) joint.MarkStructuralChange();
					else joint.ApplyLive();
				},
				std::move(meta));
		}

		// Angles are stored in radians and edited in degrees.
		template <typename T>
		PropertyDescriptor JointAngle(const std::string& name, const std::string& displayName, float T::* member,
			PropertyMetadata meta = {})
		{
			return Properties::MakeWith<float>(name, displayName,
				[member](const Entity& e) { return e.GetComponent<T>().*member * k_RadToDeg; },
				[member](Entity& e, float degrees) {
					auto& joint = e.GetComponent<T>();
					joint.*member = degrees * k_DegToRad;
					joint.ApplyLive();
				},
				std::move(meta));
		}

		template <typename T>
		std::vector<PropertyDescriptor> JointBaseProperties() {
			return {
				Properties::MakeEntityRef("ConnectedBody", "Connected Body",
					[](const Entity& e) -> uint64_t {
						const EntityHandle h = e.GetComponent<T>().ConnectedBody;
						const Scene* s = e.GetScene();
						return (h == entt::null || !s) ? 0 : s->GetEntityPersistentID(h);
					},
					[](Entity& e, uint64_t persistentId) {
						auto& joint = e.GetComponent<T>();
						EntityHandle resolved = entt::null;
						if (persistentId != 0) {
							if (Scene* s = e.GetScene()) s->TryResolveEntityRef(persistentId, resolved);
						}
						joint.ConnectedBody = resolved;
						joint.MarkStructuralChange();
					},
					Properties::Meta::Tooltip("Body B. Empty attaches the joint to the world.")),
				JointField<T>("Anchor", "Anchor", &Joint2DBase::Anchor, true,
					Properties::Meta::DragSpeed(0.01f).WithTooltip("Joint point in this body's frame (meters, not scaled).")),
				JointField<T>("AutoConfigureConnectedAnchor", "Auto Connected Anchor", &Joint2DBase::AutoConfigureConnectedAnchor, true,
					Properties::Meta::Tooltip("Pins the connected anchor where Anchor is when the joint is built.")),
				JointField<T>("ConnectedAnchor", "Connected Anchor", &Joint2DBase::ConnectedAnchor, true,
					Properties::Meta::EnabledIf<T>([](const T& j) { return !j.AutoConfigureConnectedAnchor; }).WithDragSpeed(0.01f)),
				JointField<T>("CollideConnected", "Collide Connected", &Joint2DBase::CollideConnected, true),
			};
		}

		template <typename T>
		void WriteJointBase(const Entity& e, const T& j, Json::Value& v) {
			v.AddMember("connectedBody", UIEntityHandleToJson(e, j.ConnectedBody));
			v.AddMember("anchorX", Json::Value(j.Anchor.x));
			v.AddMember("anchorY", Json::Value(j.Anchor.y));
			v.AddMember("connectedAnchorX", Json::Value(j.ConnectedAnchor.x));
			v.AddMember("connectedAnchorY", Json::Value(j.ConnectedAnchor.y));
			v.AddMember("autoConfigureConnectedAnchor", Json::Value(j.AutoConfigureConnectedAnchor));
			v.AddMember("collideConnected", Json::Value(j.CollideConnected));
		}

		float ReadJointFloat(const Json::Value& v, const char* key, float fallback) {
			const Json::Value* m = v.FindMember(key);
			return m ? static_cast<float>(m->AsDoubleOr(fallback)) : fallback;
		}
		bool ReadJointBool(const Json::Value& v, const char* key, bool fallback) {
			const Json::Value* m = v.FindMember(key);
			return m ? m->AsBoolOr(fallback) : fallback;
		}

		template <typename T>
		void ReadJointBase(Entity& e, const Json::Value& v, T& j) {
			const T d{};
			j.ConnectedBody = entt::null;
			if (const Json::Value* m = v.FindMember("connectedBody")) UIEntityHandleFromJson(e, *m, j.ConnectedBody);
			j.Anchor = { ReadJointFloat(v, "anchorX", d.Anchor.x), ReadJointFloat(v, "anchorY", d.Anchor.y) };
			j.ConnectedAnchor = { ReadJointFloat(v, "connectedAnchorX", d.ConnectedAnchor.x), ReadJointFloat(v, "connectedAnchorY", d.ConnectedAnchor.y) };
			j.AutoConfigureConnectedAnchor = ReadJointBool(v, "autoConfigureConnectedAnchor", d.AutoConfigureConnectedAnchor);
			j.CollideConnected = ReadJointBool(v, "collideConnected", d.CollideConnected);
			j.MarkStructuralChange();
		}

		void AppendProperties(std::vector<PropertyDescriptor>& to, std::vector<PropertyDescriptor> from) {
			for (auto& p : from) to.push_back(std::move(p));
		}

		void RegisterJoints2D(ComponentRegistry& registry) {
			{
				using T = RevoluteJoint2DComponent;
				ComponentInfo info{ "Revolute Joint 2D", "Physics", ComponentCategory::Component };
				info.serializedName = "RevoluteJoint2D";
				info.properties = JointBaseProperties<T>();
				AppendProperties(info.properties, {
					JointField<T>("EnableLimit", "Use Limits", &T::EnableLimit, false),
					JointAngle<T>("LowerAngle", "Lower Angle", &T::LowerAngle,
						Properties::Meta::EnabledIf<T>([](const T& j) { return j.EnableLimit; }).WithDragSpeed(0.5f)),
					JointAngle<T>("UpperAngle", "Upper Angle", &T::UpperAngle,
						Properties::Meta::EnabledIf<T>([](const T& j) { return j.EnableLimit; }).WithDragSpeed(0.5f)),
					JointField<T>("EnableMotor", "Use Motor", &T::EnableMotor, false),
					JointAngle<T>("MotorSpeed", "Motor Speed (deg/s)", &T::MotorSpeed,
						Properties::Meta::EnabledIf<T>([](const T& j) { return j.EnableMotor; }).WithDragSpeed(1.0f)),
					JointField<T>("MaxMotorTorque", "Max Motor Torque", &T::MaxMotorTorque, false,
						Properties::Meta::EnabledIf<T>([](const T& j) { return j.EnableMotor; }).WithDragSpeed(0.1f).WithDragMin(0.0)),
					JointField<T>("EnableSpring", "Use Spring", &T::EnableSpring, false),
					JointField<T>("SpringHertz", "Spring Frequency", &T::SpringHertz, false,
						Properties::Meta::EnabledIf<T>([](const T& j) { return j.EnableSpring; }).WithDragSpeed(0.05f).WithDragMin(0.0)),
					JointField<T>("SpringDampingRatio", "Spring Damping", &T::SpringDampingRatio, false,
						Properties::Meta::EnabledIf<T>([](const T& j) { return j.EnableSpring; }).WithDragSpeed(0.01f).WithDragMin(0.0)),
				});
				info.serialize = [](Entity e) -> Json::Value {
					const auto& j = e.GetComponent<T>();
					Json::Value v = Json::Value::MakeObject();
					WriteJointBase(e, j, v);
					v.AddMember("enableLimit", Json::Value(j.EnableLimit));
					v.AddMember("lowerAngle", Json::Value(j.LowerAngle));
					v.AddMember("upperAngle", Json::Value(j.UpperAngle));
					v.AddMember("enableMotor", Json::Value(j.EnableMotor));
					v.AddMember("motorSpeed", Json::Value(j.MotorSpeed));
					v.AddMember("maxMotorTorque", Json::Value(j.MaxMotorTorque));
					v.AddMember("enableSpring", Json::Value(j.EnableSpring));
					v.AddMember("springHertz", Json::Value(j.SpringHertz));
					v.AddMember("springDampingRatio", Json::Value(j.SpringDampingRatio));
					return v;
				};
				info.deserialize = [](Entity e, const Json::Value& v) {
					auto& j = e.GetComponent<T>();
					const T d{};
					ReadJointBase(e, v, j);
					j.EnableLimit = ReadJointBool(v, "enableLimit", d.EnableLimit);
					j.LowerAngle = ReadJointFloat(v, "lowerAngle", d.LowerAngle);
					j.UpperAngle = ReadJointFloat(v, "upperAngle", d.UpperAngle);
					j.EnableMotor = ReadJointBool(v, "enableMotor", d.EnableMotor);
					j.MotorSpeed = ReadJointFloat(v, "motorSpeed", d.MotorSpeed);
					j.MaxMotorTorque = ReadJointFloat(v, "maxMotorTorque", d.MaxMotorTorque);
					j.EnableSpring = ReadJointBool(v, "enableSpring", d.EnableSpring);
					j.SpringHertz = ReadJointFloat(v, "springHertz", d.SpringHertz);
					j.SpringDampingRatio = ReadJointFloat(v, "springDampingRatio", d.SpringDampingRatio);
				};
				registry.Register<T>(info);
			}
			{
				using T = PrismaticJoint2DComponent;
				ComponentInfo info{ "Prismatic Joint 2D", "Physics", ComponentCategory::Component };
				info.serializedName = "PrismaticJoint2D";
				info.properties = JointBaseProperties<T>();
				AppendProperties(info.properties, {
					JointField<T>("Axis", "Axis", &T::Axis, true,
						Properties::Meta::DragSpeed(0.01f).WithTooltip("Slide direction in this body's frame.")),
					JointField<T>("EnableLimit", "Use Limits", &T::EnableLimit, false),
					JointField<T>("LowerTranslation", "Lower Translation", &T::LowerTranslation, false,
						Properties::Meta::EnabledIf<T>([](const T& j) { return j.EnableLimit; }).WithDragSpeed(0.01f)),
					JointField<T>("UpperTranslation", "Upper Translation", &T::UpperTranslation, false,
						Properties::Meta::EnabledIf<T>([](const T& j) { return j.EnableLimit; }).WithDragSpeed(0.01f)),
					JointField<T>("EnableMotor", "Use Motor", &T::EnableMotor, false),
					JointField<T>("MotorSpeed", "Motor Speed", &T::MotorSpeed, false,
						Properties::Meta::EnabledIf<T>([](const T& j) { return j.EnableMotor; }).WithDragSpeed(0.05f)),
					JointField<T>("MaxMotorForce", "Max Motor Force", &T::MaxMotorForce, false,
						Properties::Meta::EnabledIf<T>([](const T& j) { return j.EnableMotor; }).WithDragSpeed(0.1f).WithDragMin(0.0)),
					JointField<T>("EnableSpring", "Use Spring", &T::EnableSpring, false),
					JointField<T>("SpringHertz", "Spring Frequency", &T::SpringHertz, false,
						Properties::Meta::EnabledIf<T>([](const T& j) { return j.EnableSpring; }).WithDragSpeed(0.05f).WithDragMin(0.0)),
					JointField<T>("SpringDampingRatio", "Spring Damping", &T::SpringDampingRatio, false,
						Properties::Meta::EnabledIf<T>([](const T& j) { return j.EnableSpring; }).WithDragSpeed(0.01f).WithDragMin(0.0)),
				});
				info.serialize = [](Entity e) -> Json::Value {
					const auto& j = e.GetComponent<T>();
					Json::Value v = Json::Value::MakeObject();
					WriteJointBase(e, j, v);
					v.AddMember("axisX", Json::Value(j.Axis.x));
					v.AddMember("axisY", Json::Value(j.Axis.y));
					v.AddMember("enableLimit", Json::Value(j.EnableLimit));
					v.AddMember("lowerTranslation", Json::Value(j.LowerTranslation));
					v.AddMember("upperTranslation", Json::Value(j.UpperTranslation));
					v.AddMember("enableMotor", Json::Value(j.EnableMotor));
					v.AddMember("motorSpeed", Json::Value(j.MotorSpeed));
					v.AddMember("maxMotorForce", Json::Value(j.MaxMotorForce));
					v.AddMember("enableSpring", Json::Value(j.EnableSpring));
					v.AddMember("springHertz", Json::Value(j.SpringHertz));
					v.AddMember("springDampingRatio", Json::Value(j.SpringDampingRatio));
					return v;
				};
				info.deserialize = [](Entity e, const Json::Value& v) {
					auto& j = e.GetComponent<T>();
					const T d{};
					ReadJointBase(e, v, j);
					j.Axis = { ReadJointFloat(v, "axisX", d.Axis.x), ReadJointFloat(v, "axisY", d.Axis.y) };
					j.EnableLimit = ReadJointBool(v, "enableLimit", d.EnableLimit);
					j.LowerTranslation = ReadJointFloat(v, "lowerTranslation", d.LowerTranslation);
					j.UpperTranslation = ReadJointFloat(v, "upperTranslation", d.UpperTranslation);
					j.EnableMotor = ReadJointBool(v, "enableMotor", d.EnableMotor);
					j.MotorSpeed = ReadJointFloat(v, "motorSpeed", d.MotorSpeed);
					j.MaxMotorForce = ReadJointFloat(v, "maxMotorForce", d.MaxMotorForce);
					j.EnableSpring = ReadJointBool(v, "enableSpring", d.EnableSpring);
					j.SpringHertz = ReadJointFloat(v, "springHertz", d.SpringHertz);
					j.SpringDampingRatio = ReadJointFloat(v, "springDampingRatio", d.SpringDampingRatio);
				};
				registry.Register<T>(info);
			}
			{
				using T = WeldJoint2DComponent;
				ComponentInfo info{ "Weld Joint 2D", "Physics", ComponentCategory::Component };
				info.serializedName = "WeldJoint2D";
				info.properties = JointBaseProperties<T>();
				AppendProperties(info.properties, {
					JointField<T>("LinearHertz", "Linear Frequency", &T::LinearHertz, false,
						Properties::Meta::DragSpeed(0.05f).WithDragMin(0.0).WithTooltip("0 = rigid.")),
					JointField<T>("AngularHertz", "Angular Frequency", &T::AngularHertz, false,
						Properties::Meta::DragSpeed(0.05f).WithDragMin(0.0).WithTooltip("0 = rigid.")),
					JointField<T>("LinearDampingRatio", "Linear Damping", &T::LinearDampingRatio, false,
						Properties::Meta::DragSpeed(0.01f).WithDragMin(0.0)),
					JointField<T>("AngularDampingRatio", "Angular Damping", &T::AngularDampingRatio, false,
						Properties::Meta::DragSpeed(0.01f).WithDragMin(0.0)),
				});
				info.serialize = [](Entity e) -> Json::Value {
					const auto& j = e.GetComponent<T>();
					Json::Value v = Json::Value::MakeObject();
					WriteJointBase(e, j, v);
					v.AddMember("linearHertz", Json::Value(j.LinearHertz));
					v.AddMember("angularHertz", Json::Value(j.AngularHertz));
					v.AddMember("linearDampingRatio", Json::Value(j.LinearDampingRatio));
					v.AddMember("angularDampingRatio", Json::Value(j.AngularDampingRatio));
					return v;
				};
				info.deserialize = [](Entity e, const Json::Value& v) {
					auto& j = e.GetComponent<T>();
					const T d{};
					ReadJointBase(e, v, j);
					j.LinearHertz = ReadJointFloat(v, "linearHertz", d.LinearHertz);
					j.AngularHertz = ReadJointFloat(v, "angularHertz", d.AngularHertz);
					j.LinearDampingRatio = ReadJointFloat(v, "linearDampingRatio", d.LinearDampingRatio);
					j.AngularDampingRatio = ReadJointFloat(v, "angularDampingRatio", d.AngularDampingRatio);
				};
				registry.Register<T>(info);
			}
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
				Properties::MakeWith<float>("LinearDamping", "Linear Damping",
					[](const Entity& e) { return e.GetComponent<Rigidbody2DComponent>().GetLinearDrag(); },
					[](Entity& e, float v) { e.GetComponent<Rigidbody2DComponent>().SetLinearDrag(v); },
					Properties::Meta::DragSpeed(0.01f).WithDragMin(0.0)),
				Properties::MakeWith<float>("AngularDamping", "Angular Damping",
					[](const Entity& e) { return e.GetComponent<Rigidbody2DComponent>().GetAngularDrag(); },
					[](Entity& e, float v) { e.GetComponent<Rigidbody2DComponent>().SetAngularDrag(v); },
					Properties::Meta::DragSpeed(0.01f).WithDragMin(0.0)),
				Properties::MakeWith<bool>("Bullet", "Continuous (Bullet)",
					[](const Entity& e) { return e.GetComponent<Rigidbody2DComponent>().IsBullet(); },
					[](Entity& e, bool v) { e.GetComponent<Rigidbody2DComponent>().SetBullet(v); },
					Properties::Meta::Tooltip("Continuous collision against other moving bodies, for fast objects.")),
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
				Properties::MakeWith<float>("Density", "Density",
					[](const Entity& e) { return e.GetComponent<BoxCollider2DComponent>().GetDensity(); },
					[](Entity& e, float v) {
						auto& col = e.GetComponent<BoxCollider2DComponent>();
						col.SetDensity(v);
						if (e.HasComponent<Rigidbody2DComponent>()) e.GetComponent<Rigidbody2DComponent>().ApplyMassFromState();
					},
					Properties::Meta::DragSpeed(0.01f).WithDragMin(0.0)),
				MakeColliderLayerMask<BoxCollider2DComponent>("LayerMask", "Layer", &Collider2D::GetLayer, &Collider2D::SetLayer),
				MakeColliderLayerMask<BoxCollider2DComponent>("CollisionMask", "Collides With", &Collider2D::GetCollisionMask, &Collider2D::SetCollisionMask),
				Properties::MakeWith<int>("GroupIndex", "Group Index",
					[](const Entity& e) { return e.GetComponent<BoxCollider2DComponent>().GetGroupIndex(); },
					[](Entity& e, int v) { e.GetComponent<BoxCollider2DComponent>().SetGroupIndex(v); },
					Properties::Meta::Tooltip("Same negative group: never touch. Same positive group: always touch. 0: layers decide.")),
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
				Properties::MakeWith<float>("Density", "Density",
					[](const Entity& e) { return e.GetComponent<CircleCollider2DComponent>().GetDensity(); },
					[](Entity& e, float v) {
						auto& col = e.GetComponent<CircleCollider2DComponent>();
						col.SetDensity(v);
						if (e.HasComponent<Rigidbody2DComponent>()) e.GetComponent<Rigidbody2DComponent>().ApplyMassFromState();
					},
					Properties::Meta::DragSpeed(0.01f).WithDragMin(0.0)),
				MakeColliderLayerMask<CircleCollider2DComponent>("LayerMask", "Layer", &Collider2D::GetLayer, &Collider2D::SetLayer),
				MakeColliderLayerMask<CircleCollider2DComponent>("CollisionMask", "Collides With", &Collider2D::GetCollisionMask, &Collider2D::SetCollisionMask),
				Properties::MakeWith<int>("GroupIndex", "Group Index",
					[](const Entity& e) { return e.GetComponent<CircleCollider2DComponent>().GetGroupIndex(); },
					[](Entity& e, int v) { e.GetComponent<CircleCollider2DComponent>().SetGroupIndex(v); },
					Properties::Meta::Tooltip("Same negative group: never touch. Same positive group: always touch. 0: layers decide.")),
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
				Properties::MakeWith<bool>("Edge", "Edge (Open Line)",
					[](const Entity& e) { return e.GetComponent<PolygonCollider2DComponent>().IsEdge(); },
					[](Entity& e, bool v) {
						auto& col = e.GetComponent<PolygonCollider2DComponent>();
						if (Scene* s = e.GetScene()) col.SetEdge(v, *s);
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
				Properties::MakeWith<float>("Density", "Density",
					[](const Entity& e) { return e.GetComponent<PolygonCollider2DComponent>().GetDensity(); },
					[](Entity& e, float v) {
						auto& col = e.GetComponent<PolygonCollider2DComponent>();
						col.SetDensity(v);
						if (e.HasComponent<Rigidbody2DComponent>()) e.GetComponent<Rigidbody2DComponent>().ApplyMassFromState();
					},
					Properties::Meta::DragSpeed(0.01f).WithDragMin(0.0)),
				MakeColliderLayerMask<PolygonCollider2DComponent>("LayerMask", "Layer", &Collider2D::GetLayer, &Collider2D::SetLayer),
				MakeColliderLayerMask<PolygonCollider2DComponent>("CollisionMask", "Collides With", &Collider2D::GetCollisionMask, &Collider2D::SetCollisionMask),
				Properties::MakeWith<int>("GroupIndex", "Group Index",
					[](const Entity& e) { return e.GetComponent<PolygonCollider2DComponent>().GetGroupIndex(); },
					[](Entity& e, int v) { e.GetComponent<PolygonCollider2DComponent>().SetGroupIndex(v); },
					Properties::Meta::Tooltip("Same negative group: never touch. Same positive group: always touch. 0: layers decide.")),
			});

		RegisterJoints2D(registry);
	}
}
