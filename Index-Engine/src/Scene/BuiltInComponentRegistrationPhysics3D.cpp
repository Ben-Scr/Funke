#include "pch.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"

#include "Components/Physics3D/Collider3DComponents.hpp"
#include "Components/Physics3D/Rigidbody3DComponent.hpp"
#include "Assets/AssetKind.hpp"

// Jolt 3D physics: Rigidbody 3D and the four 3D colliders. Registered after Register3D so no
// older typeIdU32 shifts (see BuiltInComponentRegistration.cpp).
namespace Index::BuiltInComponentRegistrationInternal {
	namespace {
		constexpr const char* k_Category = "Physics 3D";

		float ReadFloat(const Json::Value& v, const char* key, float fallback) {
			const Json::Value* m = v.FindMember(key);
			return m ? static_cast<float>(m->AsDoubleOr(fallback)) : fallback;
		}

		bool ReadBool(const Json::Value& v, const char* key, bool fallback) {
			const Json::Value* m = v.FindMember(key);
			return m ? m->AsBoolOr(fallback) : fallback;
		}

		template <typename TEnum>
		TEnum ReadEnum(const Json::Value& v, const char* key, TEnum fallback, int maxValue) {
			const Json::Value* m = v.FindMember(key);
			if (!m) return fallback;
			const int value = m->AsIntOr(static_cast<int>(fallback));
			return (value < 0 || value > maxValue) ? fallback : static_cast<TEnum>(value);
		}

		Json::Value Vec3ToJson(const Vec3& value) {
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("x", Json::Value(value.x));
			v.AddMember("y", Json::Value(value.y));
			v.AddMember("z", Json::Value(value.z));
			return v;
		}

		Vec3 ReadVec3(const Json::Value& v, const char* key, const Vec3& fallback) {
			const Json::Value* m = v.FindMember(key);
			if (!m || !m->IsObject()) return fallback;
			return { ReadFloat(*m, "x", fallback.x), ReadFloat(*m, "y", fallback.y), ReadFloat(*m, "z", fallback.z) };
		}

		PropertyDescriptor MakeColliderCustomMeshRef() {
			PropertyDescriptor desc;
			desc.Name = "CustomMesh";
			desc.DisplayName = "Custom Mesh";
			desc.Type = PropertyType::AssetRef;
			desc.Metadata.AssetKindFilter = AssetKind::Mesh;
			desc.Metadata.Tooltip = "A mesh asset (.obj) used instead of the primitive Mesh.";
			desc.Metadata.EnabledIfFn = [](const Entity& e) { return !e.GetComponent<MeshCollider3DComponent>().UseMeshRenderer; };
			desc.Get = [](const Entity& e) -> PropertyValue {
				PropertyValue v;
				v.Type = PropertyType::AssetRef;
				v.UIntValue = e.GetComponent<MeshCollider3DComponent>().CustomMesh;
				return v;
			};
			desc.Set = [](Entity& e, const PropertyValue& v) { e.GetComponent<MeshCollider3DComponent>().CustomMesh = v.UIntValue; };
			return desc;
		}

		const PropertyMetadata k_Positive = Properties::Meta::Clamp(0.0, 1000000.0, 0.01f);

		template <typename T>
		void AddColliderBaseProperties(std::vector<PropertyDescriptor>& properties) {
			properties.push_back(Properties::Make("Center", "Center", static_cast<Vec3 T::*>(&T::Center), Properties::Meta::DragSpeed(0.01f)));
			properties.push_back(Properties::Make("IsTrigger", "Is Trigger", static_cast<bool T::*>(&T::IsTrigger),
				Properties::Meta::Tooltip("Reports OnTriggerEnter3D/Stay/Exit overlaps instead of colliding. A body is a trigger only when all of its colliders are.")));
			properties.push_back(Properties::Make("Friction", "Friction", static_cast<float T::*>(&T::Friction), Properties::Meta::Clamp(0.0, 10.0, 0.01f)));
			properties.push_back(Properties::Make("Bounciness", "Bounciness", static_cast<float T::*>(&T::Bounciness), Properties::Meta::Clamp(0.0, 1.0, 0.01f)));
		}

		void WriteColliderBase(const Collider3DBase& c, Json::Value& v) {
			v.AddMember("center", Vec3ToJson(c.Center));
			v.AddMember("isTrigger", Json::Value(c.IsTrigger));
			v.AddMember("friction", Json::Value(c.Friction));
			v.AddMember("bounciness", Json::Value(c.Bounciness));
		}

		void ReadColliderBase(const Json::Value& v, Collider3DBase& c) {
			const Collider3DBase d{};
			c.Center = ReadVec3(v, "center", d.Center);
			c.IsTrigger = ReadBool(v, "isTrigger", d.IsTrigger);
			c.Friction = ReadFloat(v, "friction", d.Friction);
			c.Bounciness = ReadFloat(v, "bounciness", d.Bounciness);
		}
	}

	void RegisterPhysics3D(ComponentRegistry& registry) {
		// ── Rigidbody 3D ─────────────────────────────────────────────
		ComponentInfo rigidbodyInfo{ "Rigidbody 3D", k_Category, ComponentCategory::Component };
		rigidbodyInfo.serializedName = "Rigidbody3D";
		const auto isDynamic = [](const Entity& e) { return e.GetComponent<Rigidbody3DComponent>().Type == BodyType::Dynamic; };
		rigidbodyInfo.properties = {
			Properties::Make("Type", "Body Type", &Rigidbody3DComponent::Type),
			Properties::Make("Mass", "Mass", &Rigidbody3DComponent::Mass, Properties::Meta::Clamp(0.0001, 1000000.0, 0.01f).WithEnabledIf(isDynamic)),
			Properties::Make("LinearDrag", "Linear Drag", &Rigidbody3DComponent::LinearDrag, k_Positive),
			Properties::Make("AngularDrag", "Angular Drag", &Rigidbody3DComponent::AngularDrag, k_Positive),
			Properties::Make("GravityScale", "Gravity Scale", &Rigidbody3DComponent::GravityScale, Properties::Meta::DragSpeed(0.01f).WithEnabledIf(isDynamic)),
			Properties::Make("ContinuousCollision", "Continuous Collision", &Rigidbody3DComponent::ContinuousCollision,
				Properties::Meta::Tooltip("Sweeps the body between steps so fast objects don't pass through thin colliders.")),
			Properties::Make("Interpolate", "Interpolate", &Rigidbody3DComponent::Interpolate,
				Properties::Meta::Tooltip("Draws the body between its last two physics steps so it moves smoothly at high frame rates.").WithEnabledIf(isDynamic)),
			Properties::Make("FreezePositionX", "Freeze Position X", &Rigidbody3DComponent::FreezePositionX),
			Properties::Make("FreezePositionY", "Freeze Position Y", &Rigidbody3DComponent::FreezePositionY),
			Properties::Make("FreezePositionZ", "Freeze Position Z", &Rigidbody3DComponent::FreezePositionZ),
			Properties::Make("FreezeRotationX", "Freeze Rotation X", &Rigidbody3DComponent::FreezeRotationX),
			Properties::Make("FreezeRotationY", "Freeze Rotation Y", &Rigidbody3DComponent::FreezeRotationY),
			Properties::Make("FreezeRotationZ", "Freeze Rotation Z", &Rigidbody3DComponent::FreezeRotationZ),
		};
		rigidbodyInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<Rigidbody3DComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("type", Json::Value(static_cast<int>(c.Type)));
			v.AddMember("mass", Json::Value(c.Mass));
			v.AddMember("linearDrag", Json::Value(c.LinearDrag));
			v.AddMember("angularDrag", Json::Value(c.AngularDrag));
			v.AddMember("gravityScale", Json::Value(c.GravityScale));
			v.AddMember("continuous", Json::Value(c.ContinuousCollision));
			v.AddMember("interpolate", Json::Value(c.Interpolate));
			v.AddMember("freezePosX", Json::Value(c.FreezePositionX));
			v.AddMember("freezePosY", Json::Value(c.FreezePositionY));
			v.AddMember("freezePosZ", Json::Value(c.FreezePositionZ));
			v.AddMember("freezeRotX", Json::Value(c.FreezeRotationX));
			v.AddMember("freezeRotY", Json::Value(c.FreezeRotationY));
			v.AddMember("freezeRotZ", Json::Value(c.FreezeRotationZ));
			return v;
		};
		rigidbodyInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<Rigidbody3DComponent>();
			const Rigidbody3DComponent d{};
			c.Type = ReadEnum(v, "type", d.Type, static_cast<int>(BodyType::Dynamic));
			c.Mass = ReadFloat(v, "mass", d.Mass);
			c.LinearDrag = ReadFloat(v, "linearDrag", d.LinearDrag);
			c.AngularDrag = ReadFloat(v, "angularDrag", d.AngularDrag);
			c.GravityScale = ReadFloat(v, "gravityScale", d.GravityScale);
			c.ContinuousCollision = ReadBool(v, "continuous", d.ContinuousCollision);
			c.Interpolate = ReadBool(v, "interpolate", d.Interpolate);
			c.FreezePositionX = ReadBool(v, "freezePosX", d.FreezePositionX);
			c.FreezePositionY = ReadBool(v, "freezePosY", d.FreezePositionY);
			c.FreezePositionZ = ReadBool(v, "freezePosZ", d.FreezePositionZ);
			c.FreezeRotationX = ReadBool(v, "freezeRotX", d.FreezeRotationX);
			c.FreezeRotationY = ReadBool(v, "freezeRotY", d.FreezeRotationY);
			c.FreezeRotationZ = ReadBool(v, "freezeRotZ", d.FreezeRotationZ);
		};
		registry.Register<Rigidbody3DComponent>(rigidbodyInfo);

		// ── Box Collider 3D ──────────────────────────────────────────
		ComponentInfo boxInfo{ "Box Collider 3D", k_Category, ComponentCategory::Component };
		boxInfo.serializedName = "BoxCollider3D";
		boxInfo.properties = { Properties::Make("Size", "Size", &BoxCollider3DComponent::Size, Properties::Meta::DragSpeed(0.01f)) };
		AddColliderBaseProperties<BoxCollider3DComponent>(boxInfo.properties);
		boxInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<BoxCollider3DComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("size", Vec3ToJson(c.Size));
			WriteColliderBase(c, v);
			return v;
		};
		boxInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<BoxCollider3DComponent>();
			c.Size = ReadVec3(v, "size", BoxCollider3DComponent{}.Size);
			ReadColliderBase(v, c);
		};
		registry.Register<BoxCollider3DComponent>(boxInfo);

		// ── Sphere Collider 3D ───────────────────────────────────────
		ComponentInfo sphereInfo{ "Sphere Collider 3D", k_Category, ComponentCategory::Component };
		sphereInfo.serializedName = "SphereCollider3D";
		sphereInfo.properties = { Properties::Make("Radius", "Radius", &SphereCollider3DComponent::Radius, k_Positive) };
		AddColliderBaseProperties<SphereCollider3DComponent>(sphereInfo.properties);
		sphereInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<SphereCollider3DComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("radius", Json::Value(c.Radius));
			WriteColliderBase(c, v);
			return v;
		};
		sphereInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<SphereCollider3DComponent>();
			c.Radius = ReadFloat(v, "radius", SphereCollider3DComponent{}.Radius);
			ReadColliderBase(v, c);
		};
		registry.Register<SphereCollider3DComponent>(sphereInfo);

		// ── Capsule Collider 3D ──────────────────────────────────────
		ComponentInfo capsuleInfo{ "Capsule Collider 3D", k_Category, ComponentCategory::Component };
		capsuleInfo.serializedName = "CapsuleCollider3D";
		capsuleInfo.properties = {
			Properties::Make("Radius", "Radius", &CapsuleCollider3DComponent::Radius, k_Positive),
			Properties::Make("Height", "Height", &CapsuleCollider3DComponent::Height, k_Positive),
			Properties::Make("Direction", "Direction", &CapsuleCollider3DComponent::Direction),
		};
		AddColliderBaseProperties<CapsuleCollider3DComponent>(capsuleInfo.properties);
		capsuleInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<CapsuleCollider3DComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("radius", Json::Value(c.Radius));
			v.AddMember("height", Json::Value(c.Height));
			v.AddMember("direction", Json::Value(static_cast<int>(c.Direction)));
			WriteColliderBase(c, v);
			return v;
		};
		capsuleInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<CapsuleCollider3DComponent>();
			const CapsuleCollider3DComponent d{};
			c.Radius = ReadFloat(v, "radius", d.Radius);
			c.Height = ReadFloat(v, "height", d.Height);
			c.Direction = ReadEnum(v, "direction", d.Direction, static_cast<int>(CapsuleAxis::Z));
			ReadColliderBase(v, c);
		};
		registry.Register<CapsuleCollider3DComponent>(capsuleInfo);

		// ── Mesh Collider 3D ─────────────────────────────────────────
		ComponentInfo meshInfo{ "Mesh Collider 3D", k_Category, ComponentCategory::Component };
		meshInfo.serializedName = "MeshCollider3D";
		meshInfo.properties = {
			Properties::Make("UseMeshRenderer", "Use Mesh Renderer", &MeshCollider3DComponent::UseMeshRenderer,
				Properties::Meta::Tooltip("Collide with the mesh of this entity's Mesh Renderer.")),
			Properties::Make("Mesh", "Mesh", &MeshCollider3DComponent::Mesh, Properties::Meta::EnabledIf([](const Entity& e) {
				const auto& c = e.GetComponent<MeshCollider3DComponent>();
				return !c.UseMeshRenderer && c.CustomMesh == 0;
			})),
			MakeColliderCustomMeshRef(),
			Properties::Make("Convex", "Convex", &MeshCollider3DComponent::Convex,
				Properties::Meta::Tooltip("Use the convex hull. Required for dynamic bodies; a concave mesh only works on static or kinematic ones.")),
		};
		AddColliderBaseProperties<MeshCollider3DComponent>(meshInfo.properties);
		meshInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<MeshCollider3DComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("useMeshRenderer", Json::Value(c.UseMeshRenderer));
			v.AddMember("mesh", Json::Value(static_cast<int>(c.Mesh)));
			if (c.CustomMesh != 0) {
				v.AddMember("customMesh", UIUuidToJson(UUID(c.CustomMesh)));
			}
			v.AddMember("convex", Json::Value(c.Convex));
			WriteColliderBase(c, v);
			return v;
		};
		meshInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<MeshCollider3DComponent>();
			const MeshCollider3DComponent d{};
			c.UseMeshRenderer = ReadBool(v, "useMeshRenderer", d.UseMeshRenderer);
			c.Mesh = ReadEnum(v, "mesh", d.Mesh, static_cast<int>(k_PrimitiveMeshTypeCount) - 1);
			const Json::Value* customMesh = v.FindMember("customMesh");
			c.CustomMesh = customMesh ? static_cast<uint64_t>(UIUuidFromJson(*customMesh, UUID(0))) : 0;
			c.Convex = ReadBool(v, "convex", d.Convex);
			ReadColliderBase(v, c);
		};
		registry.Register<MeshCollider3DComponent>(meshInfo);
	}
}
