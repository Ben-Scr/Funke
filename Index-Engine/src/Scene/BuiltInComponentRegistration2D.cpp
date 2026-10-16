#include "pch.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"

#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Graphics/Texture2D.hpp"
#include "Graphics/TextureManager.hpp"
#include "Serialization/SceneSerializerShared.hpp"

#include <string>
#include <type_traits>
#include <variant>

// 2D module: scene serialization of Transform2D and the 2D renderers. The components themselves are
// registered in the category slices, whose order fixes typeIdU32.
namespace Index::BuiltInComponentRegistrationInternal {

	namespace {
		using Json::Value;
		using SceneSerializerShared::GetArrayMember;
		using SceneSerializerShared::GetBoolMember;
		using SceneSerializerShared::GetFloatMember;
		using SceneSerializerShared::GetIntMember;
		using SceneSerializerShared::GetObjectMember;
		using SceneSerializerShared::GetStringMember;
		using SceneSerializerShared::GetUInt64Member;
		using SceneSerializerShared::LoadTextureFromValue;

		Value SerializeTransform2D(Entity e) {
			const auto& transform = e.GetComponent<Transform2DComponent>();
			Value transformValue = Value::MakeObject();
			// Legacy posX/posY/rotation/scaleX/scaleY keys kept for backwards compat with older builds and external tools.
			transformValue.AddMember("posX", Value(transform.LocalPosition.x));
			transformValue.AddMember("posY", Value(transform.LocalPosition.y));
			transformValue.AddMember("rotation", Value(transform.LocalRotation));
			transformValue.AddMember("scaleX", Value(transform.LocalScale.x));
			transformValue.AddMember("scaleY", Value(transform.LocalScale.y));
			return transformValue;
		}

		void DeserializeTransform2D(Entity e, const Value& value) {
			auto& transform = e.GetComponent<Transform2DComponent>();
			transform.LocalPosition.x = GetFloatMember(value, "posX", 0.0f);
			transform.LocalPosition.y = GetFloatMember(value, "posY", 0.0f);
			transform.LocalRotation = GetFloatMember(value, "rotation", 0.0f);
			transform.LocalScale.x = GetFloatMember(value, "scaleX", 1.0f);
			transform.LocalScale.y = GetFloatMember(value, "scaleY", 1.0f);
			transform.Position = transform.LocalPosition;
			transform.Scale = transform.LocalScale;
			transform.Rotation = transform.LocalRotation;
			transform.MarkDirty();
		}

		Value SerializeSpriteRenderer(Entity e) {
			auto& spriteRenderer = e.GetComponent<SpriteRendererComponent>();
			Value spriteValue = Value::MakeObject();
			spriteValue.AddMember("r", Value(spriteRenderer.Color.r));
			spriteValue.AddMember("g", Value(spriteRenderer.Color.g));
			spriteValue.AddMember("b", Value(spriteRenderer.Color.b));
			spriteValue.AddMember("a", Value(spriteRenderer.Color.a));
			spriteValue.AddMember("sortOrder", Value(static_cast<int>(spriteRenderer.SortingOrder)));
			spriteValue.AddMember("sortLayer", Value(static_cast<int>(spriteRenderer.SortingLayer)));
			spriteValue.AddMember("filterMode", Value(static_cast<int>(spriteRenderer.FilterMode)));
			// Persisted slice ref. Missing key on load = full texture
			// (the default-constructed empty string).
			if (!spriteRenderer.SpriteName.empty()) {
				spriteValue.AddMember("spriteName", Value(spriteRenderer.SpriteName));
			}
			// Only when set, so prefabs saved before materials existed don't diff against every instance.
			if (spriteRenderer.Material != 0) {
				spriteValue.AddMember("materialAsset", Value(std::to_string(spriteRenderer.Material)));
			}
			if (spriteRenderer.PixelsPerUnit > 0.0f) {
				spriteValue.AddMember("pixelsPerUnit", Value(spriteRenderer.PixelsPerUnit));
			}
			if (spriteRenderer.FlipX) {
				spriteValue.AddMember("flipX", Value(true));
			}
			if (spriteRenderer.FlipY) {
				spriteValue.AddMember("flipY", Value(true));
			}

			uint64_t textureAssetId = static_cast<uint64_t>(spriteRenderer.TextureAssetId);
			if (textureAssetId == 0) {
				textureAssetId = TextureManager::GetTextureAssetUUID(spriteRenderer.TextureHandle);
				if (textureAssetId != 0) {
					spriteRenderer.TextureAssetId = UUID(textureAssetId);
				}
			}

			if (textureAssetId != 0) {
				spriteValue.AddMember("textureAsset", Value(std::to_string(textureAssetId)));
				Texture2D* texture = TextureManager::GetTexture(spriteRenderer.TextureHandle);
				if (texture) {
					spriteValue.AddMember("wrapU", Value(static_cast<int>(texture->GetWrapU())));
					spriteValue.AddMember("wrapV", Value(static_cast<int>(texture->GetWrapV())));
				}
			}

			return spriteValue;
		}

		void DeserializeSpriteRenderer(Entity e, const Value& value) {
			auto& spriteRenderer = e.GetComponent<SpriteRendererComponent>();
			spriteRenderer.Color.r = GetFloatMember(value, "r", 1.0f);
			spriteRenderer.Color.g = GetFloatMember(value, "g", 1.0f);
			spriteRenderer.Color.b = GetFloatMember(value, "b", 1.0f);
			spriteRenderer.Color.a = GetFloatMember(value, "a", 1.0f);
			spriteRenderer.SortingOrder = static_cast<short>(GetIntMember(value, "sortOrder", 0));
			spriteRenderer.SortingLayer = static_cast<uint8_t>(GetIntMember(value, "sortLayer", 0));
			// Prefer the new per-component "filterMode" key; fall back to
			// legacy "filter" (previously sourced from the texture's live
			// sampler state) so older scenes still load with the same
			// filter they were saved with.
			spriteRenderer.FilterMode = static_cast<Filter>(GetIntMember(value, "filterMode",
				GetIntMember(value, "filter", static_cast<int>(spriteRenderer.FilterMode))));
			const Wrap wrapU = static_cast<Wrap>(GetIntMember(value, "wrapU", static_cast<int>(Wrap::Clamp)));
			const Wrap wrapV = static_cast<Wrap>(GetIntMember(value, "wrapV", static_cast<int>(Wrap::Clamp)));
			spriteRenderer.TextureHandle = LoadTextureFromValue(
				value,
				"textureAsset",
				"texture",
				spriteRenderer.FilterMode,
				wrapU,
				wrapV,
				&spriteRenderer.TextureAssetId);
			if (spriteRenderer.FilterMode != Filter::Default) {
				if (auto* tex = TextureManager::GetTexture(spriteRenderer.TextureHandle); tex) {
					tex->SetFilter(spriteRenderer.FilterMode);
				}
			}
			// SpriteName key is optional. Empty/missing = full texture (the
			// default behaviour every pre-Phase-D scene relied on).
			spriteRenderer.SpriteName = GetStringMember(value, "spriteName", std::string{});
			spriteRenderer.Material = GetUInt64Member(value, "materialAsset", 0);
			spriteRenderer.PixelsPerUnit = std::max(0.0f, GetFloatMember(value, "pixelsPerUnit", 0.0f));
			spriteRenderer.FlipX = GetBoolMember(value, "flipX", false);
			spriteRenderer.FlipY = GetBoolMember(value, "flipY", false);
		}

		Value SerializeCamera2D(Entity e) {
			auto& camera = e.GetComponent<Camera2DComponent>();
			const Color clearColor = camera.GetClearColor();
			Value cameraValue = Value::MakeObject();
			cameraValue.AddMember("orthoSize", Value(camera.GetOrthographicSize()));
			cameraValue.AddMember("zoom", Value(camera.GetZoom()));
			cameraValue.AddMember("clearR", Value(clearColor.r));
			cameraValue.AddMember("clearG", Value(clearColor.g));
			cameraValue.AddMember("clearB", Value(clearColor.b));
			cameraValue.AddMember("clearA", Value(clearColor.a));
			cameraValue.AddMember("postProcessing", Value(camera.IsPostProcessingEnabled()));
			return cameraValue;
		}

		void DeserializeCamera2D(Entity e, const Value& value) {
			auto& camera = e.GetComponent<Camera2DComponent>();
			camera.SetOrthographicSize(GetFloatMember(value, "orthoSize", 5.0f));
			camera.SetZoom(GetFloatMember(value, "zoom", 1.0f));
			camera.SetClearColor(Color(
				GetFloatMember(value, "clearR", 0.1f),
				GetFloatMember(value, "clearG", 0.1f),
				GetFloatMember(value, "clearB", 0.1f),
				GetFloatMember(value, "clearA", 1.0f)));
			camera.SetPostProcessingEnabled(GetBoolMember(value, "postProcessing", true));
		}

		Value SerializeParticleSystem2D(Entity e) {
			auto& particleSystem = e.GetComponent<ParticleSystem2DComponent>();
			Value particleValue = Value::MakeObject();
			const int shapeType = std::visit([](auto&& shape) -> int {
				using T = std::decay_t<decltype(shape)>;
				if constexpr (std::is_same_v<T, ParticleSystem2DComponent::CircleParams>) return 0;
				else if constexpr (std::is_same_v<T, ParticleSystem2DComponent::SquareParams>) return 1;
				else return 2;
			}, particleSystem.Shape);

			particleValue.AddMember("playOnAwake", Value(particleSystem.PlayOnAwake));
			particleValue.AddMember("lifetime", Value(particleSystem.ParticleSettings.LifeTime));
			particleValue.AddMember("speed", Value(particleSystem.ParticleSettings.Speed));
			particleValue.AddMember("scaleX", Value(particleSystem.ParticleSettings.Scale.x));
			particleValue.AddMember("scaleY", Value(particleSystem.ParticleSettings.Scale.y));
			particleValue.AddMember("gravityX", Value(particleSystem.ParticleSettings.Gravity.x));
			particleValue.AddMember("gravityY", Value(particleSystem.ParticleSettings.Gravity.y));
			particleValue.AddMember("useGravity", Value(particleSystem.ParticleSettings.UseGravity));
			particleValue.AddMember("useRandomColors", Value(particleSystem.ParticleSettings.UseRandomColors));
			particleValue.AddMember("moveDirectionX", Value(particleSystem.ParticleSettings.MoveDirection.x));
			particleValue.AddMember("moveDirectionY", Value(particleSystem.ParticleSettings.MoveDirection.y));
			{
				const auto& sol = particleSystem.ParticleSettings.ScaleOverLifetime;
				particleValue.AddMember("scaleOverLifetimeEnabled", Value(sol.Enabled));
				Value solKeys = Value::MakeArray();
				for (const auto& key : sol.Curve.Keys) {
					Value keyValue = Value::MakeObject();
					keyValue.AddMember("x", Value(key.Pos.x));
					keyValue.AddMember("y", Value(key.Pos.y));
					keyValue.AddMember("inX", Value(key.InTangent.x));
					keyValue.AddMember("inY", Value(key.InTangent.y));
					keyValue.AddMember("outX", Value(key.OutTangent.x));
					keyValue.AddMember("outY", Value(key.OutTangent.y));
					solKeys.Append(std::move(keyValue));
				}
				particleValue.AddMember("scaleOverLifetimeKeys", std::move(solKeys));
			}
			{
				const auto& col = particleSystem.ParticleSettings.ColorOverLifetime;
				particleValue.AddMember("colorOverLifetimeEnabled", Value(col.Enabled));
				Value colorKeys = Value::MakeArray();
				for (const auto& key : col.Gradient.ColorKeys) {
					Value keyValue = Value::MakeObject();
					keyValue.AddMember("t", Value(key.Position));
					keyValue.AddMember("r", Value(key.R));
					keyValue.AddMember("g", Value(key.G));
					keyValue.AddMember("b", Value(key.B));
					colorKeys.Append(std::move(keyValue));
				}
				particleValue.AddMember("colorOverLifetimeColorKeys", std::move(colorKeys));
				Value alphaKeys = Value::MakeArray();
				for (const auto& key : col.Gradient.AlphaKeys) {
					Value keyValue = Value::MakeObject();
					keyValue.AddMember("t", Value(key.Position));
					keyValue.AddMember("a", Value(key.Alpha));
					alphaKeys.Append(std::move(keyValue));
				}
				particleValue.AddMember("colorOverLifetimeAlphaKeys", std::move(alphaKeys));
			}
			particleValue.AddMember("emitOverTime", Value(static_cast<int>(particleSystem.EmissionSettings.EmitOverTime)));
			particleValue.AddMember(
				"rateOverDistance",
				Value(static_cast<int>(particleSystem.EmissionSettings.RateOverDistance)));
			particleValue.AddMember(
				"emissionSpace",
				Value(static_cast<int>(particleSystem.EmissionSettings.EmissionSpace)));
			particleValue.AddMember("shapeType", Value(shapeType));

			if (shapeType == 0) {
				const auto& circle = std::get<ParticleSystem2DComponent::CircleParams>(particleSystem.Shape);
				particleValue.AddMember("radius", Value(circle.Radius));
				particleValue.AddMember("isOnCircle", Value(circle.IsOnCircle));
				particleValue.AddMember("arc", Value(circle.Arc));
			}
			else {
				if (shapeType == 1) {
					const auto& square = std::get<ParticleSystem2DComponent::SquareParams>(particleSystem.Shape);
					particleValue.AddMember("halfExtendsX", Value(square.HalfExtends.x));
					particleValue.AddMember("halfExtendsY", Value(square.HalfExtends.y));
				}
				else {
					const auto& edge = std::get<ParticleSystem2DComponent::EdgeParams>(particleSystem.Shape);
					particleValue.AddMember("edgeLength", Value(edge.Length));
				}
			}

			particleValue.AddMember("maxParticles", Value(static_cast<int64_t>(particleSystem.RenderingSettings.MaxParticles)));
			particleValue.AddMember("colorR", Value(particleSystem.RenderingSettings.Color.r));
			particleValue.AddMember("colorG", Value(particleSystem.RenderingSettings.Color.g));
			particleValue.AddMember("colorB", Value(particleSystem.RenderingSettings.Color.b));
			particleValue.AddMember("colorA", Value(particleSystem.RenderingSettings.Color.a));
			particleValue.AddMember("sortOrder", Value(static_cast<int>(particleSystem.RenderingSettings.SortingOrder)));
			particleValue.AddMember("sortLayer", Value(static_cast<int>(particleSystem.RenderingSettings.SortingLayer)));

			uint64_t textureAssetId = static_cast<uint64_t>(particleSystem.GetTextureAssetId());
			if (textureAssetId == 0) {
				textureAssetId = TextureManager::GetTextureAssetUUID(particleSystem.GetTextureHandle());
				if (textureAssetId != 0) {
					particleSystem.SetTexture(particleSystem.GetTextureHandle(), UUID(textureAssetId));
				}
			}

			if (textureAssetId != 0) {
				particleValue.AddMember("textureAsset", Value(std::to_string(textureAssetId)));
			}

			Value burstsValue = Value::MakeArray();
			for (const auto& burst : particleSystem.GetBursts()) {
				Value burstValue = Value::MakeObject();
				burstValue.AddMember("count", Value(static_cast<int64_t>(burst.Count)));
				burstValue.AddMember("interval", Value(burst.Interval));
				burstsValue.Append(std::move(burstValue));
			}
			particleValue.AddMember("bursts", std::move(burstsValue));

			return particleValue;
		}

		void DeserializeParticleSystem2D(Entity e, const Value& value) {
			auto& particleSystem = e.GetComponent<ParticleSystem2DComponent>();
			particleSystem.PlayOnAwake = GetBoolMember(value, "playOnAwake", true);
			particleSystem.ParticleSettings.LifeTime = GetFloatMember(value, "lifetime", 1.0f);
			particleSystem.ParticleSettings.Speed = GetFloatMember(value, "speed", 5.0f);
			const float legacyScale = GetFloatMember(value, "scale", 1.0f); // pre-Vec2 scenes stored one scalar
			particleSystem.ParticleSettings.Scale.x = GetFloatMember(value, "scaleX", legacyScale);
			particleSystem.ParticleSettings.Scale.y = GetFloatMember(value, "scaleY", legacyScale);
			particleSystem.ParticleSettings.Gravity.x = GetFloatMember(value, "gravityX", 0.0f);
			particleSystem.ParticleSettings.Gravity.y = GetFloatMember(value, "gravityY", 0.0f);
			particleSystem.ParticleSettings.UseGravity = GetBoolMember(value, "useGravity", false);
			particleSystem.ParticleSettings.UseRandomColors = GetBoolMember(value, "useRandomColors", false);
			particleSystem.ParticleSettings.MoveDirection.x = GetFloatMember(value, "moveDirectionX", 0.0f);
			particleSystem.ParticleSettings.MoveDirection.y = GetFloatMember(value, "moveDirectionY", 0.0f);
			particleSystem.ParticleSettings.ScaleOverLifetime.Enabled =
				GetBoolMember(value, "scaleOverLifetimeEnabled", false);
			if (const Value* solKeys = GetArrayMember(value, "scaleOverLifetimeKeys")) {
				auto& solKeyList = particleSystem.ParticleSettings.ScaleOverLifetime.Curve.Keys;
				solKeyList.clear();
				for (const Value& keyValue : solKeys->GetArray()) {
					Curve::Key key;
					key.Pos = Vec2{ GetFloatMember(keyValue, "x", 0.0f), GetFloatMember(keyValue, "y", 1.0f) };
					key.InTangent = Vec2{ GetFloatMember(keyValue, "inX", -0.2f), GetFloatMember(keyValue, "inY", 0.0f) };
					key.OutTangent = Vec2{ GetFloatMember(keyValue, "outX", 0.2f), GetFloatMember(keyValue, "outY", 0.0f) };
					solKeyList.push_back(key);
				}
				std::sort(solKeyList.begin(), solKeyList.end(),
					[](const auto& a, const auto& b) { return a.Pos.x < b.Pos.x; });
				if (solKeyList.size() < 2) {
					solKeyList = Curve::DefaultKeys();
				}
			}
			particleSystem.ParticleSettings.ColorOverLifetime.Enabled =
				GetBoolMember(value, "colorOverLifetimeEnabled", false);
			if (const Value* colorKeys = GetArrayMember(value, "colorOverLifetimeColorKeys")) {
				auto& colorKeyList = particleSystem.ParticleSettings.ColorOverLifetime.Gradient.ColorKeys;
				colorKeyList.clear();
				for (const Value& keyValue : colorKeys->GetArray()) {
					Gradient::ColorKey key;
					key.Position = GetFloatMember(keyValue, "t", 0.0f);
					key.R = GetFloatMember(keyValue, "r", 1.0f);
					key.G = GetFloatMember(keyValue, "g", 1.0f);
					key.B = GetFloatMember(keyValue, "b", 1.0f);
					colorKeyList.push_back(key);
				}
				std::sort(colorKeyList.begin(), colorKeyList.end(),
					[](const auto& a, const auto& b) { return a.Position < b.Position; });
				if (colorKeyList.empty()) {
					colorKeyList = Gradient::DefaultColorKeys();
				}
			}
			if (const Value* alphaKeys = GetArrayMember(value, "colorOverLifetimeAlphaKeys")) {
				auto& alphaKeyList = particleSystem.ParticleSettings.ColorOverLifetime.Gradient.AlphaKeys;
				alphaKeyList.clear();
				for (const Value& keyValue : alphaKeys->GetArray()) {
					Gradient::AlphaKey key;
					key.Position = GetFloatMember(keyValue, "t", 0.0f);
					key.Alpha = GetFloatMember(keyValue, "a", 1.0f);
					alphaKeyList.push_back(key);
				}
				std::sort(alphaKeyList.begin(), alphaKeyList.end(),
					[](const auto& a, const auto& b) { return a.Position < b.Position; });
				if (alphaKeyList.empty()) {
					alphaKeyList = Gradient::DefaultAlphaKeys();
				}
			}
			particleSystem.EmissionSettings.EmitOverTime =
				static_cast<uint16_t>(GetIntMember(value, "emitOverTime", 10));
			particleSystem.EmissionSettings.RateOverDistance =
				static_cast<uint16_t>(GetIntMember(value, "rateOverDistance", 0));
			particleSystem.EmissionSettings.EmissionSpace = static_cast<ParticleSystem2DComponent::Space>(
				GetIntMember(value, "emissionSpace", static_cast<int>(ParticleSystem2DComponent::Space::World)));

			const int shapeType = GetIntMember(value, "shapeType", 0);
			if (shapeType == 0) {
				ParticleSystem2DComponent::CircleParams circle;
				circle.Radius = GetFloatMember(value, "radius", 1.0f);
				circle.IsOnCircle = GetBoolMember(value, "isOnCircle", false);
				circle.Arc = GetFloatMember(value, "arc", 360.0f);
				particleSystem.Shape = circle;
			}
			else if (shapeType == 1) {
				ParticleSystem2DComponent::SquareParams square;
				square.HalfExtends.x = GetFloatMember(value, "halfExtendsX", 1.0f);
				square.HalfExtends.y = GetFloatMember(value, "halfExtendsY", 1.0f);
				particleSystem.Shape = square;
			}
			else {
				ParticleSystem2DComponent::EdgeParams edge;
				edge.Length = GetFloatMember(value, "edgeLength", 1.0f);
				particleSystem.Shape = edge;
			}

			particleSystem.RenderingSettings.MaxParticles =
				static_cast<uint32_t>(GetUInt64Member(value, "maxParticles", 1000));
			particleSystem.RenderingSettings.Color.r = GetFloatMember(value, "colorR", 1.0f);
			particleSystem.RenderingSettings.Color.g = GetFloatMember(value, "colorG", 1.0f);
			particleSystem.RenderingSettings.Color.b = GetFloatMember(value, "colorB", 1.0f);
			particleSystem.RenderingSettings.Color.a = GetFloatMember(value, "colorA", 1.0f);
			particleSystem.RenderingSettings.SortingOrder =
				static_cast<short>(GetIntMember(value, "sortOrder", 0));
			particleSystem.RenderingSettings.SortingLayer =
				static_cast<uint8_t>(GetIntMember(value, "sortLayer", 0));

			UUID textureAssetId = UUID(0);
			const TextureHandle textureHandle = LoadTextureFromValue(
				value,
				"textureAsset",
				"texture",
				Filter::Point,
				Wrap::Clamp,
				Wrap::Clamp,
				&textureAssetId);
			particleSystem.SetTexture(textureHandle, textureAssetId);

			particleSystem.ClearBursts();
			if (const Value* burstsValue = GetArrayMember(value, "bursts")) {
				for (const Value& burstValue : burstsValue->GetArray()) {
					if (!burstValue.IsObject()) {
						continue;
					}
					ParticleSystem2DComponent::Burst burst;
					burst.Count = static_cast<uint32_t>(GetUInt64Member(burstValue, "count", burst.Count));
					burst.Interval = GetFloatMember(burstValue, "interval", burst.Interval);
					burst.TimeUntilNext = 0.0f;
					particleSystem.AddBurst(burst);
				}
			}
		}

	}

	void Attach2DSerialization(ComponentRegistry& registry) {
		if (ComponentInfo* info = registry.GetInfo<Transform2DComponent>()) {
			info->serialize = [](Entity e) { return SerializeTransform2D(e); };
			info->deserialize = [](Entity e, const Value& value) { DeserializeTransform2D(e, value); };
			info->serializeFirst = true;
		}
		if (ComponentInfo* info = registry.GetInfo<SpriteRendererComponent>()) {
			info->serialize = [](Entity e) { return SerializeSpriteRenderer(e); };
			info->deserialize = [](Entity e, const Value& value) { DeserializeSpriteRenderer(e, value); };
		}
		if (ComponentInfo* info = registry.GetInfo<Camera2DComponent>()) {
			info->serialize = [](Entity e) { return SerializeCamera2D(e); };
			info->deserialize = [](Entity e, const Value& value) { DeserializeCamera2D(e, value); };
		}
		if (ComponentInfo* info = registry.GetInfo<ParticleSystem2DComponent>()) {
			info->serialize = [](Entity e) { return SerializeParticleSystem2D(e); };
			info->deserialize = [](Entity e, const Value& value) { DeserializeParticleSystem2D(e, value); };
		}
	}

}
