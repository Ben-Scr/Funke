#include "pch.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"

#include "Components/General/NameComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/Graphics/DirectionalLightComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#include "Components/Graphics/PointLightComponent.hpp"
#include "Components/Graphics/SpotLightComponent.hpp"
#include "Assets/AssetKind.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Scene/SceneManager.hpp"

#include <charconv>
#include <string>

// 3D components: Transform3D, Camera3D, MeshRenderer, DirectionalLight, PointLight, SpotLight. Registered last so the
// sequential typeIdU32 of every older component stays unchanged (see BuiltInComponentRegistration.cpp).
namespace Index::BuiltInComponentRegistrationInternal {
	namespace {
		void Vec3ToJson(const Vec3& v, const char* x, const char* y, const char* z, Json::Value& target) {
			target.AddMember(x, Json::Value(v.x));
			target.AddMember(y, Json::Value(v.y));
			target.AddMember(z, Json::Value(v.z));
		}

		float ReadFloat(const Json::Value& v, const char* key, float fallback) {
			const Json::Value* m = v.FindMember(key);
			return m ? static_cast<float>(m->AsDoubleOr(fallback)) : fallback;
		}

		Color ReadColor(const Json::Value& v, const char* key, const Color& fallback) {
			const Json::Value* m = v.FindMember(key);
			return m ? UIColorFromJson(*m, fallback) : fallback;
		}

		template <typename TEnum>
		TEnum ReadEnum(const Json::Value& v, const char* key, TEnum fallback, int maxValue) {
			const Json::Value* m = v.FindMember(key);
			if (!m) return fallback;
			const int value = m->AsIntOr(static_cast<int>(fallback));
			return (value < 0 || value > maxValue) ? fallback : static_cast<TEnum>(value);
		}

		bool ReadBool(const Json::Value& v, const char* key, bool fallback) {
			const Json::Value* m = v.FindMember(key);
			return m ? m->AsBoolOr(fallback) : fallback;
		}

		// Asset GUIDs are written as decimal strings (64-bit safe, found by build asset pruning).
		uint64_t ReadGuid(const Json::Value& v, const char* key) {
			const Json::Value* m = v.FindMember(key);
			if (!m) return 0;
			if (m->IsNumber()) return m->AsUInt64Or(0);
			const std::string text = m->AsStringOr();
			uint64_t guid = 0;
			const auto result = std::from_chars(text.data(), text.data() + text.size(), guid);
			return result.ec == std::errc() ? guid : 0;
		}

		PropertyDescriptor MakeMaterialRef() {
			PropertyDescriptor desc;
			desc.Name = "Material";
			desc.DisplayName = "Material";
			desc.Type = PropertyType::AssetRef;
			desc.Metadata.AssetKindFilter = AssetKind::Material;
			desc.Metadata.Tooltip = "A .material asset. None uses a white default material tinted by Color.";
			desc.Get = [](const Entity& e) -> PropertyValue {
				PropertyValue v;
				v.Type = PropertyType::AssetRef;
				v.UIntValue = e.GetComponent<MeshRendererComponent>().Material;
				return v;
			};
			desc.Set = [](Entity& e, const PropertyValue& v) {
				// Re-read on assignment so a material created moments ago (or edited on disk) is picked up.
				MaterialLibrary::Invalidate(v.UIntValue);
				e.GetComponent<MeshRendererComponent>().Material = v.UIntValue;
			};
			return desc;
		}

		PropertyDescriptor MakeSkyboxMaterialRef() {
			PropertyDescriptor desc;
			desc.Name = "SkyboxMaterial";
			desc.DisplayName = "Skybox Material";
			desc.Type = PropertyType::AssetRef;
			desc.Metadata.AssetKindFilter = AssetKind::Material;
			desc.Metadata.Tooltip = "A .material whose shader targets Skybox (e.g. the built-in Gradient, 6-Sided or Panoramic "
				"sky shaders). None draws the gradient from the colors below.";
			desc.Metadata.EnabledIfFn = [](const Entity& e) {
				return e.GetComponent<Camera3DComponent>().Background == CameraBackground::Skybox;
			};
			desc.Get = [](const Entity& e) -> PropertyValue {
				PropertyValue v;
				v.Type = PropertyType::AssetRef;
				v.UIntValue = e.GetComponent<Camera3DComponent>().SkyboxMaterial;
				return v;
			};
			desc.Set = [](Entity& e, const PropertyValue& v) {
				MaterialLibrary::Invalidate(v.UIntValue);
				e.GetComponent<Camera3DComponent>().SkyboxMaterial = v.UIntValue;
			};
			return desc;
		}

		PropertyDescriptor MakeCustomMeshRef() {
			PropertyDescriptor desc;
			desc.Name = "CustomMesh";
			desc.DisplayName = "Custom Mesh";
			desc.Type = PropertyType::AssetRef;
			desc.Metadata.AssetKindFilter = AssetKind::Mesh;
			desc.Metadata.Tooltip = "A mesh asset (.obj, .gltf, .glb) drawn instead of the primitive Mesh. None draws the primitive.";
			desc.Get = [](const Entity& e) -> PropertyValue {
				PropertyValue v;
				v.Type = PropertyType::AssetRef;
				v.UIntValue = e.GetComponent<MeshRendererComponent>().CustomMesh;
				return v;
			};
			desc.Set = [](Entity& e, const PropertyValue& v) {
				// Re-read on assignment so a file replaced moments ago is picked up.
				MeshLibrary::Invalidate(v.UIntValue);
				e.GetComponent<MeshRendererComponent>().CustomMesh = v.UIntValue;
			};
			return desc;
		}

		bool IsSkybox(const Entity& e) {
			return e.GetComponent<Camera3DComponent>().Background == CameraBackground::Skybox;
		}

		bool IsFogOn(const Entity& e) {
			return e.GetComponent<Camera3DComponent>().FogEnabled;
		}
	}

	void Register3D(ComponentRegistry& registry) {
		// ── Transform 3D ─────────────────────────────────────────────
		ComponentInfo transformInfo{ "Transform 3D", "3D", ComponentCategory::Component };
		transformInfo.serializedName = "Transform3D";
		transformInfo.properties = {
			Properties::Make("Position", "Position", &Transform3DComponent::Position),
			Properties::Make("Rotation", "Rotation", &Transform3DComponent::Rotation, Properties::Meta::DragSpeed(1.0f)),
			Properties::Make("Scale", "Scale", &Transform3DComponent::Scale),
		};
		transformInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<Transform3DComponent>();
			Json::Value v = Json::Value::MakeObject();
			Vec3ToJson(c.Position, "posX", "posY", "posZ", v);
			Vec3ToJson(c.Rotation, "rotX", "rotY", "rotZ", v);
			Vec3ToJson(c.Scale, "scaleX", "scaleY", "scaleZ", v);
			return v;
		};
		transformInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<Transform3DComponent>();
			const Transform3DComponent d{};
			c.Position = { ReadFloat(v, "posX", d.Position.x), ReadFloat(v, "posY", d.Position.y), ReadFloat(v, "posZ", d.Position.z) };
			c.Rotation = { ReadFloat(v, "rotX", d.Rotation.x), ReadFloat(v, "rotY", d.Rotation.y), ReadFloat(v, "rotZ", d.Rotation.z) };
			c.Scale = { ReadFloat(v, "scaleX", d.Scale.x), ReadFloat(v, "scaleY", d.Scale.y), ReadFloat(v, "scaleZ", d.Scale.z) };
			c.WorldMatrix = c.GetLocalMatrix();
		};
		// Before other components, so 2D construct hooks see the entity is 3D instead of adopting a Transform2D.
		transformInfo.serializeFirst = true;
		registry.Register<Transform3DComponent>(transformInfo);

		// ── Camera 3D ────────────────────────────────────────────────
		ComponentInfo cameraInfo{ "Camera 3D", "3D", ComponentCategory::Component };
		cameraInfo.serializedName = "Camera3D";
		cameraInfo.properties = {
			Properties::Make("Projection", "Projection", &Camera3DComponent::Projection),
			Properties::Make("FieldOfView", "Field Of View", &Camera3DComponent::FieldOfView,
				Properties::Meta::Clamp(1.0, 179.0, 0.5f).WithEnabledIf([](const Entity& e) {
					return e.GetComponent<Camera3DComponent>().Projection == CameraProjection::Perspective;
				})),
			Properties::Make("OrthographicSize", "Orthographic Size", &Camera3DComponent::OrthographicSize,
				Properties::Meta::Clamp(0.001, 100000.0, 0.05f).WithEnabledIf([](const Entity& e) {
					return e.GetComponent<Camera3DComponent>().Projection == CameraProjection::Orthographic;
				})),
			Properties::Make("NearClip", "Near Clip", &Camera3DComponent::NearClip, Properties::Meta::Clamp(0.0001, 100000.0, 0.01f)),
			Properties::Make("FarClip", "Far Clip", &Camera3DComponent::FarClip, Properties::Meta::Clamp(0.001, 1000000.0, 1.0f)),
			Properties::Make("Background", "Background", &Camera3DComponent::Background,
				Properties::Meta::Tooltip("Skybox draws the Skybox Material, or a procedural gradient sky without one; "
					"Solid Color clears to Clear Color.")),
			Properties::Make("ClearColor", "Clear Color", &Camera3DComponent::ClearColor,
				Properties::Meta::EnabledIf([](const Entity& e) { return !IsSkybox(e); })),
			MakeSkyboxMaterialRef(),
			Properties::Make("SkyTopColor", "Sky Top", &Camera3DComponent::SkyTopColor,
				Properties::Meta::EnabledIf(IsSkybox)),
			Properties::Make("SkyHorizonColor", "Sky Horizon", &Camera3DComponent::SkyHorizonColor,
				Properties::Meta::EnabledIf(IsSkybox)),
			Properties::Make("SkyGroundColor", "Sky Ground", &Camera3DComponent::SkyGroundColor,
				Properties::Meta::EnabledIf(IsSkybox)),
			Properties::Make("SunSize", "Sun Size", &Camera3DComponent::SunSize,
				Properties::Meta::Clamp(0.0, 45.0, 0.05f).WithEnabledIf(IsSkybox)
					.WithTooltip("Angular radius in degrees of the sun disc drawn towards the main Directional Light; 0 hides it.")),
			Properties::Make("SkyAmbient", "Sky Ambient", &Camera3DComponent::SkyAmbient,
				Properties::Meta::Clamp(0.0, 10.0, 0.01f).WithEnabledIf(IsSkybox)
					.WithTooltip("How strongly the sky colors light meshes from all directions.")),
			Properties::Make("AmbientFromSkybox", "Ambient From Skybox", &Camera3DComponent::AmbientFromSkybox,
				Properties::Meta::Tooltip("Light meshes with the Skybox Material's colors (gradient colors, or the averaged "
					"panorama / cube faces) instead of Sky Top / Horizon / Ground.")
					.WithEnabledIf([](const Entity& e) { return IsSkybox(e) && e.GetComponent<Camera3DComponent>().SkyboxMaterial != 0; })),
			Properties::Make("FogEnabled", "Fog", &Camera3DComponent::FogEnabled,
				Properties::Meta::Tooltip("Meshes fade into Fog Color with distance; materials can opt out (a sky dome).")),
			Properties::Make("FogColor", "Fog Color", &Camera3DComponent::FogColor,
				Properties::Meta::EnabledIf(IsFogOn).WithTooltip("Match the sky's horizon so distant geometry dissolves into it.")),
			Properties::Make("FogStart", "Fog Start", &Camera3DComponent::FogStart,
				Properties::Meta::Clamp(0.0, 100000.0, 0.5f).WithEnabledIf(IsFogOn).WithTooltip("Distance where the fog begins.")),
			Properties::Make("FogEnd", "Fog End", &Camera3DComponent::FogEnd,
				Properties::Meta::Clamp(0.0, 100000.0, 0.5f).WithEnabledIf(IsFogOn).WithTooltip("Distance where meshes are fully fogged.")),
		};
		cameraInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<Camera3DComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("projection", Json::Value(static_cast<int>(c.Projection)));
			v.AddMember("fov", Json::Value(c.FieldOfView));
			v.AddMember("orthoSize", Json::Value(c.OrthographicSize));
			v.AddMember("near", Json::Value(c.NearClip));
			v.AddMember("far", Json::Value(c.FarClip));
			v.AddMember("background", Json::Value(static_cast<int>(c.Background)));
			v.AddMember("clearColor", UIColorToJson(c.ClearColor));
			v.AddMember("skyTop", UIColorToJson(c.SkyTopColor));
			v.AddMember("skyHorizon", UIColorToJson(c.SkyHorizonColor));
			v.AddMember("skyGround", UIColorToJson(c.SkyGroundColor));
			v.AddMember("sunSize", Json::Value(c.SunSize));
			v.AddMember("skyAmbient", Json::Value(c.SkyAmbient));
			if (c.SkyboxMaterial != 0) {
				v.AddMember("skyboxMaterial", Json::Value(std::to_string(c.SkyboxMaterial)));
			}
			if (c.AmbientFromSkybox) {
				v.AddMember("ambientFromSkybox", Json::Value(true));
			}
			v.AddMember("fog", Json::Value(c.FogEnabled));
			v.AddMember("fogColor", UIColorToJson(c.FogColor));
			v.AddMember("fogStart", Json::Value(c.FogStart));
			v.AddMember("fogEnd", Json::Value(c.FogEnd));
			return v;
		};
		cameraInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<Camera3DComponent>();
			const Camera3DComponent d{};
			c.Projection = ReadEnum(v, "projection", d.Projection, static_cast<int>(CameraProjection::Orthographic));
			c.FieldOfView = ReadFloat(v, "fov", d.FieldOfView);
			c.OrthographicSize = ReadFloat(v, "orthoSize", d.OrthographicSize);
			c.NearClip = ReadFloat(v, "near", d.NearClip);
			c.FarClip = ReadFloat(v, "far", d.FarClip);
			c.ClearColor = ReadColor(v, "clearColor", d.ClearColor);
			// Cameras saved before the sky existed keep their solid clear color; an empty object (Reset) gets the defaults.
			const CameraBackground fallbackBackground = v.FindMember("clearColor") ? CameraBackground::SolidColor : d.Background;
			c.Background = ReadEnum(v, "background", fallbackBackground, static_cast<int>(CameraBackground::SolidColor));
			c.SkyTopColor = ReadColor(v, "skyTop", d.SkyTopColor);
			c.SkyHorizonColor = ReadColor(v, "skyHorizon", d.SkyHorizonColor);
			c.SkyGroundColor = ReadColor(v, "skyGround", d.SkyGroundColor);
			c.SunSize = ReadFloat(v, "sunSize", d.SunSize);
			c.SkyAmbient = ReadFloat(v, "skyAmbient", d.SkyAmbient);
			c.SkyboxMaterial = ReadGuid(v, "skyboxMaterial");
			const Json::Value* ambientFromSkybox = v.FindMember("ambientFromSkybox");
			c.AmbientFromSkybox = ambientFromSkybox && ambientFromSkybox->AsBoolOr(false);
			c.FogEnabled = ReadBool(v, "fog", d.FogEnabled);
			c.FogColor = ReadColor(v, "fogColor", d.FogColor);
			c.FogStart = ReadFloat(v, "fogStart", d.FogStart);
			c.FogEnd = ReadFloat(v, "fogEnd", d.FogEnd);
		};
		registry.Register<Camera3DComponent>(cameraInfo);

		// ── Mesh Renderer ────────────────────────────────────────────
		ComponentInfo meshInfo{ "Mesh Renderer", "3D", ComponentCategory::Component };
		meshInfo.serializedName = "MeshRenderer";
		meshInfo.properties = {
			Properties::Make("Mesh", "Mesh", &MeshRendererComponent::Mesh,
				Properties::Meta::EnabledIf([](const Entity& e) { return e.GetComponent<MeshRendererComponent>().CustomMesh == 0; })
					.WithTooltip("Built-in primitive; only used without a Custom Mesh.")),
			MakeCustomMeshRef(),
			MakeMaterialRef(),
			Properties::Make("Color", "Color", &MeshRendererComponent::Color,
				Properties::Meta::Tooltip("Tint multiplied into the material's base color.")),
			Properties::Make("Lit", "Lit", &MeshRendererComponent::Lit,
				Properties::Meta::EnabledIf([](const Entity& e) { return e.GetComponent<MeshRendererComponent>().Material == 0; })
					.WithTooltip("Only used without a material; a material's own Lit flag wins.")),
			Properties::Make("CastShadows", "Cast Shadows", &MeshRendererComponent::CastShadows),
		};
		meshInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<MeshRendererComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("mesh", Json::Value(static_cast<int>(c.Mesh)));
			if (c.CustomMesh != 0) {
				v.AddMember("customMesh", Json::Value(std::to_string(c.CustomMesh)));
			}
			if (c.Material != 0) {
				v.AddMember("material", Json::Value(std::to_string(c.Material)));
			}
			v.AddMember("color", UIColorToJson(c.Color));
			v.AddMember("lit", Json::Value(c.Lit));
			v.AddMember("castShadows", Json::Value(c.CastShadows));
			return v;
		};
		meshInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<MeshRendererComponent>();
			const MeshRendererComponent d{};
			c.Mesh = ReadEnum(v, "mesh", d.Mesh, static_cast<int>(k_PrimitiveMeshTypeCount) - 1);
			c.CustomMesh = ReadGuid(v, "customMesh");
			c.Color = ReadColor(v, "color", d.Color);
			c.Material = ReadGuid(v, "material");
			c.Lit = ReadBool(v, "lit", d.Lit);
			c.CastShadows = ReadBool(v, "castShadows", d.CastShadows);
		};
		registry.Register<MeshRendererComponent>(meshInfo);

		// ── Directional Light ────────────────────────────────────────
		ComponentInfo lightInfo{ "Directional Light", "3D", ComponentCategory::Component };
		lightInfo.serializedName = "DirectionalLight";
		lightInfo.properties = {
			Properties::Make("Color", "Color", &DirectionalLightComponent::Color),
			Properties::Make("Intensity", "Intensity", &DirectionalLightComponent::Intensity, Properties::Meta::Clamp(0.0, 100.0, 0.01f)),
			Properties::Make("Ambient", "Ambient", &DirectionalLightComponent::Ambient,
				Properties::Meta::Tooltip("Flat light added everywhere; only the main (first) directional light's ambient is used.")),
			Properties::Make("CastShadows", "Cast Shadows", &DirectionalLightComponent::CastShadows,
				Properties::Meta::Tooltip("Only the main (first) directional light casts shadows; quality and distance are Graphics settings.")),
		};
		lightInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<DirectionalLightComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("color", UIColorToJson(c.Color));
			v.AddMember("intensity", Json::Value(c.Intensity));
			v.AddMember("ambient", UIColorToJson(c.Ambient));
			v.AddMember("castShadows", Json::Value(c.CastShadows));
			return v;
		};
		lightInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<DirectionalLightComponent>();
			const DirectionalLightComponent d{};
			c.Color = ReadColor(v, "color", d.Color);
			c.Intensity = ReadFloat(v, "intensity", d.Intensity);
			c.Ambient = ReadColor(v, "ambient", d.Ambient);
			c.CastShadows = ReadBool(v, "castShadows", d.CastShadows);
		};
		registry.Register<DirectionalLightComponent>(lightInfo);

		// ── Point Light ──────────────────────────────────────────────
		ComponentInfo pointInfo{ "Point Light", "3D", ComponentCategory::Component };
		pointInfo.serializedName = "PointLight";
		pointInfo.properties = {
			Properties::Make("Color", "Color", &PointLightComponent::Color),
			Properties::Make("Intensity", "Intensity", &PointLightComponent::Intensity, Properties::Meta::Clamp(0.0, 10000.0, 0.05f)),
			Properties::Make("Range", "Range", &PointLightComponent::Range,
				Properties::Meta::Clamp(0.01, 100000.0, 0.05f).WithTooltip("Distance in world units at which the light fades to zero.")),
		};
		pointInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<PointLightComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("color", UIColorToJson(c.Color));
			v.AddMember("intensity", Json::Value(c.Intensity));
			v.AddMember("range", Json::Value(c.Range));
			return v;
		};
		pointInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<PointLightComponent>();
			const PointLightComponent d{};
			c.Color = ReadColor(v, "color", d.Color);
			c.Intensity = ReadFloat(v, "intensity", d.Intensity);
			c.Range = ReadFloat(v, "range", d.Range);
		};
		registry.Register<PointLightComponent>(pointInfo);

		// ── Spot Light ───────────────────────────────────────────────
		ComponentInfo spotInfo{ "Spot Light", "3D", ComponentCategory::Component };
		spotInfo.serializedName = "SpotLight";
		spotInfo.properties = {
			Properties::Make("Color", "Color", &SpotLightComponent::Color),
			Properties::Make("Intensity", "Intensity", &SpotLightComponent::Intensity, Properties::Meta::Clamp(0.0, 10000.0, 0.05f)),
			Properties::Make("Range", "Range", &SpotLightComponent::Range,
				Properties::Meta::Clamp(0.01, 100000.0, 0.05f).WithTooltip("Distance in world units at which the light fades to zero.")),
			Properties::Make("InnerAngle", "Inner Angle", &SpotLightComponent::InnerAngle,
				Properties::Meta::Clamp(0.0, 89.9, 0.1f).WithTooltip("Half angle in degrees with full intensity.")),
			Properties::Make("OuterAngle", "Outer Angle", &SpotLightComponent::OuterAngle,
				Properties::Meta::Clamp(0.1, 89.9, 0.1f).WithTooltip("Half angle in degrees where the cone fades to zero.")),
		};
		spotInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<SpotLightComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("color", UIColorToJson(c.Color));
			v.AddMember("intensity", Json::Value(c.Intensity));
			v.AddMember("range", Json::Value(c.Range));
			v.AddMember("innerAngle", Json::Value(c.InnerAngle));
			v.AddMember("outerAngle", Json::Value(c.OuterAngle));
			return v;
		};
		spotInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<SpotLightComponent>();
			const SpotLightComponent d{};
			c.Color = ReadColor(v, "color", d.Color);
			c.Intensity = ReadFloat(v, "intensity", d.Intensity);
			c.Range = ReadFloat(v, "range", d.Range);
			c.InnerAngle = ReadFloat(v, "innerAngle", d.InnerAngle);
			c.OuterAngle = ReadFloat(v, "outerAngle", d.OuterAngle);
		};
		registry.Register<SpotLightComponent>(spotInfo);
	}
}

namespace Index {
	namespace {
		Entity Create3DEntity(Scene& scene, const std::string& name) {
			Entity entity = scene.CreateEmptyEntity();
			entity.AddComponent<NameComponent>(name);
			entity.AddComponent<Transform3DComponent>();
			return entity;
		}

		void RegisterMeshPreset(const char* label, PrimitiveMeshType mesh) {
			SceneManager::RegisterEntityPreset({ "3D Entity", label, label, [label, mesh](Scene& scene) {
				Entity entity = Create3DEntity(scene, label);
				entity.AddComponent<MeshRendererComponent>().Mesh = mesh;
				return entity;
			} });
		}
	}

	void RegisterBuiltIn3DEntityPresets() {
		SceneManager::RegisterEntityPreset({ "3D Entity", "Empty 3D", "Entity", [](Scene& scene) {
			return Create3DEntity(scene, "Entity");
		} });
		RegisterMeshPreset("Cube", PrimitiveMeshType::Cube);
		RegisterMeshPreset("Sphere", PrimitiveMeshType::Sphere);
		RegisterMeshPreset("Plane", PrimitiveMeshType::Plane);
		RegisterMeshPreset("Cylinder", PrimitiveMeshType::Cylinder);
		SceneManager::RegisterEntityPreset({ "3D Entity", "Camera 3D", "Camera", [](Scene& scene) {
			Entity entity = Create3DEntity(scene, "Camera");
			entity.GetComponent<Transform3DComponent>().Position = { 0.0f, 1.0f, 5.0f };
			entity.AddComponent<Camera3DComponent>();
			return entity;
		} });
		SceneManager::RegisterEntityPreset({ "3D Entity", "Directional Light", "Directional Light", [](Scene& scene) {
			Entity entity = Create3DEntity(scene, "Directional Light");
			entity.GetComponent<Transform3DComponent>().Rotation = { -50.0f, -30.0f, 0.0f };
			entity.AddComponent<DirectionalLightComponent>();
			return entity;
		} });
		SceneManager::RegisterEntityPreset({ "3D Entity", "Point Light", "Point Light", [](Scene& scene) {
			Entity entity = Create3DEntity(scene, "Point Light");
			entity.GetComponent<Transform3DComponent>().Position = { 0.0f, 2.0f, 0.0f };
			entity.AddComponent<PointLightComponent>();
			return entity;
		} });
		SceneManager::RegisterEntityPreset({ "3D Entity", "Spot Light", "Spot Light", [](Scene& scene) {
			Entity entity = Create3DEntity(scene, "Spot Light");
			auto& transform = entity.GetComponent<Transform3DComponent>();
			transform.Position = { 0.0f, 4.0f, 0.0f };
			transform.Rotation = { -90.0f, 0.0f, 0.0f };
			entity.AddComponent<SpotLightComponent>();
			return entity;
		} });
		SceneManager::RegisterEntityPreset({ "3D Entity", "Particle System 3D", "Particle System", [](Scene& scene) {
			Entity entity = Create3DEntity(scene, "Particle System");
			entity.AddComponent<ParticleSystem3DComponent>();
			return entity;
		} });
	}
}
