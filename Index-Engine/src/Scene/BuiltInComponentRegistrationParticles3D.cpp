#include "pch.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"

#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#include "Serialization/SceneSerializerShared.hpp"

#include <algorithm>
#include <string>

// ParticleSystem3D. Its own slice, registered after Physics3D so no older typeIdU32 shifts.
namespace Index::BuiltInComponentRegistrationInternal {
	namespace {
		using Json::Value;
		using SceneSerializerShared::GetArrayMember;
		using SceneSerializerShared::GetBoolMember;
		using SceneSerializerShared::GetFloatMember;
		using SceneSerializerShared::GetIntMember;
		using SceneSerializerShared::GetUInt64Member;
		using PS = ParticleSystem3DComponent;

		template <typename TEnum>
		TEnum ReadEnum(const Value& v, const char* key, TEnum fallback, TEnum maxValue) {
			const int value = GetIntMember(v, key, static_cast<int>(fallback));
			return (value < 0 || value > static_cast<int>(maxValue)) ? fallback : static_cast<TEnum>(value);
		}

		// Same key layout as ParticleSystem2D, so both systems' curves and gradients read alike in scene files.
		Value CurveToJson(const Curve& curve) {
			Value keys = Value::MakeArray();
			for (const Curve::Key& key : curve.Keys) {
				Value k = Value::MakeObject();
				k.AddMember("x", Value(key.Pos.x));
				k.AddMember("y", Value(key.Pos.y));
				k.AddMember("inX", Value(key.InTangent.x));
				k.AddMember("inY", Value(key.InTangent.y));
				k.AddMember("outX", Value(key.OutTangent.x));
				k.AddMember("outY", Value(key.OutTangent.y));
				keys.Append(std::move(k));
			}
			return keys;
		}

		Curve CurveFromJson(const Value* keys) {
			Curve curve;
			if (!keys) return curve;
			curve.Keys.clear();
			for (const Value& k : keys->GetArray()) {
				Curve::Key key;
				key.Pos = Vec2{ GetFloatMember(k, "x", 0.0f), GetFloatMember(k, "y", 1.0f) };
				key.InTangent = Vec2{ GetFloatMember(k, "inX", -0.2f), GetFloatMember(k, "inY", 0.0f) };
				key.OutTangent = Vec2{ GetFloatMember(k, "outX", 0.2f), GetFloatMember(k, "outY", 0.0f) };
				curve.Keys.push_back(key);
			}
			std::sort(curve.Keys.begin(), curve.Keys.end(), [](const auto& a, const auto& b) { return a.Pos.x < b.Pos.x; });
			if (curve.Keys.size() < 2) curve.Keys = Curve::DefaultKeys();
			return curve;
		}

		void GradientToJson(const Gradient& gradient, Value& target) {
			Value colorKeys = Value::MakeArray();
			for (const Gradient::ColorKey& key : gradient.ColorKeys) {
				Value k = Value::MakeObject();
				k.AddMember("t", Value(key.Position));
				k.AddMember("r", Value(key.R));
				k.AddMember("g", Value(key.G));
				k.AddMember("b", Value(key.B));
				colorKeys.Append(std::move(k));
			}
			target.AddMember("colorOverLifetimeColorKeys", std::move(colorKeys));
			Value alphaKeys = Value::MakeArray();
			for (const Gradient::AlphaKey& key : gradient.AlphaKeys) {
				Value k = Value::MakeObject();
				k.AddMember("t", Value(key.Position));
				k.AddMember("a", Value(key.Alpha));
				alphaKeys.Append(std::move(k));
			}
			target.AddMember("colorOverLifetimeAlphaKeys", std::move(alphaKeys));
		}

		Gradient GradientFromJson(const Value& v) {
			Gradient gradient;
			if (const Value* keys = GetArrayMember(v, "colorOverLifetimeColorKeys")) {
				gradient.ColorKeys.clear();
				for (const Value& k : keys->GetArray()) {
					gradient.ColorKeys.push_back({ GetFloatMember(k, "t", 0.0f),
						GetFloatMember(k, "r", 1.0f), GetFloatMember(k, "g", 1.0f), GetFloatMember(k, "b", 1.0f) });
				}
				std::sort(gradient.ColorKeys.begin(), gradient.ColorKeys.end(), [](const auto& a, const auto& b) { return a.Position < b.Position; });
				if (gradient.ColorKeys.empty()) gradient.ColorKeys = Gradient::DefaultColorKeys();
			}
			if (const Value* keys = GetArrayMember(v, "colorOverLifetimeAlphaKeys")) {
				gradient.AlphaKeys.clear();
				for (const Value& k : keys->GetArray()) {
					gradient.AlphaKeys.push_back({ GetFloatMember(k, "t", 0.0f), GetFloatMember(k, "a", 1.0f) });
				}
				std::sort(gradient.AlphaKeys.begin(), gradient.AlphaKeys.end(), [](const auto& a, const auto& b) { return a.Position < b.Position; });
				if (gradient.AlphaKeys.empty()) gradient.AlphaKeys = Gradient::DefaultAlphaKeys();
			}
			return gradient;
		}

		Value Serialize(Entity e) {
			const PS& c = e.GetComponent<PS>();
			Value v = Value::MakeObject();
			v.AddMember("playOnAwake", Value(c.PlayOnAwake));
			v.AddMember("lifetime", Value(c.LifeTime));
			v.AddMember("lifetimeVariation", Value(c.LifeTimeVariation));
			v.AddMember("speed", Value(c.Speed));
			v.AddMember("speedVariation", Value(c.SpeedVariation));
			v.AddMember("size", Value(c.Size));
			v.AddMember("sizeVariation", Value(c.SizeVariation));
			v.AddMember("randomRotation", Value(c.RandomRotation));
			v.AddMember("angularVelocity", Value(c.AngularVelocity));
			v.AddMember("useGravity", Value(c.UseGravity));
			v.AddMember("gravityX", Value(c.Gravity.x));
			v.AddMember("gravityY", Value(c.Gravity.y));
			v.AddMember("gravityZ", Value(c.Gravity.z));
			v.AddMember("drag", Value(c.Drag));
			v.AddMember("useRandomColors", Value(c.UseRandomColors));
			v.AddMember("emitOverTime", Value(c.EmitOverTime));
			v.AddMember("simulationSpace", Value(static_cast<int>(c.SimulationSpace)));
			v.AddMember("shape", Value(static_cast<int>(c.Shape)));
			v.AddMember("radius", Value(c.Radius));
			v.AddMember("coneAngle", Value(c.ConeAngle));
			v.AddMember("boxSizeX", Value(c.BoxSize.x));
			v.AddMember("boxSizeY", Value(c.BoxSize.y));
			v.AddMember("boxSizeZ", Value(c.BoxSize.z));
			v.AddMember("emitFromSurface", Value(c.EmitFromSurface));
			v.AddMember("scaleOverLifetimeEnabled", Value(c.ScaleOverLifetimeEnabled));
			v.AddMember("scaleOverLifetimeKeys", CurveToJson(c.ScaleOverLifetime));
			v.AddMember("colorOverLifetimeEnabled", Value(c.ColorOverLifetimeEnabled));
			GradientToJson(c.ColorOverLifetime, v);
			if (c.Texture != 0) {
				v.AddMember("texture", Value(std::to_string(c.Texture)));
			}
			v.AddMember("color", UIColorToJson(c.Color));
			v.AddMember("blendMode", Value(static_cast<int>(c.BlendMode)));
			v.AddMember("maxParticles", Value(static_cast<int64_t>(c.MaxParticles)));
			Value bursts = Value::MakeArray();
			for (const PS::Burst& burst : c.GetBursts()) {
				Value b = Value::MakeObject();
				b.AddMember("count", Value(static_cast<int64_t>(burst.Count)));
				b.AddMember("interval", Value(burst.Interval));
				bursts.Append(std::move(b));
			}
			v.AddMember("bursts", std::move(bursts));
			return v;
		}

		void Deserialize(Entity e, const Value& v) {
			PS& c = e.GetComponent<PS>();
			const PS d{};
			c.PlayOnAwake = GetBoolMember(v, "playOnAwake", d.PlayOnAwake);
			c.LifeTime = GetFloatMember(v, "lifetime", d.LifeTime);
			c.LifeTimeVariation = GetFloatMember(v, "lifetimeVariation", d.LifeTimeVariation);
			c.Speed = GetFloatMember(v, "speed", d.Speed);
			c.SpeedVariation = GetFloatMember(v, "speedVariation", d.SpeedVariation);
			c.Size = GetFloatMember(v, "size", d.Size);
			c.SizeVariation = GetFloatMember(v, "sizeVariation", d.SizeVariation);
			c.RandomRotation = GetBoolMember(v, "randomRotation", d.RandomRotation);
			c.AngularVelocity = GetFloatMember(v, "angularVelocity", d.AngularVelocity);
			c.UseGravity = GetBoolMember(v, "useGravity", d.UseGravity);
			c.Gravity = { GetFloatMember(v, "gravityX", d.Gravity.x), GetFloatMember(v, "gravityY", d.Gravity.y), GetFloatMember(v, "gravityZ", d.Gravity.z) };
			c.Drag = GetFloatMember(v, "drag", d.Drag);
			c.UseRandomColors = GetBoolMember(v, "useRandomColors", d.UseRandomColors);
			c.EmitOverTime = GetFloatMember(v, "emitOverTime", d.EmitOverTime);
			c.SimulationSpace = ReadEnum(v, "simulationSpace", d.SimulationSpace, ParticleSpace3D::World);
			c.Shape = ReadEnum(v, "shape", d.Shape, ParticleShape3D::Box);
			c.Radius = GetFloatMember(v, "radius", d.Radius);
			c.ConeAngle = GetFloatMember(v, "coneAngle", d.ConeAngle);
			c.BoxSize = { GetFloatMember(v, "boxSizeX", d.BoxSize.x), GetFloatMember(v, "boxSizeY", d.BoxSize.y), GetFloatMember(v, "boxSizeZ", d.BoxSize.z) };
			c.EmitFromSurface = GetBoolMember(v, "emitFromSurface", d.EmitFromSurface);
			c.ScaleOverLifetimeEnabled = GetBoolMember(v, "scaleOverLifetimeEnabled", d.ScaleOverLifetimeEnabled);
			c.ScaleOverLifetime = CurveFromJson(GetArrayMember(v, "scaleOverLifetimeKeys"));
			c.ColorOverLifetimeEnabled = GetBoolMember(v, "colorOverLifetimeEnabled", d.ColorOverLifetimeEnabled);
			c.ColorOverLifetime = GradientFromJson(v);
			c.Texture = GetUInt64Member(v, "texture", 0);
			const Value* color = v.FindMember("color");
			c.Color = color ? UIColorFromJson(*color, d.Color) : d.Color;
			c.BlendMode = ReadEnum(v, "blendMode", d.BlendMode, ParticleBlendMode3D::Additive);
			c.MaxParticles = static_cast<uint32_t>(std::clamp<uint64_t>(GetUInt64Member(v, "maxParticles", d.MaxParticles), 1, 1000000));
			c.ClearBursts();
			if (const Value* bursts = GetArrayMember(v, "bursts")) {
				for (const Value& b : bursts->GetArray()) {
					if (!b.IsObject()) continue;
					PS::Burst burst;
					burst.Count = static_cast<uint32_t>(GetUInt64Member(b, "count", burst.Count));
					burst.Interval = GetFloatMember(b, "interval", burst.Interval);
					c.AddBurst(burst);
				}
			}
		}

		bool ShapeIs(const Entity& e, std::initializer_list<ParticleShape3D> shapes) {
			const ParticleShape3D shape = e.GetComponent<PS>().Shape;
			return std::find(shapes.begin(), shapes.end(), shape) != shapes.end();
		}
	}

	void RegisterParticles3D(ComponentRegistry& registry) {
		ComponentInfo info{ "Particle System 3D", "3D", ComponentCategory::Component };
		info.serializedName = "ParticleSystem3D";
		info.properties = {
			Properties::Make("PlayOnAwake", "Play On Awake", &PS::PlayOnAwake),
			Properties::Make("LifeTime", "Life Time", &PS::LifeTime, Properties::Meta::Clamp(0.001, 1000.0, 0.05f)),
			Properties::Make("LifeTimeVariation", "Life Time Variation", &PS::LifeTimeVariation,
				Properties::Meta::Clamp(0.0, 1.0, 0.01f).WithTooltip("Each particle's life time is randomized by up to this fraction.")),
			Properties::Make("Speed", "Speed", &PS::Speed, Properties::Meta::DragSpeed(0.05f)),
			Properties::Make("SpeedVariation", "Speed Variation", &PS::SpeedVariation, Properties::Meta::Clamp(0.0, 1.0, 0.01f)),
			Properties::Make("Size", "Size", &PS::Size,
				Properties::Meta::Clamp(0.0, 1000.0, 0.01f).WithTooltip("Billboard edge length in world units.")),
			Properties::Make("SizeVariation", "Size Variation", &PS::SizeVariation, Properties::Meta::Clamp(0.0, 1.0, 0.01f)),
			Properties::Make("RandomRotation", "Random Rotation", &PS::RandomRotation),
			Properties::Make("AngularVelocity", "Angular Velocity", &PS::AngularVelocity,
				Properties::Meta::DragSpeed(1.0f).WithTooltip("Degrees per second around the view axis.")),
			Properties::Make("UseGravity", "Use Gravity", &PS::UseGravity),
			Properties::Make("Gravity", "Gravity", &PS::Gravity,
				Properties::Meta::EnabledIf([](const Entity& e) { return e.GetComponent<PS>().UseGravity; }).WithDragSpeed(0.05f)),
			Properties::Make("Drag", "Drag", &PS::Drag,
				Properties::Meta::Clamp(0.0, 100.0, 0.01f).WithTooltip("Fraction of velocity lost per second.")),
			Properties::Make("UseRandomColors", "Random Colors", &PS::UseRandomColors),

			Properties::Make("EmitOverTime", "Emit Over Time", &PS::EmitOverTime,
				Properties::Meta::Clamp(0.0, 100000.0, 0.5f).WithHeader("Emission").WithTooltip("Particles per second.")),
			Properties::Make("SimulationSpace", "Simulation Space", &PS::SimulationSpace,
				Properties::Meta::Tooltip("World: particles stay where they were emitted. Local: they move with the entity.")),

			Properties::Make("Shape", "Shape", &PS::Shape,
				Properties::Meta::Header("Shape").WithTooltip("Cone and Hemisphere open along the entity's local +Y; Box emits along +Y.")),
			Properties::Make("Radius", "Radius", &PS::Radius,
				Properties::Meta::Clamp(0.0, 10000.0, 0.01f).WithEnabledIf([](const Entity& e) {
					return ShapeIs(e, { ParticleShape3D::Sphere, ParticleShape3D::Hemisphere, ParticleShape3D::Cone });
				})),
			Properties::Make("ConeAngle", "Cone Angle", &PS::ConeAngle,
				Properties::Meta::Clamp(0.0, 89.9, 0.25f).WithEnabledIf([](const Entity& e) { return ShapeIs(e, { ParticleShape3D::Cone }); })
					.WithTooltip("Half angle in degrees.")),
			Properties::Make("BoxSize", "Box Size", &PS::BoxSize,
				Properties::Meta::Clamp(0.0, 10000.0, 0.05f).WithEnabledIf([](const Entity& e) { return ShapeIs(e, { ParticleShape3D::Box }); })),
			Properties::Make("EmitFromSurface", "Emit From Surface", &PS::EmitFromSurface,
				Properties::Meta::EnabledIf([](const Entity& e) { return !ShapeIs(e, { ParticleShape3D::Box }); })
					.WithTooltip("Sphere/Hemisphere: only the shell. Cone: only the base circle's edge.")),

			Properties::MakeTextureRef("Texture", "Texture",
				[](const Entity& e) { return e.GetComponent<PS>().Texture; },
				[](Entity& e, uint64_t guid) { e.GetComponent<PS>().Texture = guid; },
				Properties::Meta::Header("Rendering").WithTooltip("None draws soft round dots.")),
			Properties::Make("Color", "Color", &PS::Color),
			Properties::Make("BlendMode", "Blend Mode", &PS::BlendMode,
				Properties::Meta::Tooltip("Additive suits fire, sparks and magic; Alpha suits smoke and dust.")),
			Properties::MakeWith<int32_t>("MaxParticles", "Max Particles",
				[](const Entity& e) { return static_cast<int32_t>(e.GetComponent<PS>().MaxParticles); },
				[](Entity& e, int32_t v) { e.GetComponent<PS>().MaxParticles = static_cast<uint32_t>(std::clamp(v, 1, 1000000)); },
				Properties::Meta::DragSpeed(1.0f)),
		};
		info.serialize = Serialize;
		info.deserialize = Deserialize;
		registry.Register<PS>(info);
	}
}
