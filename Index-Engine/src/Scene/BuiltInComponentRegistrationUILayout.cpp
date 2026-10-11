#include "pch.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"

#include "Components/General/General.hpp"
#include "Components/UI/UI.hpp"
#include "Graphics/TextureManager.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// UI masking and layout: mask, circular slider, content size fitter, width constraint, layout groups.
// Split out of BuiltInComponentRegistration.cpp, which owns the call order.
namespace Index::BuiltInComponentRegistrationInternal {
	void RegisterUILayout(ComponentRegistry& registry) {
		// ── Mask ─────────────────────────────────────────────────────
		ComponentInfo maskInfo{ "Mask", "UI", ComponentCategory::Component };
		maskInfo.serializedName = "Mask";
		maskInfo.properties = {
			Properties::Make("ShowMaskGraphic", "Show Mask Graphic", &MaskComponent::ShowMaskGraphic),
		};
		maskInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<MaskComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("showMaskGraphic", Json::Value(c.ShowMaskGraphic));
			return v;
		};
		maskInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<MaskComponent>();
			if (const Json::Value* m = v.FindMember("showMaskGraphic")) c.ShowMaskGraphic = m->AsBoolOr(c.ShowMaskGraphic);
		};
		registry.Register<MaskComponent>(maskInfo);

		// ── Circular Slider ──────────────────────────────────────────
		ComponentInfo csInfo{ "Circular Slider", "UI", ComponentCategory::Component };
		csInfo.serializedName = "CircularSlider";
		csInfo.properties = {
			Properties::MakeWith<float>("Value", "Value",
				[](const Entity& e) { return e.GetComponent<CircularSliderComponent>().Value; },
				[](Entity& e, float v) { auto& c = e.GetComponent<CircularSliderComponent>(); c.Value = std::clamp(v, std::min(c.MinValue, c.MaxValue), std::max(c.MinValue, c.MaxValue)); },
				Properties::Meta::DragSpeed(0.01f)),
			Properties::Make("MinValue",     "Min Value",     &CircularSliderComponent::MinValue),
			Properties::Make("MaxValue",     "Max Value",     &CircularSliderComponent::MaxValue),
			Properties::Make("WholeNumbers", "Whole Numbers", &CircularSliderComponent::WholeNumbers),
			Properties::Make("IsReadOnly",   "Read Only",     &CircularSliderComponent::IsReadOnly),
			Properties::Make("StartAngleDegrees", "Start Angle", &CircularSliderComponent::StartAngleDegrees,
				Properties::Meta::Header("Geometry")),
			Properties::Make("SweepDegrees",   "Sweep",         &CircularSliderComponent::SweepDegrees,
				Properties::Meta::Clamp(1.0, 360.0, 1.0f)),
			Properties::Make("Clockwise",      "Clockwise",     &CircularSliderComponent::Clockwise),
			Properties::Make("RingThickness",  "Ring Thickness",&CircularSliderComponent::RingThickness,
				Properties::Meta::Clamp(0.5, 1024.0, 0.5f)),
			Properties::Make("RingSegments",   "Ring Segments", &CircularSliderComponent::RingSegments,
				Properties::Meta::Clamp(8.0, 256.0, 1.0f)),
			Properties::Make("BackgroundColor", "Background", &CircularSliderComponent::BackgroundColor,
				Properties::Meta::Header("Visual")),
			Properties::Make("FillColor",       "Fill",       &CircularSliderComponent::FillColor),
			Properties::MakeWith<int16_t>("SortingOrder", "Sorting Order",
				[](const Entity& e) { return e.GetComponent<CircularSliderComponent>().SortingOrder; },
				[](Entity& e, int16_t v) { e.GetComponent<CircularSliderComponent>().SortingOrder = v; }),
			Properties::MakeWith<uint8_t>("SortingLayer", "Sorting Layer",
				[](const Entity& e) { return e.GetComponent<CircularSliderComponent>().SortingLayer; },
				[](Entity& e, uint8_t v) { e.GetComponent<CircularSliderComponent>().SortingLayer = v; }),
			MakeUiEntityRef<CircularSliderComponent>("HandleEntity", "Handle Rect", &CircularSliderComponent::HandleEntity),
			Properties::Make("TransitionMode", "Transition",   &CircularSliderComponent::TransitionMode,
				Properties::Meta::Header("Handle Transition")),
			Properties::Make("NormalColor",   "Normal",   &CircularSliderComponent::NormalColor),
			Properties::Make("HoveredColor",  "Hovered",  &CircularSliderComponent::HoveredColor),
			Properties::Make("PressedColor",  "Pressed",  &CircularSliderComponent::PressedColor),
			Properties::Make("DisabledColor", "Disabled", &CircularSliderComponent::DisabledColor),
			Properties::Make("FocusedColor",  "Focused",  &CircularSliderComponent::FocusedColor),
			MakeUiSpriteRef<CircularSliderComponent>("NormalSprite",   "Normal Sprite",   &CircularSliderComponent::NormalSprite),
			MakeUiSpriteRef<CircularSliderComponent>("HoveredSprite",  "Hovered Sprite",  &CircularSliderComponent::HoveredSprite),
			MakeUiSpriteRef<CircularSliderComponent>("PressedSprite",  "Pressed Sprite",  &CircularSliderComponent::PressedSprite),
			MakeUiSpriteRef<CircularSliderComponent>("DisabledSprite", "Disabled Sprite", &CircularSliderComponent::DisabledSprite),
			MakeUiSpriteRef<CircularSliderComponent>("FocusedSprite",  "Focused Sprite",  &CircularSliderComponent::FocusedSprite),
		};
		csInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<CircularSliderComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("value",        Json::Value(c.Value));
			v.AddMember("min",          Json::Value(c.MinValue));
			v.AddMember("max",          Json::Value(c.MaxValue));
			v.AddMember("wholeNumbers", Json::Value(c.WholeNumbers));
			v.AddMember("isReadOnly",   Json::Value(c.IsReadOnly));
			v.AddMember("startAngle",   Json::Value(c.StartAngleDegrees));
			v.AddMember("sweep",        Json::Value(c.SweepDegrees));
			v.AddMember("clockwise",    Json::Value(c.Clockwise));
			v.AddMember("ringThickness",Json::Value(c.RingThickness));
			v.AddMember("ringSegments", Json::Value(c.RingSegments));
			v.AddMember("background",   UIColorToJson(c.BackgroundColor));
			v.AddMember("fill",         UIColorToJson(c.FillColor));
			v.AddMember("sortOrder",    Json::Value(static_cast<int>(c.SortingOrder)));
			v.AddMember("sortLayer",    Json::Value(static_cast<int>(c.SortingLayer)));
			v.AddMember("handleEntity", UIEntityHandleToJson(e, c.HandleEntity));
			v.AddMember("transitionMode", Json::Value(static_cast<int>(c.TransitionMode)));
			v.AddMember("normal",   UIColorToJson(c.NormalColor));
			v.AddMember("hovered",  UIColorToJson(c.HoveredColor));
			v.AddMember("pressed",  UIColorToJson(c.PressedColor));
			v.AddMember("disabled", UIColorToJson(c.DisabledColor));
			v.AddMember("focused",  UIColorToJson(c.FocusedColor));
			v.AddMember("normalSprite",   UIUuidToJson(c.NormalSprite));
			v.AddMember("hoveredSprite",  UIUuidToJson(c.HoveredSprite));
			v.AddMember("pressedSprite",  UIUuidToJson(c.PressedSprite));
			v.AddMember("disabledSprite", UIUuidToJson(c.DisabledSprite));
			v.AddMember("focusedSprite",  UIUuidToJson(c.FocusedSprite));
			return v;
		};
		csInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<CircularSliderComponent>();
			if (const Json::Value* m = v.FindMember("value"))         c.Value         = static_cast<float>(m->AsDoubleOr(c.Value));
			if (const Json::Value* m = v.FindMember("min"))           c.MinValue      = static_cast<float>(m->AsDoubleOr(c.MinValue));
			if (const Json::Value* m = v.FindMember("max"))           c.MaxValue      = static_cast<float>(m->AsDoubleOr(c.MaxValue));
			if (const Json::Value* m = v.FindMember("wholeNumbers"))  c.WholeNumbers  = m->AsBoolOr(c.WholeNumbers);
			if (const Json::Value* m = v.FindMember("isReadOnly"))    c.IsReadOnly    = m->AsBoolOr(c.IsReadOnly);
			if (const Json::Value* m = v.FindMember("startAngle"))    c.StartAngleDegrees = static_cast<float>(m->AsDoubleOr(c.StartAngleDegrees));
			if (const Json::Value* m = v.FindMember("sweep"))         c.SweepDegrees  = static_cast<float>(m->AsDoubleOr(c.SweepDegrees));
			if (const Json::Value* m = v.FindMember("clockwise"))     c.Clockwise     = m->AsBoolOr(c.Clockwise);
			if (const Json::Value* m = v.FindMember("ringThickness")) c.RingThickness = static_cast<float>(m->AsDoubleOr(c.RingThickness));
			if (const Json::Value* m = v.FindMember("ringSegments"))  c.RingSegments  = m->AsIntOr(c.RingSegments);
			if (const Json::Value* m = v.FindMember("background"))    c.BackgroundColor = UIColorFromJson(*m, c.BackgroundColor);
			if (const Json::Value* m = v.FindMember("fill"))          c.FillColor       = UIColorFromJson(*m, c.FillColor);
			if (const Json::Value* m = v.FindMember("sortOrder"))     c.SortingOrder    = static_cast<int16_t>(m->AsIntOr(c.SortingOrder));
			if (const Json::Value* m = v.FindMember("sortLayer"))     c.SortingLayer    = static_cast<uint8_t>(m->AsIntOr(c.SortingLayer));
			if (const Json::Value* m = v.FindMember("handleEntity"))  UIEntityHandleFromJson(e, *m, c.HandleEntity);
			if (const Json::Value* m = v.FindMember("transitionMode")) c.TransitionMode = static_cast<UITransitionMode>(m->AsIntOr(static_cast<int>(c.TransitionMode)));
			if (const Json::Value* m = v.FindMember("normal"))   c.NormalColor   = UIColorFromJson(*m, c.NormalColor);
			if (const Json::Value* m = v.FindMember("hovered"))  c.HoveredColor  = UIColorFromJson(*m, c.HoveredColor);
			if (const Json::Value* m = v.FindMember("pressed"))  c.PressedColor  = UIColorFromJson(*m, c.PressedColor);
			if (const Json::Value* m = v.FindMember("disabled")) c.DisabledColor = UIColorFromJson(*m, c.DisabledColor);
			if (const Json::Value* m = v.FindMember("focused"))  c.FocusedColor  = UIColorFromJson(*m, c.FocusedColor);
			if (const Json::Value* m = v.FindMember("normalSprite"))   c.NormalSprite   = UIUuidFromJson(*m, c.NormalSprite);
			if (const Json::Value* m = v.FindMember("hoveredSprite"))  c.HoveredSprite  = UIUuidFromJson(*m, c.HoveredSprite);
			if (const Json::Value* m = v.FindMember("pressedSprite"))  c.PressedSprite  = UIUuidFromJson(*m, c.PressedSprite);
			if (const Json::Value* m = v.FindMember("disabledSprite")) c.DisabledSprite = UIUuidFromJson(*m, c.DisabledSprite);
			if (const Json::Value* m = v.FindMember("focusedSprite"))  c.FocusedSprite  = UIUuidFromJson(*m, c.FocusedSprite);
		};
		registry.Register<CircularSliderComponent>(csInfo);

		// ── Content Size Fitter ──────────────────────────────────────
		ComponentInfo csfInfo{ "Content Size Fitter", "UI", ComponentCategory::Component };
		csfInfo.serializedName = "ContentSizeFitter";
		csfInfo.properties = {
			Properties::Make("HorizontalFit", "Horizontal Fit", &ContentSizeFitterComponent::HorizontalFit),
			Properties::Make("VerticalFit",   "Vertical Fit",   &ContentSizeFitterComponent::VerticalFit),
			Properties::Make("PaddingLeft",   "Left",   &ContentSizeFitterComponent::PaddingLeft,
				Properties::Meta::Header("Padding")),
			Properties::Make("PaddingRight",  "Right",  &ContentSizeFitterComponent::PaddingRight),
			Properties::Make("PaddingTop",    "Top",    &ContentSizeFitterComponent::PaddingTop),
			Properties::Make("PaddingBottom", "Bottom", &ContentSizeFitterComponent::PaddingBottom),
		};
		csfInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<ContentSizeFitterComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("horizontalFit", Json::Value(c.HorizontalFit));
			v.AddMember("verticalFit",   Json::Value(c.VerticalFit));
			v.AddMember("paddingLeft",   Json::Value(c.PaddingLeft));
			v.AddMember("paddingRight",  Json::Value(c.PaddingRight));
			v.AddMember("paddingTop",    Json::Value(c.PaddingTop));
			v.AddMember("paddingBottom", Json::Value(c.PaddingBottom));
			return v;
		};
		csfInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<ContentSizeFitterComponent>();
			if (const Json::Value* m = v.FindMember("horizontalFit")) c.HorizontalFit = m->AsBoolOr(c.HorizontalFit);
			if (const Json::Value* m = v.FindMember("verticalFit"))   c.VerticalFit   = m->AsBoolOr(c.VerticalFit);
			if (const Json::Value* m = v.FindMember("paddingLeft"))   c.PaddingLeft   = static_cast<float>(m->AsDoubleOr(c.PaddingLeft));
			if (const Json::Value* m = v.FindMember("paddingRight"))  c.PaddingRight  = static_cast<float>(m->AsDoubleOr(c.PaddingRight));
			if (const Json::Value* m = v.FindMember("paddingTop"))    c.PaddingTop    = static_cast<float>(m->AsDoubleOr(c.PaddingTop));
			if (const Json::Value* m = v.FindMember("paddingBottom")) c.PaddingBottom = static_cast<float>(m->AsDoubleOr(c.PaddingBottom));
		};
		registry.Register<ContentSizeFitterComponent>(csfInfo);

		// ── Width Constraint ─────────────────────────────────────────
		ComponentInfo wcInfo{ "Width Constraint", "UI", ComponentCategory::Component };
		wcInfo.serializedName = "WidthConstraint";
		wcInfo.properties = {
			Properties::Make("MinWidth", "Min Width", &WidthConstraintComponent::MinWidth),
			Properties::Make("MaxWidth", "Max Width", &WidthConstraintComponent::MaxWidth),
		};
		wcInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<WidthConstraintComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("minWidth", Json::Value(c.MinWidth));
			v.AddMember("maxWidth", Json::Value(c.MaxWidth));
			return v;
		};
		wcInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<WidthConstraintComponent>();
			if (const Json::Value* m = v.FindMember("minWidth")) c.MinWidth = static_cast<float>(m->AsDoubleOr(c.MinWidth));
			if (const Json::Value* m = v.FindMember("maxWidth")) c.MaxWidth = static_cast<float>(m->AsDoubleOr(c.MaxWidth));
		};
		registry.Register<WidthConstraintComponent>(wcInfo);

		// ── Layout groups ────────────────────────────────────────────
		auto registerHorizontalLayout = [&]() {
			ComponentInfo info{ "Horizontal Layout Group", "UI", ComponentCategory::Component };
			info.serializedName = "HorizontalLayoutGroup";
			info.properties = {
				Properties::Make("PaddingLeft",   "Left",   &HorizontalLayoutGroupComponent::PaddingLeft,
					Properties::Meta::Header("Padding")),
				Properties::Make("PaddingRight",  "Right",  &HorizontalLayoutGroupComponent::PaddingRight),
				Properties::Make("PaddingTop",    "Top",    &HorizontalLayoutGroupComponent::PaddingTop),
				Properties::Make("PaddingBottom", "Bottom", &HorizontalLayoutGroupComponent::PaddingBottom),
				Properties::Make("Spacing",            "Spacing",             &HorizontalLayoutGroupComponent::Spacing,
					Properties::Meta::Header("Layout")),
				Properties::Make("ChildAlignment",     "Child Alignment",     &HorizontalLayoutGroupComponent::ChildAlignment),
				Properties::Make("ReverseArrangement", "Reverse Arrangement", &HorizontalLayoutGroupComponent::ReverseArrangement),
				Properties::Make("ControlChildWidth",  "Control Child Width",  &HorizontalLayoutGroupComponent::ControlChildWidth,
					Properties::Meta::Header("Children")),
				Properties::Make("ControlChildHeight", "Control Child Height", &HorizontalLayoutGroupComponent::ControlChildHeight),
				Properties::Make("UseChildScaleWidth",  "Use Child Scale Width",  &HorizontalLayoutGroupComponent::UseChildScaleWidth,
					PropertyMetadata{}.WithEnabledIf([](const Entity& e) {
						return e.GetComponent<HorizontalLayoutGroupComponent>().ControlChildWidth;
					})),
				Properties::Make("UseChildScaleHeight", "Use Child Scale Height", &HorizontalLayoutGroupComponent::UseChildScaleHeight,
					PropertyMetadata{}.WithEnabledIf([](const Entity& e) {
						return e.GetComponent<HorizontalLayoutGroupComponent>().ControlChildHeight;
					})),
				Properties::Make("ChildForceExpandWidth",  "Child Force Expand Width",  &HorizontalLayoutGroupComponent::ChildForceExpandWidth,
					PropertyMetadata{}.WithEnabledIf([](const Entity& e) {
						return e.GetComponent<HorizontalLayoutGroupComponent>().ControlChildWidth;
					})),
				Properties::Make("ChildForceExpandHeight", "Child Force Expand Height", &HorizontalLayoutGroupComponent::ChildForceExpandHeight,
					PropertyMetadata{}.WithEnabledIf([](const Entity& e) {
						return e.GetComponent<HorizontalLayoutGroupComponent>().ControlChildHeight;
					})),
			};
			info.serialize = [](Entity e) -> Json::Value {
				const auto& c = e.GetComponent<HorizontalLayoutGroupComponent>();
				Json::Value v = Json::Value::MakeObject();
				v.AddMember("paddingLeft",   Json::Value(c.PaddingLeft));
				v.AddMember("paddingRight",  Json::Value(c.PaddingRight));
				v.AddMember("paddingTop",    Json::Value(c.PaddingTop));
				v.AddMember("paddingBottom", Json::Value(c.PaddingBottom));
				v.AddMember("spacing",       Json::Value(c.Spacing));
				v.AddMember("childAlignment", Json::Value(static_cast<int>(c.ChildAlignment)));
				v.AddMember("reverseArrangement", Json::Value(c.ReverseArrangement));
				v.AddMember("controlChildWidth",  Json::Value(c.ControlChildWidth));
				v.AddMember("controlChildHeight", Json::Value(c.ControlChildHeight));
				v.AddMember("useChildScaleWidth",  Json::Value(c.UseChildScaleWidth));
				v.AddMember("useChildScaleHeight", Json::Value(c.UseChildScaleHeight));
				v.AddMember("childForceExpandWidth",  Json::Value(c.ChildForceExpandWidth));
				v.AddMember("childForceExpandHeight", Json::Value(c.ChildForceExpandHeight));
				return v;
			};
			info.deserialize = [](Entity e, const Json::Value& v) {
				auto& c = e.GetComponent<HorizontalLayoutGroupComponent>();
				if (const Json::Value* m = v.FindMember("paddingLeft"))   c.PaddingLeft   = static_cast<float>(m->AsDoubleOr(c.PaddingLeft));
				if (const Json::Value* m = v.FindMember("paddingRight"))  c.PaddingRight  = static_cast<float>(m->AsDoubleOr(c.PaddingRight));
				if (const Json::Value* m = v.FindMember("paddingTop"))    c.PaddingTop    = static_cast<float>(m->AsDoubleOr(c.PaddingTop));
				if (const Json::Value* m = v.FindMember("paddingBottom")) c.PaddingBottom = static_cast<float>(m->AsDoubleOr(c.PaddingBottom));
				if (const Json::Value* m = v.FindMember("spacing"))       c.Spacing       = static_cast<float>(m->AsDoubleOr(c.Spacing));
				if (const Json::Value* m = v.FindMember("childAlignment"))     c.ChildAlignment     = static_cast<UIAlignment>(m->AsIntOr(static_cast<int>(c.ChildAlignment)));
				if (const Json::Value* m = v.FindMember("reverseArrangement")) c.ReverseArrangement = m->AsBoolOr(c.ReverseArrangement);
				if (const Json::Value* m = v.FindMember("controlChildWidth"))  c.ControlChildWidth  = m->AsBoolOr(c.ControlChildWidth);
				if (const Json::Value* m = v.FindMember("controlChildHeight")) c.ControlChildHeight = m->AsBoolOr(c.ControlChildHeight);
				if (const Json::Value* m = v.FindMember("useChildScaleWidth"))  c.UseChildScaleWidth  = m->AsBoolOr(c.UseChildScaleWidth);
				if (const Json::Value* m = v.FindMember("useChildScaleHeight")) c.UseChildScaleHeight = m->AsBoolOr(c.UseChildScaleHeight);
				if (const Json::Value* m = v.FindMember("childForceExpandWidth"))  c.ChildForceExpandWidth  = m->AsBoolOr(c.ChildForceExpandWidth);
				if (const Json::Value* m = v.FindMember("childForceExpandHeight")) c.ChildForceExpandHeight = m->AsBoolOr(c.ChildForceExpandHeight);
			};
			registry.Register<HorizontalLayoutGroupComponent>(info);
		};
		registerHorizontalLayout();

		auto registerVerticalLayout = [&]() {
			ComponentInfo info{ "Vertical Layout Group", "UI", ComponentCategory::Component };
			info.serializedName = "VerticalLayoutGroup";
			info.properties = {
				Properties::Make("PaddingLeft",   "Left",   &VerticalLayoutGroupComponent::PaddingLeft,
					Properties::Meta::Header("Padding")),
				Properties::Make("PaddingRight",  "Right",  &VerticalLayoutGroupComponent::PaddingRight),
				Properties::Make("PaddingTop",    "Top",    &VerticalLayoutGroupComponent::PaddingTop),
				Properties::Make("PaddingBottom", "Bottom", &VerticalLayoutGroupComponent::PaddingBottom),
				Properties::Make("Spacing",            "Spacing",             &VerticalLayoutGroupComponent::Spacing,
					Properties::Meta::Header("Layout")),
				Properties::Make("ChildAlignment",     "Child Alignment",     &VerticalLayoutGroupComponent::ChildAlignment),
				Properties::Make("ReverseArrangement", "Reverse Arrangement", &VerticalLayoutGroupComponent::ReverseArrangement),
				Properties::Make("ControlChildWidth",  "Control Child Width",  &VerticalLayoutGroupComponent::ControlChildWidth,
					Properties::Meta::Header("Children")),
				Properties::Make("ControlChildHeight", "Control Child Height", &VerticalLayoutGroupComponent::ControlChildHeight),
				Properties::Make("UseChildScaleWidth",  "Use Child Scale Width",  &VerticalLayoutGroupComponent::UseChildScaleWidth,
					PropertyMetadata{}.WithEnabledIf([](const Entity& e) {
						return e.GetComponent<VerticalLayoutGroupComponent>().ControlChildWidth;
					})),
				Properties::Make("UseChildScaleHeight", "Use Child Scale Height", &VerticalLayoutGroupComponent::UseChildScaleHeight,
					PropertyMetadata{}.WithEnabledIf([](const Entity& e) {
						return e.GetComponent<VerticalLayoutGroupComponent>().ControlChildHeight;
					})),
				Properties::Make("ChildForceExpandWidth",  "Child Force Expand Width",  &VerticalLayoutGroupComponent::ChildForceExpandWidth,
					PropertyMetadata{}.WithEnabledIf([](const Entity& e) {
						return e.GetComponent<VerticalLayoutGroupComponent>().ControlChildWidth;
					})),
				Properties::Make("ChildForceExpandHeight", "Child Force Expand Height", &VerticalLayoutGroupComponent::ChildForceExpandHeight,
					PropertyMetadata{}.WithEnabledIf([](const Entity& e) {
						return e.GetComponent<VerticalLayoutGroupComponent>().ControlChildHeight;
					})),
			};
			info.serialize = [](Entity e) -> Json::Value {
				const auto& c = e.GetComponent<VerticalLayoutGroupComponent>();
				Json::Value v = Json::Value::MakeObject();
				v.AddMember("paddingLeft",   Json::Value(c.PaddingLeft));
				v.AddMember("paddingRight",  Json::Value(c.PaddingRight));
				v.AddMember("paddingTop",    Json::Value(c.PaddingTop));
				v.AddMember("paddingBottom", Json::Value(c.PaddingBottom));
				v.AddMember("spacing",       Json::Value(c.Spacing));
				v.AddMember("childAlignment", Json::Value(static_cast<int>(c.ChildAlignment)));
				v.AddMember("reverseArrangement", Json::Value(c.ReverseArrangement));
				v.AddMember("controlChildWidth",  Json::Value(c.ControlChildWidth));
				v.AddMember("controlChildHeight", Json::Value(c.ControlChildHeight));
				v.AddMember("useChildScaleWidth",  Json::Value(c.UseChildScaleWidth));
				v.AddMember("useChildScaleHeight", Json::Value(c.UseChildScaleHeight));
				v.AddMember("childForceExpandWidth",  Json::Value(c.ChildForceExpandWidth));
				v.AddMember("childForceExpandHeight", Json::Value(c.ChildForceExpandHeight));
				return v;
			};
			info.deserialize = [](Entity e, const Json::Value& v) {
				auto& c = e.GetComponent<VerticalLayoutGroupComponent>();
				if (const Json::Value* m = v.FindMember("paddingLeft"))   c.PaddingLeft   = static_cast<float>(m->AsDoubleOr(c.PaddingLeft));
				if (const Json::Value* m = v.FindMember("paddingRight"))  c.PaddingRight  = static_cast<float>(m->AsDoubleOr(c.PaddingRight));
				if (const Json::Value* m = v.FindMember("paddingTop"))    c.PaddingTop    = static_cast<float>(m->AsDoubleOr(c.PaddingTop));
				if (const Json::Value* m = v.FindMember("paddingBottom")) c.PaddingBottom = static_cast<float>(m->AsDoubleOr(c.PaddingBottom));
				if (const Json::Value* m = v.FindMember("spacing"))       c.Spacing       = static_cast<float>(m->AsDoubleOr(c.Spacing));
				if (const Json::Value* m = v.FindMember("childAlignment"))     c.ChildAlignment     = static_cast<UIAlignment>(m->AsIntOr(static_cast<int>(c.ChildAlignment)));
				if (const Json::Value* m = v.FindMember("reverseArrangement")) c.ReverseArrangement = m->AsBoolOr(c.ReverseArrangement);
				if (const Json::Value* m = v.FindMember("controlChildWidth"))  c.ControlChildWidth  = m->AsBoolOr(c.ControlChildWidth);
				if (const Json::Value* m = v.FindMember("controlChildHeight")) c.ControlChildHeight = m->AsBoolOr(c.ControlChildHeight);
				if (const Json::Value* m = v.FindMember("useChildScaleWidth"))  c.UseChildScaleWidth  = m->AsBoolOr(c.UseChildScaleWidth);
				if (const Json::Value* m = v.FindMember("useChildScaleHeight")) c.UseChildScaleHeight = m->AsBoolOr(c.UseChildScaleHeight);
				if (const Json::Value* m = v.FindMember("childForceExpandWidth"))  c.ChildForceExpandWidth  = m->AsBoolOr(c.ChildForceExpandWidth);
				if (const Json::Value* m = v.FindMember("childForceExpandHeight")) c.ChildForceExpandHeight = m->AsBoolOr(c.ChildForceExpandHeight);
			};
			registry.Register<VerticalLayoutGroupComponent>(info);
		};
		registerVerticalLayout();

		auto registerGridLayout = [&]() {
			ComponentInfo info{ "Grid Layout Group", "UI", ComponentCategory::Component };
			info.serializedName = "GridLayoutGroup";
			info.properties = {
				Properties::Make("PaddingLeft",   "Left",   &GridLayoutGroupComponent::PaddingLeft,
					Properties::Meta::Header("Padding")),
				Properties::Make("PaddingRight",  "Right",  &GridLayoutGroupComponent::PaddingRight),
				Properties::Make("PaddingTop",    "Top",    &GridLayoutGroupComponent::PaddingTop),
				Properties::Make("PaddingBottom", "Bottom", &GridLayoutGroupComponent::PaddingBottom),
				Properties::Make("CellSize", "Cell Size", &GridLayoutGroupComponent::CellSize,
					Properties::Meta::Header("Cells")),
				Properties::Make("Spacing",  "Spacing",   &GridLayoutGroupComponent::Spacing),
				Properties::Make("StartCorner",    "Start Corner",    &GridLayoutGroupComponent::StartCorner,
					Properties::Meta::Header("Layout")),
				Properties::Make("StartAxis",      "Start Axis",      &GridLayoutGroupComponent::StartAxis),
				Properties::Make("ChildAlignment", "Child Alignment", &GridLayoutGroupComponent::ChildAlignment),
				Properties::Make("Constraint",     "Constraint",      &GridLayoutGroupComponent::Constraint),
				// Gate: ConstraintCount only matters for the fixed-count
				// modes. Greys out for the Flexible layout where the
				// row/column count is derived from cell+rect size.
				Properties::Make("ConstraintCount", "Constraint Count", &GridLayoutGroupComponent::ConstraintCount,
					PropertyMetadata{}.WithEnabledIf([](const Entity& e) {
						return e.GetComponent<GridLayoutGroupComponent>().Constraint != GridLayoutConstraint::Flexible;
					})),
				Properties::Make("Reverse", "Reverse", &GridLayoutGroupComponent::Reverse),
			};
			info.serialize = [](Entity e) -> Json::Value {
				const auto& c = e.GetComponent<GridLayoutGroupComponent>();
				Json::Value v = Json::Value::MakeObject();
				v.AddMember("paddingLeft",   Json::Value(c.PaddingLeft));
				v.AddMember("paddingRight",  Json::Value(c.PaddingRight));
				v.AddMember("paddingTop",    Json::Value(c.PaddingTop));
				v.AddMember("paddingBottom", Json::Value(c.PaddingBottom));
				Json::Value cellSize = Json::Value::MakeObject();
				cellSize.AddMember("x", Json::Value(c.CellSize.x));
				cellSize.AddMember("y", Json::Value(c.CellSize.y));
				v.AddMember("cellSize", std::move(cellSize));
				Json::Value spacing = Json::Value::MakeObject();
				spacing.AddMember("x", Json::Value(c.Spacing.x));
				spacing.AddMember("y", Json::Value(c.Spacing.y));
				v.AddMember("spacing", std::move(spacing));
				v.AddMember("startCorner",     Json::Value(static_cast<int>(c.StartCorner)));
				v.AddMember("startAxis",       Json::Value(static_cast<int>(c.StartAxis)));
				v.AddMember("childAlignment",  Json::Value(static_cast<int>(c.ChildAlignment)));
				v.AddMember("constraint",      Json::Value(static_cast<int>(c.Constraint)));
				v.AddMember("constraintCount", Json::Value(c.ConstraintCount));
				v.AddMember("reverse",         Json::Value(c.Reverse));
				return v;
			};
			info.deserialize = [](Entity e, const Json::Value& v) {
				auto& c = e.GetComponent<GridLayoutGroupComponent>();
				if (const Json::Value* m = v.FindMember("paddingLeft"))   c.PaddingLeft   = static_cast<float>(m->AsDoubleOr(c.PaddingLeft));
				if (const Json::Value* m = v.FindMember("paddingRight"))  c.PaddingRight  = static_cast<float>(m->AsDoubleOr(c.PaddingRight));
				if (const Json::Value* m = v.FindMember("paddingTop"))    c.PaddingTop    = static_cast<float>(m->AsDoubleOr(c.PaddingTop));
				if (const Json::Value* m = v.FindMember("paddingBottom")) c.PaddingBottom = static_cast<float>(m->AsDoubleOr(c.PaddingBottom));
				if (const Json::Value* m = v.FindMember("cellSize"); m && m->IsObject()) {
					if (const Json::Value* x = m->FindMember("x")) c.CellSize.x = static_cast<float>(x->AsDoubleOr(c.CellSize.x));
					if (const Json::Value* y = m->FindMember("y")) c.CellSize.y = static_cast<float>(y->AsDoubleOr(c.CellSize.y));
				}
				if (const Json::Value* m = v.FindMember("spacing"); m && m->IsObject()) {
					if (const Json::Value* x = m->FindMember("x")) c.Spacing.x = static_cast<float>(x->AsDoubleOr(c.Spacing.x));
					if (const Json::Value* y = m->FindMember("y")) c.Spacing.y = static_cast<float>(y->AsDoubleOr(c.Spacing.y));
				}
				if (const Json::Value* m = v.FindMember("startCorner"))     c.StartCorner     = static_cast<GridLayoutStartCorner>(m->AsIntOr(static_cast<int>(c.StartCorner)));
				if (const Json::Value* m = v.FindMember("startAxis"))       c.StartAxis       = static_cast<GridLayoutStartAxis>(m->AsIntOr(static_cast<int>(c.StartAxis)));
				if (const Json::Value* m = v.FindMember("childAlignment"))  c.ChildAlignment  = static_cast<UIAlignment>(m->AsIntOr(static_cast<int>(c.ChildAlignment)));
				if (const Json::Value* m = v.FindMember("constraint"))      c.Constraint      = static_cast<GridLayoutConstraint>(m->AsIntOr(static_cast<int>(c.Constraint)));
				if (const Json::Value* m = v.FindMember("constraintCount")) c.ConstraintCount = m->AsIntOr(c.ConstraintCount);
				if (const Json::Value* m = v.FindMember("reverse"))         c.Reverse         = m->AsBoolOr(c.Reverse);
			};
			registry.Register<GridLayoutGroupComponent>(info);
		};
		registerGridLayout();
	}
}
