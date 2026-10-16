#include "pch.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"

#include "Assets/AssetRegistry.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/PostProcessing2DComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/Text/FontManager.hpp"
#include "Graphics/TextureManager.hpp"

#if INDEX_WITH_2D
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/SpriteAnimator2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Physics/PhysicsTypes.hpp"
#endif

#include <cmath>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

// Sprites, images, cameras, particles, post-processing and text.
// Split out of BuiltInComponentRegistration.cpp, which owns the call order.
namespace Index::BuiltInComponentRegistrationInternal {

	// The member ends the struct; only tail padding (which differs between 32- and 64-bit targets) may follow.
	constexpr bool IsTrailingMember(size_t offset, size_t memberSize, size_t structSize, size_t structAlign) {
		return offset + memberSize <= structSize && structSize - (offset + memberSize) < structAlign;
	}

	namespace {
		bool IsSlicedImage(const Entity& e) {
			return e.GetComponent<ImageComponent>().Type != ImageType::Simple;
		}

		PropertyMetadata SlicedBorderMeta() {
			return Properties::Meta::Clamp(0.0, 100000.0, 0.5f).WithEnabledIf(IsSlicedImage)
				.WithTooltip("Texture pixels of the sprite that keep their size when the image stretches. "
					"All four 0 = the border set on the texture or sprite slice (.meta).");
		}

		PropertyDescriptor MakeImageMaterialRef() {
			PropertyDescriptor desc;
			desc.Name = "Material";
			desc.DisplayName = "Material";
			desc.Type = PropertyType::AssetRef;
			desc.Metadata.AssetKindFilter = AssetKind::Material;
			desc.Metadata.Tooltip = "A .material whose shader targets UI (or Sprite2D). None draws with the standard shader. "
				"Images batch only with images of the same material.";
			desc.Get = [](const Entity& e) -> PropertyValue {
				PropertyValue v;
				v.Type = PropertyType::AssetRef;
				v.UIntValue = e.GetComponent<ImageComponent>().Material;
				return v;
			};
			desc.Set = [](Entity& e, const PropertyValue& v) {
				// Re-read on assignment so a material created moments ago (or edited on disk) is picked up.
				MaterialLibrary::Invalidate(v.UIntValue);
				e.GetComponent<ImageComponent>().Material = v.UIntValue;
			};
			return desc;
		}

#if INDEX_WITH_2D
		PropertyDescriptor MakeSpriteMaterialRef() {
			PropertyDescriptor desc;
			desc.Name = "Material";
			desc.DisplayName = "Material";
			desc.Type = PropertyType::AssetRef;
			desc.Metadata.AssetKindFilter = AssetKind::Material;
			desc.Metadata.Tooltip = "A .material whose shader targets Sprite2D. None draws with the standard sprite shader. "
				"Sprites batch only with sprites of the same material.";
			desc.Get = [](const Entity& e) -> PropertyValue {
				PropertyValue v;
				v.Type = PropertyType::AssetRef;
				v.UIntValue = e.GetComponent<SpriteRendererComponent>().Material;
				return v;
			};
			desc.Set = [](Entity& e, const PropertyValue& v) {
				// Re-read on assignment so a material created moments ago (or edited on disk) is picked up.
				MaterialLibrary::Invalidate(v.UIntValue);
				e.GetComponent<SpriteRendererComponent>().Material = v.UIntValue;
			};
			return desc;
		}
#endif

		template <typename T, typename HandleAccessor, typename HandleAssign>
		PropertyDescriptor MakeTextureRefDirect(const std::string& name, const std::string& displayName,
			HandleAccessor getHandle, HandleAssign setHandle)
		{
			return Properties::MakeTextureRef(name, displayName,
				[getHandle](const Entity& e, std::string& outSlice) -> uint64_t {
					const auto& component = e.GetComponent<T>();
					uint64_t assetId = 0;
					if constexpr (requires { component.GetTextureAssetId(); }) {
						assetId = static_cast<uint64_t>(component.GetTextureAssetId());
					}
					else if constexpr (requires { component.TextureAssetId; }) {
						assetId = static_cast<uint64_t>(component.TextureAssetId);
					}
					if (assetId == 0) {
						const auto handle = getHandle(component);
						assetId = handle.IsValid() ? TextureManager::GetTextureAssetUUID(handle) : 0ull;
					}
					if constexpr (requires { component.SpriteName; }) {
						if (assetId != 0) {
							outSlice = component.SpriteName;
						}
					}
					return assetId;
				},
				[setHandle](Entity& e, uint64_t uuid, [[maybe_unused]] const std::string& sliceName) {
					auto& component = e.GetComponent<T>();
					TextureHandle h = uuid != 0
						? TextureManager::LoadTextureByUUID(uuid)
						: TextureHandle{};
					if (uuid != 0 && !h.IsValid()) {
						AssetRegistry::MarkDirty();
						AssetRegistry::Sync();
						h = TextureManager::LoadTextureByUUID(uuid);
					}
					if constexpr (std::is_invocable_v<HandleAssign, T&, TextureHandle, UUID, const std::string&>) {
						setHandle(component, h, UUID(uuid), sliceName);
					}
					else {
						setHandle(component, h, UUID(uuid));
					}
				});
		}
	}

	void RegisterRendering(ComponentRegistry& registry) {
		using Index::Codegen::RegisterComponent;
		using Index::Codegen::DeclareConflict;

		// ── Rendering ───────────────────────────────────────────────

#if INDEX_WITH_2D
		{
		ModuleRegistrationScope module2D(registry, EngineModule::TwoD);
		RegisterComponent<SpriteRendererComponent>(registry, "Sprite Renderer",
			ComponentCategory::Component, "Rendering", "SpriteRenderer",
			{
				Properties::Make("Color", "Color", &SpriteRendererComponent::Color),
				Properties::MakeWith<int16_t>("SortingOrder", "Sorting Order",
					[](const Entity& e) { return e.GetComponent<SpriteRendererComponent>().SortingOrder; },
					[](Entity& e, int16_t v) {
						e.GetComponent<SpriteRendererComponent>().SortingOrder = v;
					}),
				Properties::MakeWith<uint8_t>("SortingLayer", "Sorting Layer",
					[](const Entity& e) { return e.GetComponent<SpriteRendererComponent>().SortingLayer; },
					[](Entity& e, uint8_t v) {
						e.GetComponent<SpriteRendererComponent>().SortingLayer = v;
					}),
				Properties::MakeWith<Filter>("FilterMode", "Filter Mode",
					[](const Entity& e) { return e.GetComponent<SpriteRendererComponent>().FilterMode; },
					[](Entity& e, Filter v) {
						auto& sprite = e.GetComponent<SpriteRendererComponent>();
						sprite.FilterMode = v;
						if (v == Filter::Default) {
							// Revert to the texture's own (.meta/import) filter.
							TextureManager::ApplyMetaSamplerToLoaded(
								TextureManager::GetTextureName(sprite.TextureHandle));
						}
						else if (auto* tex = TextureManager::GetTexture(sprite.TextureHandle); tex) {
							tex->SetFilter(v);
						}
					}),
				MakeTextureRefDirect<SpriteRendererComponent>("Texture", "Texture",
					[](const SpriteRendererComponent& s) { return s.TextureHandle; },
					// Slice-aware setter: set SpriteName atomically with the texture so the renderer clips to the correct rect on the first frame; clear it on plain texture drops to avoid dangling stale UV coords.
					[](SpriteRendererComponent& s, TextureHandle h, UUID assetId, const std::string& sliceName) {
						s.TextureHandle = h;
						s.TextureAssetId = assetId;
						s.SpriteName = sliceName;
						// Default leaves the texture's own (.meta) filter untouched.
						if (s.FilterMode != Filter::Default) {
							if (auto* tex = TextureManager::GetTexture(h); tex) {
								tex->SetFilter(s.FilterMode);
							}
						}
					}),
				MakeSpriteMaterialRef(),
				Properties::Make("PixelsPerUnit", "Pixels Per Unit", &SpriteRendererComponent::PixelsPerUnit,
					Properties::Meta::Clamp(0.0, 100000.0, 1.0f)
					.WithTooltip("0 stretches the sprite over the transform scale. Above 0 the sprite keeps its native size "
						"(slice pixels / this value, times the scale) and sits on the slice pivot from the texture's .meta.")),
				Properties::Make("FlipX", "Flip X", &SpriteRendererComponent::FlipX,
					Properties::Meta::Tooltip("Mirrors the sprite horizontally around its pivot.")),
				Properties::Make("FlipY", "Flip Y", &SpriteRendererComponent::FlipY,
					Properties::Meta::Tooltip("Mirrors the sprite vertically around its pivot.")),
			});

		// NativeSpriteRenderer (C#) is the POD prefix of
		// SpriteRendererComponent; the trailing std::string SpriteName makes
		// the type non-trivially-destructible, so the auto byte-emplacer is
		// skipped. Register a prefix-aware one so ECB AddComponent of the
		// native mirror works from scripts AND prefabs with a sprite stay bakeable.
		static_assert(IsTrailingMember(offsetof(SpriteRendererComponent, SpriteName), sizeof(std::string), sizeof(SpriteRendererComponent), alignof(SpriteRendererComponent)),
			"SpriteName must stay the last member: the prefix emplacer bakes only the POD bytes before it.");
		Index::Codegen::RegisterPrefixByteEmplacer<SpriteRendererComponent>(
			registry, offsetof(SpriteRendererComponent, SpriteName));
		}
#endif

		RegisterComponent<ImageComponent>(registry, "Image",
			ComponentCategory::Component, "UI", "Image",
			{
				Properties::Make("Color", "Color", &ImageComponent::Color),
				Properties::MakeWith<int16_t>("SortingOrder", "Sorting Order",
					[](const Entity& e) { return e.GetComponent<ImageComponent>().SortingOrder; },
					[](Entity& e, int16_t v) {
						e.GetComponent<ImageComponent>().SortingOrder = v;
					}),
				Properties::MakeWith<uint8_t>("SortingLayer", "Sorting Layer",
					[](const Entity& e) { return e.GetComponent<ImageComponent>().SortingLayer; },
					[](Entity& e, uint8_t v) {
						e.GetComponent<ImageComponent>().SortingLayer = v;
					}),
				MakeTextureRefDirect<ImageComponent>("Texture", "Texture",
					[](const ImageComponent& i) { return i.TextureHandle; },
					// Slice-aware setter — see SpriteRenderer above for why
					// SpriteName is written here too.
					[](ImageComponent& i, TextureHandle h, UUID assetId, const std::string& sliceName) {
						i.TextureHandle = h;
						i.TextureAssetId = assetId;
						i.SpriteName = sliceName;
						// Default leaves the texture's own (.meta) filter untouched.
						if (i.FilterMode != Filter::Default) {
							if (auto* tex = TextureManager::GetTexture(h); tex) {
								tex->SetFilter(i.FilterMode);
							}
						}
					}),
				Properties::MakeWith<Filter>("FilterMode", "Filter Mode",
					[](const Entity& e) { return e.GetComponent<ImageComponent>().FilterMode; },
					[](Entity& e, Filter v) {
						auto& image = e.GetComponent<ImageComponent>();
						image.FilterMode = v;
						if (v == Filter::Default) {
							TextureManager::ApplyMetaSamplerToLoaded(
								TextureManager::GetTextureName(image.TextureHandle));
						}
						else if (auto* tex = TextureManager::GetTexture(image.TextureHandle); tex) {
							tex->SetFilter(v);
						}
					}),
				Properties::Make("Type", "Image Type", &ImageComponent::Type,
					Properties::Meta::Tooltip("Sliced keeps the borders below at their size while the middle stretches (9-slice), "
						"for rounded buttons and panels of any size. Tiled keeps the borders too but repeats the edges and the middle.")),
				Properties::Make("BorderLeft", "Border Left", &ImageComponent::BorderLeft, SlicedBorderMeta()),
				Properties::Make("BorderRight", "Border Right", &ImageComponent::BorderRight, SlicedBorderMeta()),
				Properties::Make("BorderTop", "Border Top", &ImageComponent::BorderTop, SlicedBorderMeta()),
				Properties::Make("BorderBottom", "Border Bottom", &ImageComponent::BorderBottom, SlicedBorderMeta()),
				Properties::Make("BorderScale", "Border Scale", &ImageComponent::BorderScale,
					Properties::Meta::Clamp(0.0, 100.0, 0.01f).WithEnabledIf(IsSlicedImage)
						.WithTooltip("UI units per texture pixel of the borders (2 = twice as thick); for Tiled also the tile size.")),
				MakeImageMaterialRef(),
			});

		// Same trailing-std::string layout as SpriteRenderer: register a prefix emplacer so UI
		// prefabs containing an Image are ECB-bakeable instead of forced onto the slow spawn path.
		static_assert(IsTrailingMember(offsetof(ImageComponent, SpriteName), sizeof(std::string), sizeof(ImageComponent), alignof(ImageComponent)),
			"SpriteName must stay the last member: the prefix emplacer bakes only the POD bytes before it.");
		Index::Codegen::RegisterPrefixByteEmplacer<ImageComponent>(
			registry, offsetof(ImageComponent, SpriteName));

#if INDEX_WITH_2D
		{
		ModuleRegistrationScope module2D(registry, EngineModule::TwoD);
		RegisterComponent<Camera2DComponent>(registry, "Camera 2D",
			ComponentCategory::Component, "Rendering", "Camera2D",
			{
				Properties::MakeWith<float>("Zoom", "Zoom",
					[](const Entity& e) { return e.GetComponent<Camera2DComponent>().GetZoom(); },
					[](Entity& e, float v) {
						e.GetComponent<Camera2DComponent>().SetZoom(v);
					}),
				Properties::MakeWith<float>("OrthographicSize", "Orthographic Size",
					[](const Entity& e) { return e.GetComponent<Camera2DComponent>().GetOrthographicSize(); },
					[](Entity& e, float v) {
						e.GetComponent<Camera2DComponent>().SetOrthographicSize(v);
					}),
				Properties::MakeWith<Color>("ClearColor", "Clear Color",
					[](const Entity& e) { return e.GetComponent<Camera2DComponent>().GetClearColor(); },
					[](Entity& e, const Color& c) {
						e.GetComponent<Camera2DComponent>().SetClearColor(c);
					}),
				Properties::MakeWith<bool>("PostProcessing", "Post Processing",
					[](const Entity& e) { return e.GetComponent<Camera2DComponent>().IsPostProcessingEnabled(); },
					[](Entity& e, bool v) {
						e.GetComponent<Camera2DComponent>().SetPostProcessingEnabled(v);
					}),
			});

		using PSC = ParticleSystem2DComponent;
		using ShapeType = PSC::ShapeType;
		using CircleParams = PSC::CircleParams;
		using SquareParams = PSC::SquareParams;
		using EdgeParams = PSC::EdgeParams;

		PropertyMetadata gravityEnabledMeta = Properties::Meta::EnabledIf<PSC>(
			[](const PSC& ps) { return ps.ParticleSettings.UseGravity; })
			.WithDragSpeed(0.05f);

		std::vector<PropertyDescriptor> particleProperties;
		particleProperties.push_back(Properties::Make("PlayOnAwake", "Play On Awake",
			&PSC::PlayOnAwake));
		particleProperties.push_back(Properties::MakeWith<float>("LifeTime", "Life Time",
			[](const Entity& e) { return e.GetComponent<PSC>().ParticleSettings.LifeTime; },
			[](Entity& e, float v) { e.GetComponent<PSC>().ParticleSettings.LifeTime = v; },
			Properties::Meta::DragSpeed(0.05f)));
		particleProperties.push_back(Properties::MakeWith<Vec2>("Scale", "Scale",
			[](const Entity& e) { return e.GetComponent<PSC>().ParticleSettings.Scale; },
			[](Entity& e, const Vec2& v) { e.GetComponent<PSC>().ParticleSettings.Scale = v; },
			Properties::Meta::DragSpeed(0.05f).WithAxisLabels(true)));
		particleProperties.push_back(Properties::MakeWith<float>("Speed", "Speed",
			[](const Entity& e) { return e.GetComponent<PSC>().ParticleSettings.Speed; },
			[](Entity& e, float v) { e.GetComponent<PSC>().ParticleSettings.Speed = v; },
			Properties::Meta::DragSpeed(0.1f)));
		particleProperties.push_back(Properties::MakeWith<Vec2>("MoveDirection", "Move Direction",
			[](const Entity& e) { return e.GetComponent<PSC>().ParticleSettings.MoveDirection; },
			[](Entity& e, const Vec2& v) { e.GetComponent<PSC>().ParticleSettings.MoveDirection = v; },
			Properties::Meta::DragSpeed(0.05f)));
		particleProperties.push_back(Properties::MakeWith<bool>("Gravity", "Use Gravity",
			[](const Entity& e) { return e.GetComponent<PSC>().ParticleSettings.UseGravity; },
			[](Entity& e, bool v) { e.GetComponent<PSC>().ParticleSettings.UseGravity = v; }));
		particleProperties.push_back(Properties::MakeWith<Vec2>("GravityValue", "Gravity Value",
			[](const Entity& e) { return e.GetComponent<PSC>().ParticleSettings.Gravity; },
			[](Entity& e, const Vec2& v) { e.GetComponent<PSC>().ParticleSettings.Gravity = v; },
			gravityEnabledMeta));
		particleProperties.push_back(Properties::MakeWith<bool>("RandomColors", "Random Colors",
			[](const Entity& e) { return e.GetComponent<PSC>().ParticleSettings.UseRandomColors; },
			[](Entity& e, bool v) { e.GetComponent<PSC>().ParticleSettings.UseRandomColors = v; }));
		particleProperties.push_back(Properties::MakeWith<uint16_t>("EmitOverTime", "Emit Over Time",
			[](const Entity& e) { return e.GetComponent<PSC>().EmissionSettings.EmitOverTime; },
			[](Entity& e, uint16_t v) { e.GetComponent<PSC>().EmissionSettings.EmitOverTime = v; },
			Properties::Meta::DragSpeed(1.0f).WithHeader("Emission")));
		// Variant: Shape Type -> Circle / Square / Edge branches.
		particleProperties.push_back(Properties::MakeVariantWith<ShapeType>("ShapeType", "Shape Type",
			[](const Entity& e) -> ShapeType {
				return std::visit([](auto&& s) -> ShapeType {
					using T = std::decay_t<decltype(s)>;
					if constexpr (std::is_same_v<T, CircleParams>) return ShapeType::Circle;
					else if constexpr (std::is_same_v<T, SquareParams>) return ShapeType::Square;
					else return ShapeType::Edge;
				}, e.GetComponent<PSC>().Shape);
			},
			[](Entity& e, ShapeType v) {
				auto& ps = e.GetComponent<PSC>();
				if (v == ShapeType::Circle) ps.Shape = CircleParams{ 1.0f, false, 360.0f };
				else if (v == ShapeType::Square) ps.Shape = SquareParams{ Vec2{ 1.0f, 1.0f } };
				else ps.Shape = EdgeParams{ 1.0f };
			},
			{
				Properties::Branch(ShapeType::Circle, {
					Properties::MakeWith<float>("Radius", "Radius",
						[](const Entity& e) {
							return std::get<CircleParams>(e.GetComponent<PSC>().Shape).Radius;
						},
						[](Entity& e, float v) {
							// Radius of 0 makes every particle spawn at the centre, so they all
							// emit straight up; clamp to a tiny positive minimum.
							std::get<CircleParams>(e.GetComponent<PSC>().Shape).Radius = std::max(v, 0.0001f);
						},
						Properties::Meta::DragSpeed(0.05f).WithDragMin(0.0001)),
					Properties::MakeWith<bool>("OnCircleEdge", "On Circle Edge",
						[](const Entity& e) {
							return std::get<CircleParams>(e.GetComponent<PSC>().Shape).IsOnCircle;
						},
						[](Entity& e, bool v) {
							std::get<CircleParams>(e.GetComponent<PSC>().Shape).IsOnCircle = v;
						}),
					Properties::MakeWith<float>("Arc", "Arc",
						[](const Entity& e) {
							return std::get<CircleParams>(e.GetComponent<PSC>().Shape).Arc;
						},
						[](Entity& e, float v) {
							std::get<CircleParams>(e.GetComponent<PSC>().Shape).Arc = v;
						},
						Properties::Meta::Clamp(0.0, 360.0, 1.0f)),
				}),
				Properties::Branch(ShapeType::Square, {
					Properties::MakeWith<Vec2>("HalfExtents", "Half Extents",
						[](const Entity& e) {
							return std::get<SquareParams>(e.GetComponent<PSC>().Shape).HalfExtends;
						},
						[](Entity& e, const Vec2& v) {
							std::get<SquareParams>(e.GetComponent<PSC>().Shape).HalfExtends = v;
						},
						Properties::Meta::Clamp(0.0, 10000.0, 0.05f)),
				}),
				Properties::Branch(ShapeType::Edge, {
					Properties::MakeWith<float>("Length", "Length",
						[](const Entity& e) {
							return std::get<EdgeParams>(e.GetComponent<PSC>().Shape).Length;
						},
						[](Entity& e, float v) {
							std::get<EdgeParams>(e.GetComponent<PSC>().Shape).Length = v;
						},
						Properties::Meta::DragSpeed(0.05f)),
				}),
			}));
		particleProperties.push_back(Properties::MakeWith<Color>("Color", "Color",
			[](const Entity& e) { return e.GetComponent<PSC>().RenderingSettings.Color; },
			[](Entity& e, const Color& c) { e.GetComponent<PSC>().RenderingSettings.Color = c; },
			Properties::Meta::Header("Rendering")));
		particleProperties.push_back(Properties::MakeWith<int32_t>("MaxParticles", "Max Particles",
			[](const Entity& e) { return static_cast<int32_t>(e.GetComponent<PSC>().RenderingSettings.MaxParticles); },
			[](Entity& e, int32_t v) {
				e.GetComponent<PSC>().RenderingSettings.MaxParticles = static_cast<uint32_t>(std::max(1, v));
			},
			Properties::Meta::DragSpeed(1.0f)));
		particleProperties.push_back(Properties::MakeWith<int16_t>("SortingOrder", "Sorting Order",
			[](const Entity& e) { return e.GetComponent<PSC>().RenderingSettings.SortingOrder; },
			[](Entity& e, int16_t v) { e.GetComponent<PSC>().RenderingSettings.SortingOrder = v; }));
		particleProperties.push_back(Properties::MakeWith<uint8_t>("SortingLayer", "Sorting Layer",
			[](const Entity& e) { return e.GetComponent<PSC>().RenderingSettings.SortingLayer; },
			[](Entity& e, uint8_t v) { e.GetComponent<PSC>().RenderingSettings.SortingLayer = v; }));
		// Texture lives at the end of the Rendering group so the picker sits right
		// above the preview the custom inspector draws after the property list.
		particleProperties.push_back(MakeTextureRefDirect<PSC>("Texture", "Texture",
			[](const PSC& ps) { return ps.GetTextureHandle(); },
			[](PSC& ps, TextureHandle h, UUID assetId) {
				ps.SetTexture(h, assetId);
			}));

		// Particle inspector uses drag-only numeric fields (no +/- steppers), incl. nested shape-variant params.
		std::function<void(std::vector<PropertyDescriptor>&)> hideParticleSteppers =
			[&hideParticleSteppers](std::vector<PropertyDescriptor>& props) {
				for (PropertyDescriptor& prop : props) {
					prop.Metadata.HideStepperButtons = true;
					for (PropertyDescriptor::Branch& branch : prop.VariantBranches)
						hideParticleSteppers(branch.Properties);
				}
			};
		hideParticleSteppers(particleProperties);

		RegisterComponent<ParticleSystem2DComponent>(registry, "Particle System 2D",
			ComponentCategory::Component, "Rendering", "ParticleSystem2D",
			std::move(particleProperties));

		RegisterComponent<SpriteAnimator2DComponent>(registry, "Sprite Animator 2D",
			ComponentCategory::Component, "Rendering", "SpriteAnimator2D",
			{
				Properties::MakeStringList("Frames", "Frames",
					[](const Entity& e) { return e.GetComponent<SpriteAnimator2DComponent>().Frames; },
					[](Entity& e, const std::vector<std::string>& frames) {
						e.GetComponent<SpriteAnimator2DComponent>().Frames = frames;
					},
					Properties::Meta::Tooltip("Sprite-sheet slice names on this entity's Sprite Renderer texture, in playback order.")),
				Properties::MakeWith<float>("FramesPerSecond", "Frames Per Second",
					[](const Entity& e) { return e.GetComponent<SpriteAnimator2DComponent>().FramesPerSecond; },
					[](Entity& e, float fps) { e.GetComponent<SpriteAnimator2DComponent>().FramesPerSecond = std::max(0.0f, fps); },
					Properties::Meta::Clamp(0.0, 120.0, 0.1f)),
				Properties::MakeWith<bool>("Loop", "Loop",
					[](const Entity& e) { return e.GetComponent<SpriteAnimator2DComponent>().Loop; },
					[](Entity& e, bool loop) { e.GetComponent<SpriteAnimator2DComponent>().Loop = loop; }),
				Properties::MakeWith<bool>("PlayOnStart", "Play On Start",
					[](const Entity& e) { return e.GetComponent<SpriteAnimator2DComponent>().PlayOnStart; },
					[](Entity& e, bool play) { e.GetComponent<SpriteAnimator2DComponent>().PlayOnStart = play; }),
			});
		// Registry-driven serialization: the scene serializer calls these for every component with a serializedName.
		registry.ForEachComponentInfo(
			[](const std::type_index& id, ComponentInfo& info) {
				if (id != std::type_index(typeid(SpriteAnimator2DComponent))) return;
				info.serialize = [](Entity e) {
					const auto& animator = e.GetComponent<SpriteAnimator2DComponent>();
					Json::Value value = Json::Value::MakeObject();
					Json::Value frames = Json::Value::MakeArray();
					for (const std::string& frame : animator.Frames) frames.Append(Json::Value(frame));
					value.AddMember("frames", std::move(frames));
					value.AddMember("fps", Json::Value(static_cast<double>(animator.FramesPerSecond)));
					value.AddMember("loop", Json::Value(animator.Loop));
					value.AddMember("playOnStart", Json::Value(animator.PlayOnStart));
					return value;
				};
				info.deserialize = [](Entity e, const Json::Value& value) {
					auto& animator = e.GetComponent<SpriteAnimator2DComponent>();
					animator.Frames.clear();
					if (const Json::Value* frames = value.FindMember("frames"); frames && frames->IsArray()) {
						for (const Json::Value& frame : frames->GetArray()) {
							if (frame.IsString()) animator.Frames.push_back(frame.AsStringOr());
						}
					}
					const Json::Value* fps = value.FindMember("fps");
					const Json::Value* loop = value.FindMember("loop");
					const Json::Value* playOnStart = value.FindMember("playOnStart");
					animator.FramesPerSecond = std::max(0.0f, fps ? static_cast<float>(fps->AsDoubleOr(12.0)) : 12.0f);
					animator.Loop = loop ? loop->AsBoolOr(true) : true;
					animator.PlayOnStart = playOnStart ? playOnStart->AsBoolOr(true) : true;
				};
			});

		// Custom copyTo: default value-copy would carry source entity's cached scene/entity pointers; RebindEmitter re-points the copy at its own entity.
		registry.ForEachComponentInfo(
			[](const std::type_index& id, ComponentInfo& info) {
				if (id == std::type_index(typeid(ParticleSystem2DComponent))) {
					info.copyTo = [](Entity src, Entity dst) {
						if (!src.HasComponent<ParticleSystem2DComponent>()) return;
						const auto& srcComp = src.GetComponent<ParticleSystem2DComponent>();
						if (dst.HasComponent<ParticleSystem2DComponent>()) {
							dst.GetComponent<ParticleSystem2DComponent>() = srcComp;
						} else {
							dst.AddComponent<ParticleSystem2DComponent>(srcComp);
						}
						dst.GetComponent<ParticleSystem2DComponent>()
							.RebindEmitter(dst.GetScene(), dst.GetHandle());
					};
				}
			});
		}
#endif

		// No module scope: Post Processing applies to Camera2D and Camera3D.
		{
			using PP = PostProcessing2DComponent;

			PropertyMetadata cgEnabled = Properties::Meta::EnabledIf<PP>(
				[](const PP& p) { return p.ColorGrading.Enabled; });
			PropertyMetadata vgEnabled = Properties::Meta::EnabledIf<PP>(
				[](const PP& p) { return p.Vignette.Enabled; });
			PropertyMetadata caEnabled = Properties::Meta::EnabledIf<PP>(
				[](const PP& p) { return p.ChromaticAberration.Enabled; });
			PropertyMetadata grEnabled = Properties::Meta::EnabledIf<PP>(
				[](const PP& p) { return p.Grain.Enabled; });
			PropertyMetadata blEnabled = Properties::Meta::EnabledIf<PP>(
				[](const PP& p) { return p.Bloom.Enabled; });
			PropertyMetadata ldEnabled = Properties::Meta::EnabledIf<PP>(
				[](const PP& p) { return p.LensDistortion.Enabled; });

			std::vector<PropertyDescriptor> ppProps;

			// Color Grading
			ppProps.push_back(Properties::MakeWith<bool>("CG_Enabled", "Enabled",
				[](const Entity& e) { return e.GetComponent<PP>().ColorGrading.Enabled; },
				[](Entity& e, bool v) { e.GetComponent<PP>().ColorGrading.Enabled = v; },
				Properties::Meta::Header("Color Grading").WithResetSection(
					[](Entity& e) {
						e.GetComponent<PP>().ColorGrading = PP::ColorGradingSettings{};
					})));
			ppProps.push_back(Properties::MakeWith<float>("CG_Exposure", "Exposure",
				[](const Entity& e) { return e.GetComponent<PP>().ColorGrading.Exposure; },
				[](Entity& e, float v) { e.GetComponent<PP>().ColorGrading.Exposure = v; },
				PropertyMetadata{ cgEnabled }.WithClamp(-10.0, 10.0).WithDragSpeed(0.01f)));
			ppProps.push_back(Properties::MakeWith<float>("CG_Contrast", "Contrast",
				[](const Entity& e) { return e.GetComponent<PP>().ColorGrading.Contrast; },
				[](Entity& e, float v) { e.GetComponent<PP>().ColorGrading.Contrast = v; },
				PropertyMetadata{ cgEnabled }.WithClamp(0.0, 4.0).WithDragSpeed(0.01f)));
			ppProps.push_back(Properties::MakeWith<float>("CG_Saturation", "Saturation",
				[](const Entity& e) { return e.GetComponent<PP>().ColorGrading.Saturation; },
				[](Entity& e, float v) { e.GetComponent<PP>().ColorGrading.Saturation = v; },
				PropertyMetadata{ cgEnabled }.WithClamp(0.0, 4.0).WithDragSpeed(0.01f)));
			ppProps.push_back(Properties::MakeWith<float>("CG_Temperature", "Temperature",
				[](const Entity& e) { return e.GetComponent<PP>().ColorGrading.Temperature; },
				[](Entity& e, float v) { e.GetComponent<PP>().ColorGrading.Temperature = v; },
				PropertyMetadata{ cgEnabled }.WithClamp(-1.0, 1.0).WithDragSpeed(0.01f)));
			ppProps.push_back(Properties::MakeWith<float>("CG_Tint", "Tint",
				[](const Entity& e) { return e.GetComponent<PP>().ColorGrading.Tint; },
				[](Entity& e, float v) { e.GetComponent<PP>().ColorGrading.Tint = v; },
				PropertyMetadata{ cgEnabled }.WithClamp(-1.0, 1.0).WithDragSpeed(0.01f)));
			ppProps.push_back(Properties::MakeWith<Color>("CG_ColorFilter", "Color Filter",
				[](const Entity& e) { return e.GetComponent<PP>().ColorGrading.ColorFilter; },
				[](Entity& e, const Color& c) { e.GetComponent<PP>().ColorGrading.ColorFilter = c; },
				cgEnabled));

			// Vignette
			ppProps.push_back(Properties::MakeWith<bool>("VG_Enabled", "Enabled",
				[](const Entity& e) { return e.GetComponent<PP>().Vignette.Enabled; },
				[](Entity& e, bool v) { e.GetComponent<PP>().Vignette.Enabled = v; },
				Properties::Meta::Header("Vignette").WithResetSection(
					[](Entity& e) {
						e.GetComponent<PP>().Vignette = PP::VignetteSettings{};
					})));
			ppProps.push_back(Properties::MakeWith<Color>("VG_Color", "Color",
				[](const Entity& e) { return e.GetComponent<PP>().Vignette.Color; },
				[](Entity& e, const Color& c) { e.GetComponent<PP>().Vignette.Color = c; },
				vgEnabled));
			ppProps.push_back(Properties::MakeWith<float>("VG_Intensity", "Intensity",
				[](const Entity& e) { return e.GetComponent<PP>().Vignette.Intensity; },
				[](Entity& e, float v) { e.GetComponent<PP>().Vignette.Intensity = v; },
				PropertyMetadata{ vgEnabled }.WithClamp(0.0, 1.0).WithDragSpeed(0.005f)));
			ppProps.push_back(Properties::MakeWith<float>("VG_Smoothness", "Smoothness",
				[](const Entity& e) { return e.GetComponent<PP>().Vignette.Smoothness; },
				[](Entity& e, float v) { e.GetComponent<PP>().Vignette.Smoothness = v; },
				PropertyMetadata{ vgEnabled }.WithClamp(0.0, 1.0).WithDragSpeed(0.005f)));
			ppProps.push_back(Properties::MakeWith<float>("VG_Roundness", "Roundness",
				[](const Entity& e) { return e.GetComponent<PP>().Vignette.Roundness; },
				[](Entity& e, float v) { e.GetComponent<PP>().Vignette.Roundness = v; },
				PropertyMetadata{ vgEnabled }.WithClamp(0.0, 1.0).WithDragSpeed(0.005f)));
			ppProps.push_back(Properties::MakeWith<Vec2>("VG_Center", "Center",
				[](const Entity& e) { return e.GetComponent<PP>().Vignette.Center; },
				[](Entity& e, const Vec2& v) { e.GetComponent<PP>().Vignette.Center = v; },
				vgEnabled));

			// Chromatic Aberration
			ppProps.push_back(Properties::MakeWith<bool>("CA_Enabled", "Enabled",
				[](const Entity& e) { return e.GetComponent<PP>().ChromaticAberration.Enabled; },
				[](Entity& e, bool v) { e.GetComponent<PP>().ChromaticAberration.Enabled = v; },
				Properties::Meta::Header("Chromatic Aberration").WithResetSection(
					[](Entity& e) {
						e.GetComponent<PP>().ChromaticAberration = PP::ChromaticAberrationSettings{};
					})));
			ppProps.push_back(Properties::MakeWith<float>("CA_Intensity", "Intensity",
				[](const Entity& e) { return e.GetComponent<PP>().ChromaticAberration.Intensity; },
				[](Entity& e, float v) { e.GetComponent<PP>().ChromaticAberration.Intensity = v; },
				PropertyMetadata{ caEnabled }.WithClamp(0.0, 1.0).WithDragSpeed(0.005f)));

			// Grain
			ppProps.push_back(Properties::MakeWith<bool>("GR_Enabled", "Enabled",
				[](const Entity& e) { return e.GetComponent<PP>().Grain.Enabled; },
				[](Entity& e, bool v) { e.GetComponent<PP>().Grain.Enabled = v; },
				Properties::Meta::Header("Grain").WithResetSection(
					[](Entity& e) {
						e.GetComponent<PP>().Grain = PP::GrainSettings{};
					})));
			ppProps.push_back(Properties::MakeWith<float>("GR_Intensity", "Intensity",
				[](const Entity& e) { return e.GetComponent<PP>().Grain.Intensity; },
				[](Entity& e, float v) { e.GetComponent<PP>().Grain.Intensity = v; },
				PropertyMetadata{ grEnabled }.WithClamp(0.0, 1.0).WithDragSpeed(0.005f)));
			ppProps.push_back(Properties::MakeWith<float>("GR_Size", "Size",
				[](const Entity& e) { return e.GetComponent<PP>().Grain.Size; },
				[](Entity& e, float v) { e.GetComponent<PP>().Grain.Size = v; },
				PropertyMetadata{ grEnabled }.WithClamp(0.3, 3.0).WithDragSpeed(0.01f)));
			ppProps.push_back(Properties::MakeWith<bool>("GR_Colored", "Colored",
				[](const Entity& e) { return e.GetComponent<PP>().Grain.Colored; },
				[](Entity& e, bool v) { e.GetComponent<PP>().Grain.Colored = v; },
				grEnabled));

			// Bloom
			ppProps.push_back(Properties::MakeWith<bool>("BL_Enabled", "Enabled",
				[](const Entity& e) { return e.GetComponent<PP>().Bloom.Enabled; },
				[](Entity& e, bool v) { e.GetComponent<PP>().Bloom.Enabled = v; },
				Properties::Meta::Header("Bloom").WithResetSection(
					[](Entity& e) {
						e.GetComponent<PP>().Bloom = PP::BloomSettings{};
					})));
			ppProps.push_back(Properties::MakeWith<int32_t>("BL_Taps", "Taps",
				[](const Entity& e) { return static_cast<int32_t>(e.GetComponent<PP>().Bloom.Taps); },
				[](Entity& e, int32_t v) {
					e.GetComponent<PP>().Bloom.Taps = std::max(7, std::min(500, v));
				},
				PropertyMetadata{ blEnabled }.WithClamp(7.0, 500.0).WithDragSpeed(1.0f)));
			ppProps.push_back(Properties::MakeWith<float>("BL_Threshold", "Threshold",
				[](const Entity& e) { return e.GetComponent<PP>().Bloom.Threshold; },
				[](Entity& e, float v) { e.GetComponent<PP>().Bloom.Threshold = v; },
				PropertyMetadata{ blEnabled }.WithClamp(0.0, 10.0).WithDragSpeed(0.01f)));
			ppProps.push_back(Properties::MakeWith<float>("BL_Intensity", "Intensity",
				[](const Entity& e) { return e.GetComponent<PP>().Bloom.Intensity; },
				[](Entity& e, float v) { e.GetComponent<PP>().Bloom.Intensity = v; },
				PropertyMetadata{ blEnabled }.WithClamp(0.0, 10.0).WithDragSpeed(0.01f)));
			ppProps.push_back(Properties::MakeWith<float>("BL_Scatter", "Scatter",
				[](const Entity& e) { return e.GetComponent<PP>().Bloom.Scatter; },
				[](Entity& e, float v) { e.GetComponent<PP>().Bloom.Scatter = v; },
				PropertyMetadata{ blEnabled }.WithClamp(0.0, 1.0).WithDragSpeed(0.005f)));
			ppProps.push_back(Properties::MakeWith<Color>("BL_Tint", "Tint",
				[](const Entity& e) { return e.GetComponent<PP>().Bloom.Tint; },
				[](Entity& e, const Color& c) { e.GetComponent<PP>().Bloom.Tint = c; },
				blEnabled));

			// Lens Distortion
			ppProps.push_back(Properties::MakeWith<bool>("LD_Enabled", "Enabled",
				[](const Entity& e) { return e.GetComponent<PP>().LensDistortion.Enabled; },
				[](Entity& e, bool v) { e.GetComponent<PP>().LensDistortion.Enabled = v; },
				Properties::Meta::Header("Lens Distortion").WithResetSection(
					[](Entity& e) {
						e.GetComponent<PP>().LensDistortion = PP::LensDistortionSettings{};
					})));
			ppProps.push_back(Properties::MakeWith<float>("LD_Intensity", "Intensity",
				[](const Entity& e) { return e.GetComponent<PP>().LensDistortion.Intensity; },
				[](Entity& e, float v) { e.GetComponent<PP>().LensDistortion.Intensity = v; },
				PropertyMetadata{ ldEnabled }.WithClamp(-1.0, 1.0).WithDragSpeed(0.005f)));
			ppProps.push_back(Properties::MakeWith<float>("LD_Scale", "Scale",
				[](const Entity& e) { return e.GetComponent<PP>().LensDistortion.Scale; },
				[](Entity& e, float v) { e.GetComponent<PP>().LensDistortion.Scale = v; },
				PropertyMetadata{ ldEnabled }.WithClamp(0.5, 1.5).WithDragSpeed(0.005f)));
			ppProps.push_back(Properties::MakeWith<Vec2>("LD_Center", "Center",
				[](const Entity& e) { return e.GetComponent<PP>().LensDistortion.Center; },
				[](Entity& e, const Vec2& v) { e.GetComponent<PP>().LensDistortion.Center = v; },
				ldEnabled));

			// Pixelated
			PropertyMetadata pxEnabled = Properties::Meta::EnabledIf<PP>(
				[](const PP& p) { return p.Pixelated.Enabled; });
			PropertyMetadata pxPaletteEnabled = Properties::Meta::EnabledIf<PP>(
				[](const PP& p) { return p.Pixelated.Enabled && p.Pixelated.QuantizeColor; });
			ppProps.push_back(Properties::MakeWith<bool>("PX_Enabled", "Enabled",
				[](const Entity& e) { return e.GetComponent<PP>().Pixelated.Enabled; },
				[](Entity& e, bool v) { e.GetComponent<PP>().Pixelated.Enabled = v; },
				Properties::Meta::Header("Pixelated").WithResetSection(
					[](Entity& e) {
						e.GetComponent<PP>().Pixelated = PP::PixelatedSettings{};
					})));
			ppProps.push_back(Properties::MakeWith<float>("PX_BlockSize", "Block Size",
				[](const Entity& e) { return e.GetComponent<PP>().Pixelated.BlockSize; },
				[](Entity& e, float v) { e.GetComponent<PP>().Pixelated.BlockSize = v; },
				PropertyMetadata{ pxEnabled }.WithClamp(1.0, 64.0).WithDragSpeed(0.1f)));
			ppProps.push_back(Properties::MakeWith<bool>("PX_QuantizeColor", "Quantize Color",
				[](const Entity& e) { return e.GetComponent<PP>().Pixelated.QuantizeColor; },
				[](Entity& e, bool v) { e.GetComponent<PP>().Pixelated.QuantizeColor = v; },
				pxEnabled));
			ppProps.push_back(Properties::MakeWith<int32_t>("PX_PaletteSteps", "Palette Steps",
				[](const Entity& e) { return static_cast<int32_t>(e.GetComponent<PP>().Pixelated.PaletteSteps); },
				[](Entity& e, int32_t v) {
					e.GetComponent<PP>().Pixelated.PaletteSteps = std::max(2, std::min(256, v));
				},
				PropertyMetadata{ pxPaletteEnabled }.WithClamp(2.0, 256.0).WithDragSpeed(1.0f)));

			// Gaussian Blur
			PropertyMetadata gbEnabled = Properties::Meta::EnabledIf<PP>(
				[](const PP& p) { return p.GaussianBlur.Enabled; });
			ppProps.push_back(Properties::MakeWith<bool>("GB_Enabled", "Enabled",
				[](const Entity& e) { return e.GetComponent<PP>().GaussianBlur.Enabled; },
				[](Entity& e, bool v) { e.GetComponent<PP>().GaussianBlur.Enabled = v; },
				Properties::Meta::Header("Gaussian Blur").WithResetSection(
					[](Entity& e) {
						e.GetComponent<PP>().GaussianBlur = PP::GaussianBlurSettings{};
					})));
			ppProps.push_back(Properties::MakeWith<int32_t>("GB_Taps", "Taps",
				[](const Entity& e) { return static_cast<int32_t>(e.GetComponent<PP>().GaussianBlur.Taps); },
				[](Entity& e, int32_t v) {
					e.GetComponent<PP>().GaussianBlur.Taps = std::max(7, std::min(500, v));
				},
				PropertyMetadata{ gbEnabled }.WithClamp(7.0, 500.0).WithDragSpeed(1.0f)));
			ppProps.push_back(Properties::MakeWith<float>("GB_Radius", "Radius",
				[](const Entity& e) { return e.GetComponent<PP>().GaussianBlur.Radius; },
				[](Entity& e, float v) { e.GetComponent<PP>().GaussianBlur.Radius = v; },
				PropertyMetadata{ gbEnabled }.WithClamp(0.0, 1.0).WithDragSpeed(0.005f)));

			// Custom effects
			for (size_t slot = 0; slot < PP{}.CustomEffects.size(); ++slot) {
				PropertyDescriptor effect;
				effect.Name = "Custom_" + std::to_string(slot);
				effect.DisplayName = "Effect " + std::to_string(slot + 1);
				effect.Type = PropertyType::AssetRef;
				effect.Metadata.AssetKindFilter = AssetKind::Material;
				effect.Metadata.Tooltip = "A .material whose shader targets PostProcess. Effects run in this order, after the built-in ones.";
				if (slot == 0) {
					effect.Metadata.HeaderContent = "Custom Effects";
					effect.Metadata.HeaderSize = 5;
				}
				effect.Get = [slot](const Entity& e) -> PropertyValue {
					PropertyValue v;
					v.Type = PropertyType::AssetRef;
					v.UIntValue = e.GetComponent<PP>().CustomEffects[slot];
					return v;
				};
				effect.Set = [slot](Entity& e, const PropertyValue& v) {
					MaterialLibrary::Invalidate(v.UIntValue);
					e.GetComponent<PP>().CustomEffects[slot] = v.UIntValue;
				};
				ppProps.push_back(std::move(effect));
			}

			ComponentInfo ppInfo{ "Post Processing", "Rendering", ComponentCategory::Component };
			ppInfo.serializedName = "PostProcessing2D";
			ppInfo.properties = std::move(ppProps);

			ppInfo.serialize = [](Entity e) -> Json::Value {
				const auto& p = e.GetComponent<PP>();
				Json::Value v = Json::Value::MakeObject();

				Json::Value cg = Json::Value::MakeObject();
				cg.AddMember("enabled",     Json::Value(p.ColorGrading.Enabled));
				cg.AddMember("exposure",    Json::Value(p.ColorGrading.Exposure));
				cg.AddMember("contrast",    Json::Value(p.ColorGrading.Contrast));
				cg.AddMember("saturation",  Json::Value(p.ColorGrading.Saturation));
				cg.AddMember("temperature", Json::Value(p.ColorGrading.Temperature));
				cg.AddMember("tint",        Json::Value(p.ColorGrading.Tint));
				cg.AddMember("colorFilter", UIColorToJson(p.ColorGrading.ColorFilter));
				v.AddMember("colorGrading", std::move(cg));

				Json::Value vg = Json::Value::MakeObject();
				vg.AddMember("enabled",    Json::Value(p.Vignette.Enabled));
				vg.AddMember("color",      UIColorToJson(p.Vignette.Color));
				vg.AddMember("intensity",  Json::Value(p.Vignette.Intensity));
				vg.AddMember("smoothness", Json::Value(p.Vignette.Smoothness));
				vg.AddMember("roundness",  Json::Value(p.Vignette.Roundness));
				vg.AddMember("centerX",    Json::Value(p.Vignette.Center.x));
				vg.AddMember("centerY",    Json::Value(p.Vignette.Center.y));
				v.AddMember("vignette", std::move(vg));

				Json::Value ca = Json::Value::MakeObject();
				ca.AddMember("enabled",   Json::Value(p.ChromaticAberration.Enabled));
				ca.AddMember("intensity", Json::Value(p.ChromaticAberration.Intensity));
				v.AddMember("chromaticAberration", std::move(ca));

				Json::Value gr = Json::Value::MakeObject();
				gr.AddMember("enabled",   Json::Value(p.Grain.Enabled));
				gr.AddMember("intensity", Json::Value(p.Grain.Intensity));
				gr.AddMember("size",      Json::Value(p.Grain.Size));
				gr.AddMember("colored",   Json::Value(p.Grain.Colored));
				v.AddMember("grain", std::move(gr));

				Json::Value bl = Json::Value::MakeObject();
				bl.AddMember("enabled",   Json::Value(p.Bloom.Enabled));
				bl.AddMember("threshold", Json::Value(p.Bloom.Threshold));
				bl.AddMember("intensity", Json::Value(p.Bloom.Intensity));
				bl.AddMember("scatter",   Json::Value(p.Bloom.Scatter));
				bl.AddMember("taps",      Json::Value(p.Bloom.Taps));
				bl.AddMember("tint",      UIColorToJson(p.Bloom.Tint));
				v.AddMember("bloom", std::move(bl));

				Json::Value ld = Json::Value::MakeObject();
				ld.AddMember("enabled",   Json::Value(p.LensDistortion.Enabled));
				ld.AddMember("intensity", Json::Value(p.LensDistortion.Intensity));
				ld.AddMember("scale",     Json::Value(p.LensDistortion.Scale));
				ld.AddMember("centerX",   Json::Value(p.LensDistortion.Center.x));
				ld.AddMember("centerY",   Json::Value(p.LensDistortion.Center.y));
				v.AddMember("lensDistortion", std::move(ld));

				Json::Value px = Json::Value::MakeObject();
				px.AddMember("enabled",       Json::Value(p.Pixelated.Enabled));
				px.AddMember("blockSize",     Json::Value(p.Pixelated.BlockSize));
				px.AddMember("paletteSteps",  Json::Value(p.Pixelated.PaletteSteps));
				px.AddMember("quantizeColor", Json::Value(p.Pixelated.QuantizeColor));
				v.AddMember("pixelated", std::move(px));

				Json::Value gb = Json::Value::MakeObject();
				gb.AddMember("enabled", Json::Value(p.GaussianBlur.Enabled));
				gb.AddMember("radius",  Json::Value(p.GaussianBlur.Radius));
				gb.AddMember("taps",    Json::Value(p.GaussianBlur.Taps));
				v.AddMember("gaussianBlur", std::move(gb));

				// Only when used, so prefabs saved before custom effects existed don't diff against every instance.
				if (p.HasCustomEffect()) {
					Json::Value custom = Json::Value::MakeArray();
					for (uint64_t material : p.CustomEffects) custom.Append(Json::Value(std::to_string(material)));
					v.AddMember("customEffects", std::move(custom));
				}

				return v;
			};

			ppInfo.deserialize = [](Entity e, const Json::Value& v) {
				auto& p = e.GetComponent<PP>();

				// Empty JSON object = SceneSerializer::ResetComponent signal; DeserializeComponent's whitelist excludes this type so we must detect it manually.
				const bool isEmptyReset = v.IsObject()
					&& !v.FindMember("colorGrading")
					&& !v.FindMember("vignette")
					&& !v.FindMember("chromaticAberration")
					&& !v.FindMember("grain")
					&& !v.FindMember("bloom")
					&& !v.FindMember("lensDistortion")
					&& !v.FindMember("pixelated")
					&& !v.FindMember("gaussianBlur")
					&& !v.FindMember("customEffects");
				if (isEmptyReset) {
					p = PP{};
					return;
				}

				if (const Json::Value* cg = v.FindMember("colorGrading")) {
					if (const Json::Value* m = cg->FindMember("enabled"))     p.ColorGrading.Enabled     = m->AsBoolOr(p.ColorGrading.Enabled);
					if (const Json::Value* m = cg->FindMember("exposure"))    p.ColorGrading.Exposure    = static_cast<float>(m->AsDoubleOr(p.ColorGrading.Exposure));
					if (const Json::Value* m = cg->FindMember("contrast"))    p.ColorGrading.Contrast    = static_cast<float>(m->AsDoubleOr(p.ColorGrading.Contrast));
					if (const Json::Value* m = cg->FindMember("saturation"))  p.ColorGrading.Saturation  = static_cast<float>(m->AsDoubleOr(p.ColorGrading.Saturation));
					if (const Json::Value* m = cg->FindMember("temperature")) p.ColorGrading.Temperature = static_cast<float>(m->AsDoubleOr(p.ColorGrading.Temperature));
					if (const Json::Value* m = cg->FindMember("tint"))        p.ColorGrading.Tint        = static_cast<float>(m->AsDoubleOr(p.ColorGrading.Tint));
					if (const Json::Value* m = cg->FindMember("colorFilter")) p.ColorGrading.ColorFilter = UIColorFromJson(*m, p.ColorGrading.ColorFilter);
				}

				if (const Json::Value* vg = v.FindMember("vignette")) {
					if (const Json::Value* m = vg->FindMember("enabled"))    p.Vignette.Enabled    = m->AsBoolOr(p.Vignette.Enabled);
					if (const Json::Value* m = vg->FindMember("color"))      p.Vignette.Color      = UIColorFromJson(*m, p.Vignette.Color);
					if (const Json::Value* m = vg->FindMember("intensity"))  p.Vignette.Intensity  = static_cast<float>(m->AsDoubleOr(p.Vignette.Intensity));
					if (const Json::Value* m = vg->FindMember("smoothness")) p.Vignette.Smoothness = static_cast<float>(m->AsDoubleOr(p.Vignette.Smoothness));
					if (const Json::Value* m = vg->FindMember("roundness"))  p.Vignette.Roundness  = static_cast<float>(m->AsDoubleOr(p.Vignette.Roundness));
					if (const Json::Value* m = vg->FindMember("centerX"))    p.Vignette.Center.x   = static_cast<float>(m->AsDoubleOr(p.Vignette.Center.x));
					if (const Json::Value* m = vg->FindMember("centerY"))    p.Vignette.Center.y   = static_cast<float>(m->AsDoubleOr(p.Vignette.Center.y));
				}

				if (const Json::Value* ca = v.FindMember("chromaticAberration")) {
					if (const Json::Value* m = ca->FindMember("enabled"))   p.ChromaticAberration.Enabled   = m->AsBoolOr(p.ChromaticAberration.Enabled);
					if (const Json::Value* m = ca->FindMember("intensity")) p.ChromaticAberration.Intensity = static_cast<float>(m->AsDoubleOr(p.ChromaticAberration.Intensity));
				}

				if (const Json::Value* gr = v.FindMember("grain")) {
					if (const Json::Value* m = gr->FindMember("enabled"))   p.Grain.Enabled   = m->AsBoolOr(p.Grain.Enabled);
					if (const Json::Value* m = gr->FindMember("intensity")) p.Grain.Intensity = static_cast<float>(m->AsDoubleOr(p.Grain.Intensity));
					if (const Json::Value* m = gr->FindMember("size"))      p.Grain.Size      = static_cast<float>(m->AsDoubleOr(p.Grain.Size));
					if (const Json::Value* m = gr->FindMember("colored"))   p.Grain.Colored   = m->AsBoolOr(p.Grain.Colored);
				}

				if (const Json::Value* bl = v.FindMember("bloom")) {
					if (const Json::Value* m = bl->FindMember("enabled"))   p.Bloom.Enabled   = m->AsBoolOr(p.Bloom.Enabled);
					if (const Json::Value* m = bl->FindMember("threshold")) p.Bloom.Threshold = static_cast<float>(m->AsDoubleOr(p.Bloom.Threshold));
					if (const Json::Value* m = bl->FindMember("intensity")) p.Bloom.Intensity = static_cast<float>(m->AsDoubleOr(p.Bloom.Intensity));
					if (const Json::Value* m = bl->FindMember("scatter"))   p.Bloom.Scatter   = static_cast<float>(m->AsDoubleOr(p.Bloom.Scatter));
					if (const Json::Value* m = bl->FindMember("taps")) {
						p.Bloom.Taps = std::max(7, std::min(500, m->AsIntOr(p.Bloom.Taps)));
					}
					// Legacy: pre-slider scenes used quality enum (0/1/2 → 11/21/33 taps); only consumed when "taps" is absent.
					else if (const Json::Value* m = bl->FindMember("quality")) {
						const int q = m->AsIntOr(1);
						p.Bloom.Taps = (q == 0) ? 11 : (q == 2) ? 33 : 21;
					}
					if (const Json::Value* m = bl->FindMember("tint"))      p.Bloom.Tint      = UIColorFromJson(*m, p.Bloom.Tint);
				}

				if (const Json::Value* ld = v.FindMember("lensDistortion")) {
					if (const Json::Value* m = ld->FindMember("enabled"))   p.LensDistortion.Enabled   = m->AsBoolOr(p.LensDistortion.Enabled);
					if (const Json::Value* m = ld->FindMember("intensity")) p.LensDistortion.Intensity = static_cast<float>(m->AsDoubleOr(p.LensDistortion.Intensity));
					if (const Json::Value* m = ld->FindMember("scale"))     p.LensDistortion.Scale     = static_cast<float>(m->AsDoubleOr(p.LensDistortion.Scale));
					if (const Json::Value* m = ld->FindMember("centerX"))   p.LensDistortion.Center.x  = static_cast<float>(m->AsDoubleOr(p.LensDistortion.Center.x));
					if (const Json::Value* m = ld->FindMember("centerY"))   p.LensDistortion.Center.y  = static_cast<float>(m->AsDoubleOr(p.LensDistortion.Center.y));
				}

				if (const Json::Value* px = v.FindMember("pixelated")) {
					if (const Json::Value* m = px->FindMember("enabled"))       p.Pixelated.Enabled       = m->AsBoolOr(p.Pixelated.Enabled);
					if (const Json::Value* m = px->FindMember("blockSize"))     p.Pixelated.BlockSize     = static_cast<float>(m->AsDoubleOr(p.Pixelated.BlockSize));
					if (const Json::Value* m = px->FindMember("paletteSteps"))  p.Pixelated.PaletteSteps  = m->AsIntOr(p.Pixelated.PaletteSteps);
					if (const Json::Value* m = px->FindMember("quantizeColor")) p.Pixelated.QuantizeColor = m->AsBoolOr(p.Pixelated.QuantizeColor);
				}

				if (const Json::Value* gb = v.FindMember("gaussianBlur")) {
					if (const Json::Value* m = gb->FindMember("enabled")) p.GaussianBlur.Enabled = m->AsBoolOr(p.GaussianBlur.Enabled);
					if (const Json::Value* m = gb->FindMember("radius"))  p.GaussianBlur.Radius  = static_cast<float>(m->AsDoubleOr(p.GaussianBlur.Radius));
					if (const Json::Value* m = gb->FindMember("taps")) {
						p.GaussianBlur.Taps = std::max(7, std::min(500, m->AsIntOr(p.GaussianBlur.Taps)));
					}
					// Legacy migration from the pre-slider quality enum.
					else if (const Json::Value* m = gb->FindMember("quality")) {
						const int q = m->AsIntOr(1);
						p.GaussianBlur.Taps = (q == 0) ? 11 : (q == 2) ? 33 : 21;
					}
				}

				p.CustomEffects = {};
				if (const Json::Value* custom = v.FindMember("customEffects"); custom && custom->IsArray()) {
					const auto& items = custom->GetArray();
					for (size_t i = 0; i < items.size() && i < p.CustomEffects.size(); ++i) {
						p.CustomEffects[i] = items[i].IsNumber() ? items[i].AsUInt64Or(0) : std::strtoull(items[i].AsStringOr().c_str(), nullptr, 10);
					}
				}
			};

			registry.Register<PostProcessing2DComponent>(ppInfo);
		}

		RegisterComponent<TextRendererComponent>(registry, "Text Renderer",
			ComponentCategory::Component, "UI", "TextRenderer",
			{
				Properties::Make("Text", "Text", &TextRendererComponent::Text),
				Properties::MakeFontRef("Font", "Font",
					[](const Entity& e) -> uint64_t {
						return static_cast<uint64_t>(e.GetComponent<TextRendererComponent>().FontAssetId);
					},
					[](Entity& e, uint64_t uuid) {
						auto& text = e.GetComponent<TextRendererComponent>();
						text.FontAssetId = UUID(uuid);
						// Force re-resolve next frame — atlas might need
						// a different size if FontSize changed at the same
						// time, but ResolvedFont = invalid is enough either way.
						text.ResolvedFont = FontHandle{};
					}),
				Properties::MakeWith<float>("FontSize", "Font Size",
					[](const Entity& e) { return e.GetComponent<TextRendererComponent>().FontSize; },
					[](Entity& e, float v) {
						auto& text = e.GetComponent<TextRendererComponent>();
						text.FontSize = v;
						// Different px size means different atlas slot.
						text.ResolvedFont = FontHandle{};
					}),
				Properties::Make("Color", "Color", &TextRendererComponent::Color),
				Properties::MakeWith<float>("LetterSpacing", "Letter Spacing",
					[](const Entity& e) { return e.GetComponent<TextRendererComponent>().LetterSpacing; },
					[](Entity& e, float v) {
						e.GetComponent<TextRendererComponent>().LetterSpacing = v;
					},
					Properties::Meta::DragSpeed(0.1f)),
				Properties::MakeWith<float>("LineSpacing", "Line Spacing",
					[](const Entity& e) { return e.GetComponent<TextRendererComponent>().LineSpacing; },
					[](Entity& e, float v) {
						e.GetComponent<TextRendererComponent>().LineSpacing = v;
					},
					Properties::Meta::DragSpeed(0.25f)),
				Properties::MakeWith<TextAlignment>("Alignment", "Alignment",
					[](const Entity& e) { return e.GetComponent<TextRendererComponent>().HAlign; },
					[](Entity& e, TextAlignment v) {
						e.GetComponent<TextRendererComponent>().HAlign = v;
					}),
				Properties::MakeWith<TextVerticalAlignment>("VerticalAlignment", "Vertical Alignment",
					[](const Entity& e) { return e.GetComponent<TextRendererComponent>().VAlign; },
					[](Entity& e, TextVerticalAlignment v) {
						e.GetComponent<TextRendererComponent>().VAlign = v;
					}),
				Properties::MakeWith<TextWrapMode>("WrapMode", "Wrap Mode",
					[](const Entity& e) { return e.GetComponent<TextRendererComponent>().WrapMode; },
					[](Entity& e, TextWrapMode v) {
						e.GetComponent<TextRendererComponent>().WrapMode = v;
					}),
				Properties::MakeWith<bool>("AutoSize", "Auto Size",
					[](const Entity& e) { return e.GetComponent<TextRendererComponent>().AutoSize; },
					[](Entity& e, bool v) {
						e.GetComponent<TextRendererComponent>().AutoSize = v;
					}),
				// (Left, Top, Right, Bottom) margin in pixels; drives the editor gizmo and reduces effective wrap width.
				Properties::MakeWith<Vec4>("Margin", "Margin",
					[](const Entity& e) { return e.GetComponent<TextRendererComponent>().Margin; },
					[](Entity& e, const Vec4& v) {
						e.GetComponent<TextRendererComponent>().Margin = v;
					}),
				Properties::MakeWith<int16_t>("SortingOrder", "Sorting Order",
					[](const Entity& e) { return e.GetComponent<TextRendererComponent>().SortingOrder; },
					[](Entity& e, int16_t v) {
						e.GetComponent<TextRendererComponent>().SortingOrder = v;
					}),
				Properties::MakeWith<uint8_t>("SortingLayer", "Sorting Layer",
					[](const Entity& e) { return e.GetComponent<TextRendererComponent>().SortingLayer; },
					[](Entity& e, uint8_t v) {
						e.GetComponent<TextRendererComponent>().SortingLayer = v;
					}),
			});

		DeclareConflict<ImageComponent, TextRendererComponent>(registry);
#if INDEX_WITH_2D
		DeclareConflict<SpriteRendererComponent, TextRendererComponent>(registry);
		DeclareConflict<ParticleSystem2DComponent, TextRendererComponent>(registry);
#endif
	}
}
