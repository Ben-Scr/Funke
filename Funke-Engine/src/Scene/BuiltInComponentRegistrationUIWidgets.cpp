#include "pch.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"

#include "Components/General/General.hpp"
#include "Components/UI/UI.hpp"
#include "Graphics/TextureManager.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// Interactive UI widgets: interactable, button, slider, input field, dropdown, toggle, scrollbar, scroll rect.
// Split out of BuiltInComponentRegistration.cpp, which owns the call order.
namespace Funke::BuiltInComponentRegistrationInternal {
	void RegisterUIWidgets(ComponentRegistry& registry) {
		// ── UI widgets ──────────────────────────────────────────────

		ComponentInfo interactableInfo{ "Interactable", "UI", ComponentCategory::Component };
		interactableInfo.serializedName = "Interactable";
		interactableInfo.properties = {
			Properties::Make("Interactable", "Interactable", &InteractableComponent::Interactable),
			// Opt-in keyboard / controller navigation. Defaults to false
			// so existing scenes are not silently included in Tab order.
			Properties::Make("Focusable",    "Focusable",    &InteractableComponent::Focusable),
		};
		interactableInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<InteractableComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("interactable", Json::Value(c.Interactable));
			v.AddMember("focusable",    Json::Value(c.Focusable));
			return v;
		};
		interactableInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<InteractableComponent>();
			if (const Json::Value* m = v.FindMember("interactable")) c.Interactable = m->AsBoolOr(c.Interactable);
			if (const Json::Value* m = v.FindMember("focusable"))    c.Focusable    = m->AsBoolOr(c.Focusable);
		};
		registry.Register<InteractableComponent>(interactableInfo);

		ComponentInfo buttonInfo{ "Button", "UI", ComponentCategory::Component };
		buttonInfo.serializedName = "Button";
		buttonInfo.properties = {
			MakeUiEntityRef<ButtonComponent>("TargetGraphic", "Target Graphic",
				&ButtonComponent::TargetGraphic),
			// Variant: TransitionMode picks which per-state slots show.
			// Alpha == 0 on FocusedColor = no focus tint (see ButtonComponent.hpp).
			Properties::MakeVariant("TransitionMode", "Transition Mode",
				&ButtonComponent::TransitionMode,
				{
					Properties::Branch(UITransitionMode::ColorSwap, {
						Properties::Make("NormalColor",   "Normal Color",   &ButtonComponent::NormalColor),
						Properties::Make("HoveredColor",  "Hovered Color",  &ButtonComponent::HoveredColor),
						Properties::Make("PressedColor",  "Pressed Color",  &ButtonComponent::PressedColor),
						Properties::Make("DisabledColor", "Disabled Color", &ButtonComponent::DisabledColor),
						Properties::Make("FocusedColor",  "Focused Color",  &ButtonComponent::FocusedColor),
					}),
					Properties::Branch(UITransitionMode::SpriteSwap, {
						MakeUiSpriteRef<ButtonComponent>("NormalSprite",   "Normal Sprite",   &ButtonComponent::NormalSprite),
						MakeUiSpriteRef<ButtonComponent>("HoveredSprite",  "Hovered Sprite",  &ButtonComponent::HoveredSprite),
						MakeUiSpriteRef<ButtonComponent>("PressedSprite",  "Pressed Sprite",  &ButtonComponent::PressedSprite),
						MakeUiSpriteRef<ButtonComponent>("DisabledSprite", "Disabled Sprite", &ButtonComponent::DisabledSprite),
						MakeUiSpriteRef<ButtonComponent>("FocusedSprite",  "Focused Sprite",  &ButtonComponent::FocusedSprite),
					}),
					Properties::Branch(UITransitionMode::None, {}),
				}),
		};
		buttonInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<ButtonComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("targetGraphic",  UIEntityHandleToJson(e, c.TargetGraphic));
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

			// OnClick event list. UUID 0 == "self", same convention as
			// UIEntityHandleToJson at file scope.
			v.AddMember("onClick", UIEventListToJson(c.OnClick));
			return v;
		};
		buttonInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<ButtonComponent>();
			if (const Json::Value* m = v.FindMember("targetGraphic")) UIEntityHandleFromJson(e, *m, c.TargetGraphic);
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

			if (const Json::Value* m = v.FindMember("onClick")) {
				UIEventListFromJson(e, *m, c.OnClick);
			}
		};
		buttonInfo.onAdd = &InheritImageColorIntoNormal<ButtonComponent>;
		registry.Register<ButtonComponent>(buttonInfo);

		ComponentInfo sliderInfo{ "Slider", "UI", ComponentCategory::Component };
		sliderInfo.serializedName = "Slider";
		sliderInfo.properties = {
			Properties::MakeWith<float>("Value", "Value",
				[](const Entity& e) { return e.GetComponent<SliderComponent>().Value; },
				[](Entity& e, float v) { auto& c = e.GetComponent<SliderComponent>(); c.Value = std::clamp(v, std::min(c.MinValue, c.MaxValue), std::max(c.MinValue, c.MaxValue)); },
				Properties::Meta::DragSpeed(0.01f)),
			Properties::Make("Direction",    "Direction",     &SliderComponent::Direction),
			Properties::Make("MinValue",     "Min Value",     &SliderComponent::MinValue),
			Properties::Make("MaxValue",     "Max Value",     &SliderComponent::MaxValue),
			Properties::Make("WholeNumbers", "Whole Numbers", &SliderComponent::WholeNumbers),
			Properties::Make("IsReadOnly",   "Read Only",     &SliderComponent::IsReadOnly),
			MakeUiEntityRef<SliderComponent>("BackgroundEntity", "Background Rect", &SliderComponent::BackgroundEntity),
			MakeUiEntityRef<SliderComponent>("FillEntity",       "Fill Rect",       &SliderComponent::FillEntity),
			MakeUiEntityRef<SliderComponent>("HandleEntity",     "Handle Rect",     &SliderComponent::HandleEntity),
			MakeUiEntityRef<SliderComponent>("LabelEntity",      "Label",           &SliderComponent::LabelEntity),
			Properties::MakeVariant("TransitionMode", "Transition Mode",
				&SliderComponent::TransitionMode,
				{
					Properties::Branch(UITransitionMode::ColorSwap, {
						Properties::Make("NormalColor",   "Normal Color",   &SliderComponent::NormalColor),
						Properties::Make("HoveredColor",  "Hovered Color",  &SliderComponent::HoveredColor),
						Properties::Make("PressedColor",  "Pressed Color",  &SliderComponent::PressedColor),
						Properties::Make("DisabledColor", "Disabled Color", &SliderComponent::DisabledColor),
						Properties::Make("FocusedColor",  "Focused Color",  &SliderComponent::FocusedColor),
					}),
					Properties::Branch(UITransitionMode::SpriteSwap, {
						MakeUiSpriteRef<SliderComponent>("NormalSprite",   "Normal Sprite",   &SliderComponent::NormalSprite),
						MakeUiSpriteRef<SliderComponent>("HoveredSprite",  "Hovered Sprite",  &SliderComponent::HoveredSprite),
						MakeUiSpriteRef<SliderComponent>("PressedSprite",  "Pressed Sprite",  &SliderComponent::PressedSprite),
						MakeUiSpriteRef<SliderComponent>("DisabledSprite", "Disabled Sprite", &SliderComponent::DisabledSprite),
						MakeUiSpriteRef<SliderComponent>("FocusedSprite",  "Focused Sprite",  &SliderComponent::FocusedSprite),
					}),
					Properties::Branch(UITransitionMode::None, {}),
				}),
		};
		sliderInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<SliderComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("value",        Json::Value(c.Value));
			v.AddMember("direction",    Json::Value(static_cast<int>(c.Direction)));
			v.AddMember("min",          Json::Value(c.MinValue));
			v.AddMember("max",          Json::Value(c.MaxValue));
			v.AddMember("wholeNumbers", Json::Value(c.WholeNumbers));
			v.AddMember("isReadOnly",   Json::Value(c.IsReadOnly));
			v.AddMember("backgroundEntity", UIEntityHandleToJson(e, c.BackgroundEntity));
			v.AddMember("fillEntity",       UIEntityHandleToJson(e, c.FillEntity));
			v.AddMember("handleEntity",     UIEntityHandleToJson(e, c.HandleEntity));
			v.AddMember("labelEntity",      UIEntityHandleToJson(e, c.LabelEntity));
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
			v.AddMember("onValueChanged", UIEventListToJson(c.OnValueChanged));
			return v;
		};
		sliderInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<SliderComponent>();
			if (const Json::Value* m = v.FindMember("value"))        c.Value        = static_cast<float>(m->AsDoubleOr(c.Value));
			if (const Json::Value* m = v.FindMember("direction"))    c.Direction    = static_cast<SliderDirection>(m->AsIntOr(static_cast<int>(c.Direction)));
			if (const Json::Value* m = v.FindMember("min"))          c.MinValue     = static_cast<float>(m->AsDoubleOr(c.MinValue));
			if (const Json::Value* m = v.FindMember("max"))          c.MaxValue     = static_cast<float>(m->AsDoubleOr(c.MaxValue));
			if (const Json::Value* m = v.FindMember("wholeNumbers")) c.WholeNumbers = m->AsBoolOr(c.WholeNumbers);
			if (const Json::Value* m = v.FindMember("isReadOnly"))   c.IsReadOnly   = m->AsBoolOr(c.IsReadOnly);
			if (const Json::Value* m = v.FindMember("backgroundEntity")) UIEntityHandleFromJson(e, *m, c.BackgroundEntity);
			if (const Json::Value* m = v.FindMember("fillEntity"))       UIEntityHandleFromJson(e, *m, c.FillEntity);
			if (const Json::Value* m = v.FindMember("handleEntity"))     UIEntityHandleFromJson(e, *m, c.HandleEntity);
			if (const Json::Value* m = v.FindMember("labelEntity"))      UIEntityHandleFromJson(e, *m, c.LabelEntity);
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
			if (const Json::Value* m = v.FindMember("onValueChanged")) UIEventListFromJson(e, *m, c.OnValueChanged);
		};
		sliderInfo.onAdd = &InheritImageColorIntoNormal<SliderComponent>;
		registry.Register<SliderComponent>(sliderInfo);

		ComponentInfo inputFieldInfo{ "Input Field", "UI", ComponentCategory::Component };
		inputFieldInfo.serializedName = "InputField";
		PropertyMetadata caretBlinkMeta = Properties::Meta::Clamp(0.0, 5.0, 0.05f).WithHeader("Caret & Colors");
		PropertyMetadata caretWidthMeta = Properties::Meta::Clamp(1.0, 5.0, 0.05f);
		PropertyMetadata multilineMeta = Properties::Meta::EnabledIf<InputFieldComponent>([](const InputFieldComponent& f) { return f.Multiline; });
		inputFieldInfo.properties = {
			Properties::Make("Text",            "Text",            &InputFieldComponent::Text),
			Properties::Make("PlaceholderText", "Placeholder",     &InputFieldComponent::PlaceholderText),
			MakeUiEntityRef<InputFieldComponent>("TextEntity", "Text Entity", &InputFieldComponent::TextEntity),
			Properties::Make("CharacterLimit",  "Character Limit", &InputFieldComponent::CharacterLimit),
			Properties::Make("ContentType",     "Content Type",    &InputFieldComponent::ContentType),
			Properties::Make("IsSecret",        "Secret",          &InputFieldComponent::IsSecret),
			Properties::Make("IsReadOnly",      "Read Only",       &InputFieldComponent::IsReadOnly),
			Properties::Make("Multiline",       "Multiline",       &InputFieldComponent::Multiline),
			MakeUiEntityRef<InputFieldComponent>("VerticalScrollbarEntity", "Vertical Scrollbar", &InputFieldComponent::VerticalScrollbarEntity),
			Properties::Make("VerticalScrollbarVisibility", "Scrollbar Visibility", &InputFieldComponent::VerticalScrollbarVisibility, multilineMeta),
			Properties::Make("ScrollSensitivity", "Scroll Sensitivity", &InputFieldComponent::ScrollSensitivity, multilineMeta),
			Properties::Make("LineLimit",       "Line Limit",      &InputFieldComponent::LineLimit, multilineMeta),
			Properties::MakeVariant("TransitionMode", "Transition Mode",
				&InputFieldComponent::TransitionMode,
				{
					Properties::Branch(UITransitionMode::ColorSwap, {
						Properties::Make("NormalColor",   "Normal Color",   &InputFieldComponent::NormalColor),
						Properties::Make("HoveredColor",  "Hovered Color",  &InputFieldComponent::HoveredColor),
						Properties::Make("PressedColor",  "Pressed Color",  &InputFieldComponent::PressedColor),
						Properties::Make("DisabledColor", "Disabled Color", &InputFieldComponent::DisabledColor),
						Properties::Make("FocusedColor",  "Focused Color",  &InputFieldComponent::FocusedColor),
					}),
					Properties::Branch(UITransitionMode::SpriteSwap, {
						MakeUiSpriteRef<InputFieldComponent>("NormalSprite",   "Normal Sprite",   &InputFieldComponent::NormalSprite),
						MakeUiSpriteRef<InputFieldComponent>("HoveredSprite",  "Hovered Sprite",  &InputFieldComponent::HoveredSprite),
						MakeUiSpriteRef<InputFieldComponent>("PressedSprite",  "Pressed Sprite",  &InputFieldComponent::PressedSprite),
						MakeUiSpriteRef<InputFieldComponent>("DisabledSprite", "Disabled Sprite", &InputFieldComponent::DisabledSprite),
						MakeUiSpriteRef<InputFieldComponent>("FocusedSprite",  "Focused Sprite",  &InputFieldComponent::FocusedSprite),
					}),
					Properties::Branch(UITransitionMode::None, {}),
				}),
			// "Caret & Colors" collapsible section: the WithHeader on CaretBlinkRate
			// opens a CollapsingHeader and the fields below render inside it. Kept
			// last so the section runs to the end of the list — nothing following
			// gets swept into the group.
			Properties::Make("CaretBlinkRate",  "Caret Blink Rate (Hz)", &InputFieldComponent::CaretBlinkRate, caretBlinkMeta),
			Properties::Make("CaretWidth",      "Caret Width",     &InputFieldComponent::CaretWidth, caretWidthMeta),
			Properties::Make("TextColor",       "Text Color",      &InputFieldComponent::TextColor),
			Properties::Make("PlaceholderColor","Placeholder Color", &InputFieldComponent::PlaceholderColor),
			Properties::Make("CaretColor",      "Caret Color",     &InputFieldComponent::CaretColor),
			Properties::Make("SelectionColor",  "Selection Color", &InputFieldComponent::SelectionColor),
		};
		inputFieldInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<InputFieldComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("text",             Json::Value(c.Text));
			v.AddMember("placeholder",      Json::Value(c.PlaceholderText));
			v.AddMember("textEntity",       UIEntityHandleToJson(e, c.TextEntity));
			v.AddMember("characterLimit",   Json::Value(c.CharacterLimit));
			v.AddMember("lineLimit",        Json::Value(c.LineLimit));
			v.AddMember("contentType",      Json::Value(static_cast<int>(c.ContentType)));
			v.AddMember("isSecret",         Json::Value(c.IsSecret));
			v.AddMember("isReadOnly",       Json::Value(c.IsReadOnly));
			v.AddMember("multiline",        Json::Value(c.Multiline));
			v.AddMember("verticalScrollbarEntity", UIEntityHandleToJson(e, c.VerticalScrollbarEntity));
			v.AddMember("verticalScrollbarVisibility", Json::Value(static_cast<int>(c.VerticalScrollbarVisibility)));
			v.AddMember("scrollSensitivity", Json::Value(c.ScrollSensitivity));
			v.AddMember("caretBlinkRate",   Json::Value(c.CaretBlinkRate));
			v.AddMember("caretWidth",       Json::Value(c.CaretWidth));
			v.AddMember("textColor",        UIColorToJson(c.TextColor));
			v.AddMember("placeholderColor", UIColorToJson(c.PlaceholderColor));
			v.AddMember("caretColor",       UIColorToJson(c.CaretColor));
			v.AddMember("selectionColor",   UIColorToJson(c.SelectionColor));
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
			v.AddMember("onValueChanged", UIEventListToJson(c.OnValueChanged));
			v.AddMember("onSubmitted",    UIEventListToJson(c.OnSubmitted));
			return v;
		};
		inputFieldInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<InputFieldComponent>();
			if (const Json::Value* m = v.FindMember("text"))             c.Text             = m->AsStringOr(c.Text);
			if (const Json::Value* m = v.FindMember("placeholder"))      c.PlaceholderText  = m->AsStringOr(c.PlaceholderText);
			if (const Json::Value* m = v.FindMember("textEntity"))       UIEntityHandleFromJson(e, *m, c.TextEntity);
			if (const Json::Value* m = v.FindMember("characterLimit"))   c.CharacterLimit   = m->AsIntOr(c.CharacterLimit);
			if (const Json::Value* m = v.FindMember("lineLimit"))        c.LineLimit        = m->AsIntOr(c.LineLimit);
			if (const Json::Value* m = v.FindMember("contentType"))      c.ContentType      = static_cast<InputContentType>(m->AsIntOr(static_cast<int>(c.ContentType)));
			if (const Json::Value* m = v.FindMember("isSecret"))         c.IsSecret         = m->AsBoolOr(c.IsSecret);
			if (const Json::Value* m = v.FindMember("isReadOnly"))       c.IsReadOnly       = m->AsBoolOr(c.IsReadOnly);
			if (const Json::Value* m = v.FindMember("multiline"))        c.Multiline        = m->AsBoolOr(c.Multiline);
			if (const Json::Value* m = v.FindMember("verticalScrollbarEntity")) UIEntityHandleFromJson(e, *m, c.VerticalScrollbarEntity);
			if (const Json::Value* m = v.FindMember("verticalScrollbarVisibility")) c.VerticalScrollbarVisibility = static_cast<ScrollbarVisibility>(m->AsIntOr(static_cast<int>(c.VerticalScrollbarVisibility)));
			if (const Json::Value* m = v.FindMember("scrollSensitivity")) c.ScrollSensitivity = static_cast<float>(m->AsDoubleOr(c.ScrollSensitivity));
			if (const Json::Value* m = v.FindMember("caretBlinkRate"))   c.CaretBlinkRate   = static_cast<float>(m->AsDoubleOr(c.CaretBlinkRate));
			if (const Json::Value* m = v.FindMember("caretWidth"))       c.CaretWidth       = static_cast<float>(m->AsDoubleOr(c.CaretWidth));
			if (const Json::Value* m = v.FindMember("textColor"))        c.TextColor        = UIColorFromJson(*m, c.TextColor);
			if (const Json::Value* m = v.FindMember("placeholderColor")) c.PlaceholderColor = UIColorFromJson(*m, c.PlaceholderColor);
			if (const Json::Value* m = v.FindMember("caretColor"))       c.CaretColor       = UIColorFromJson(*m, c.CaretColor);
			if (const Json::Value* m = v.FindMember("selectionColor"))   c.SelectionColor   = UIColorFromJson(*m, c.SelectionColor);
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
			if (const Json::Value* m = v.FindMember("onValueChanged")) UIEventListFromJson(e, *m, c.OnValueChanged);
			if (const Json::Value* m = v.FindMember("onSubmitted"))    UIEventListFromJson(e, *m, c.OnSubmitted);
		};
		inputFieldInfo.onAdd = &InheritImageColorIntoNormal<InputFieldComponent>;
		registry.Register<InputFieldComponent>(inputFieldInfo);

		ComponentInfo dropdownInfo{ "Dropdown", "UI", ComponentCategory::Component };
		dropdownInfo.serializedName = "Dropdown";
		dropdownInfo.properties = {
			Properties::Make("SelectedIndex", "Selected Index", &DropdownComponent::SelectedIndex),
			// Real list-of-strings editor — each entry gets its own
			// row with a delete button + an "Add" button. Replaces
			// the older "join on newlines" hack.
			Properties::MakeStringList("Options", "Options",
				[](const Entity& e) -> std::vector<std::string> {
					return e.GetComponent<DropdownComponent>().Options;
				},
				[](Entity& e, const std::vector<std::string>& items) {
					auto& dd = e.GetComponent<DropdownComponent>();
					dd.Options = items;
					// Keep SelectedIndex inside the new range so the
					// closed header doesn't display a stale row, and
					// don't crash the rendering path on empty lists.
					if (dd.Options.empty()) {
						dd.SelectedIndex = 0;
					}
					else {
						if (dd.SelectedIndex < 0) dd.SelectedIndex = 0;
						if (dd.SelectedIndex >= static_cast<int>(dd.Options.size())) {
							dd.SelectedIndex = static_cast<int>(dd.Options.size()) - 1;
						}
					}
				}),
			Properties::Make("IsReadOnly",      "Read Only",      &DropdownComponent::IsReadOnly),
			MakeUiEntityRef<DropdownComponent>("LabelEntity", "Label Entity", &DropdownComponent::LabelEntity),
			Properties::Make("OptionRowHeight", "Row Height", &DropdownComponent::OptionRowHeight),
			Properties::Make("PopupBackgroundColor", "Popup Background", &DropdownComponent::PopupBackgroundColor),
			Properties::Make("OptionTextColor",      "Option Text Color", &DropdownComponent::OptionTextColor),
			Properties::Make("OptionNormalColor",    "Option Normal Color (override)",  &DropdownComponent::OptionNormalColor),
			Properties::Make("OptionHoverColor",     "Option Hover Color",   &DropdownComponent::OptionHoverColor),
			Properties::Make("OptionPressedColor",   "Option Pressed Color (override)", &DropdownComponent::OptionPressedColor),
			Properties::Make("OptionSelectedColor",  "Option Selected Color (override)",&DropdownComponent::OptionSelectedColor),
			Properties::MakeVariant("TransitionMode", "Transition Mode",
				&DropdownComponent::TransitionMode,
				{
					Properties::Branch(UITransitionMode::ColorSwap, {
						Properties::Make("NormalColor",   "Normal Color",   &DropdownComponent::NormalColor),
						Properties::Make("HoveredColor",  "Hovered Color",  &DropdownComponent::HoveredColor),
						Properties::Make("PressedColor",  "Pressed Color",  &DropdownComponent::PressedColor),
						Properties::Make("DisabledColor", "Disabled Color", &DropdownComponent::DisabledColor),
						Properties::Make("FocusedColor",  "Focused Color",  &DropdownComponent::FocusedColor),
					}),
					Properties::Branch(UITransitionMode::SpriteSwap, {
						MakeUiSpriteRef<DropdownComponent>("NormalSprite",   "Normal Sprite",   &DropdownComponent::NormalSprite),
						MakeUiSpriteRef<DropdownComponent>("HoveredSprite",  "Hovered Sprite",  &DropdownComponent::HoveredSprite),
						MakeUiSpriteRef<DropdownComponent>("PressedSprite",  "Pressed Sprite",  &DropdownComponent::PressedSprite),
						MakeUiSpriteRef<DropdownComponent>("DisabledSprite", "Disabled Sprite", &DropdownComponent::DisabledSprite),
						MakeUiSpriteRef<DropdownComponent>("FocusedSprite",  "Focused Sprite",  &DropdownComponent::FocusedSprite),
					}),
					Properties::Branch(UITransitionMode::None, {}),
				}),
		};
		dropdownInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<DropdownComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("selectedIndex",   Json::Value(c.SelectedIndex));
			v.AddMember("isReadOnly",      Json::Value(c.IsReadOnly));
			v.AddMember("labelEntity",     UIEntityHandleToJson(e, c.LabelEntity));
			v.AddMember("rowHeight",       Json::Value(c.OptionRowHeight));
			v.AddMember("popupBackground", UIColorToJson(c.PopupBackgroundColor));
			v.AddMember("optionTextColor", UIColorToJson(c.OptionTextColor));
			v.AddMember("optionNormalColor",   UIColorToJson(c.OptionNormalColor));
			v.AddMember("optionHoverColor",    UIColorToJson(c.OptionHoverColor));
			v.AddMember("optionPressedColor",  UIColorToJson(c.OptionPressedColor));
			v.AddMember("optionSelectedColor", UIColorToJson(c.OptionSelectedColor));
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
			Json::Value optionsArr = Json::Value::MakeArray();
			for (const std::string& opt : c.Options) {
				optionsArr.Append(Json::Value(opt));
			}
			v.AddMember("options", std::move(optionsArr));
			v.AddMember("onValueChanged", UIEventListToJson(c.OnValueChanged));
			return v;
		};
		dropdownInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<DropdownComponent>();
			if (const Json::Value* m = v.FindMember("selectedIndex"))   c.SelectedIndex        = m->AsIntOr(c.SelectedIndex);
			if (const Json::Value* m = v.FindMember("isReadOnly"))      c.IsReadOnly           = m->AsBoolOr(c.IsReadOnly);
			if (const Json::Value* m = v.FindMember("labelEntity"))     UIEntityHandleFromJson(e, *m, c.LabelEntity);
			if (const Json::Value* m = v.FindMember("rowHeight"))       c.OptionRowHeight      = static_cast<float>(m->AsDoubleOr(c.OptionRowHeight));
			if (const Json::Value* m = v.FindMember("popupBackground")) c.PopupBackgroundColor = UIColorFromJson(*m, c.PopupBackgroundColor);
			if (const Json::Value* m = v.FindMember("optionTextColor")) c.OptionTextColor      = UIColorFromJson(*m, c.OptionTextColor);
			if (const Json::Value* m = v.FindMember("optionNormalColor"))   c.OptionNormalColor   = UIColorFromJson(*m, c.OptionNormalColor);
			if (const Json::Value* m = v.FindMember("optionHoverColor"))    c.OptionHoverColor    = UIColorFromJson(*m, c.OptionHoverColor);
			if (const Json::Value* m = v.FindMember("optionPressedColor"))  c.OptionPressedColor  = UIColorFromJson(*m, c.OptionPressedColor);
			if (const Json::Value* m = v.FindMember("optionSelectedColor"))c.OptionSelectedColor  = UIColorFromJson(*m, c.OptionSelectedColor);
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
			if (const Json::Value* m = v.FindMember("options"); m && m->IsArray()) {
				c.Options.clear();
				for (const Json::Value& item : m->GetArray()) {
					c.Options.push_back(item.AsStringOr(""));
				}
			}
			if (const Json::Value* m = v.FindMember("onValueChanged")) UIEventListFromJson(e, *m, c.OnValueChanged);
		};
		dropdownInfo.onAdd = &InheritImageColorIntoNormal<DropdownComponent>;
		registry.Register<DropdownComponent>(dropdownInfo);

		ComponentInfo toggleInfo{ "Toggle", "UI", ComponentCategory::Component };
		toggleInfo.serializedName = "Toggle";
		toggleInfo.properties = {
			Properties::Make("IsOn",       "Is On",     &ToggleComponent::IsOn),
			Properties::Make("IsReadOnly", "Read Only", &ToggleComponent::IsReadOnly),
			MakeUiEntityRef<ToggleComponent>("CheckmarkEntity", "Graphic Entity", &ToggleComponent::CheckmarkEntity),
			Properties::MakeVariant("TransitionMode", "Transition Mode",
				&ToggleComponent::TransitionMode,
				{
					Properties::Branch(UITransitionMode::ColorSwap, {
						Properties::Make("NormalColor",   "Normal Color",   &ToggleComponent::NormalColor),
						Properties::Make("HoveredColor",  "Hovered Color",  &ToggleComponent::HoveredColor),
						Properties::Make("PressedColor",  "Pressed Color",  &ToggleComponent::PressedColor),
						Properties::Make("DisabledColor", "Disabled Color", &ToggleComponent::DisabledColor),
						Properties::Make("FocusedColor",  "Focused Color",  &ToggleComponent::FocusedColor),
					}),
					Properties::Branch(UITransitionMode::SpriteSwap, {
						MakeUiSpriteRef<ToggleComponent>("NormalSprite",   "Normal Sprite",   &ToggleComponent::NormalSprite),
						MakeUiSpriteRef<ToggleComponent>("HoveredSprite",  "Hovered Sprite",  &ToggleComponent::HoveredSprite),
						MakeUiSpriteRef<ToggleComponent>("PressedSprite",  "Pressed Sprite",  &ToggleComponent::PressedSprite),
						MakeUiSpriteRef<ToggleComponent>("DisabledSprite", "Disabled Sprite", &ToggleComponent::DisabledSprite),
						MakeUiSpriteRef<ToggleComponent>("FocusedSprite",  "Focused Sprite",  &ToggleComponent::FocusedSprite),
					}),
					Properties::Branch(UITransitionMode::None, {}),
				}),
		};
		toggleInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<ToggleComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("isOn",        Json::Value(c.IsOn));
			v.AddMember("isReadOnly",  Json::Value(c.IsReadOnly));
			v.AddMember("checkmarkEntity", UIEntityHandleToJson(e, c.CheckmarkEntity));
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
			v.AddMember("onValueChanged", UIEventListToJson(c.OnValueChanged));
			return v;
		};
		toggleInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<ToggleComponent>();
			if (const Json::Value* m = v.FindMember("isOn"))       c.IsOn       = m->AsBoolOr(c.IsOn);
			if (const Json::Value* m = v.FindMember("isReadOnly")) c.IsReadOnly = m->AsBoolOr(c.IsReadOnly);
			if (const Json::Value* m = v.FindMember("checkmarkEntity")) UIEntityHandleFromJson(e, *m, c.CheckmarkEntity);
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
			if (const Json::Value* m = v.FindMember("onValueChanged")) UIEventListFromJson(e, *m, c.OnValueChanged);
		};
		toggleInfo.onAdd = &InheritImageColorIntoNormal<ToggleComponent>;
		registry.Register<ToggleComponent>(toggleInfo);

		ComponentInfo scrollbarInfo{ "Scrollbar", "UI", ComponentCategory::Component };
		scrollbarInfo.serializedName = "Scrollbar";
		PropertyMetadata scrollbarValueMeta = Properties::Meta::Clamp(0.0, 1.0, 0.01f);
		PropertyMetadata scrollbarSizeMeta  = Properties::Meta::Clamp(0.0, 1.0, 0.01f);
		PropertyMetadata scrollbarStepsMeta = Properties::Meta::Clamp(0.0, 11.0);
		scrollbarInfo.properties = {
			MakeUiEntityRef<ScrollbarComponent>("HandleEntity", "Handle Rect", &ScrollbarComponent::HandleEntity),
			Properties::Make("Direction",     "Direction",      &ScrollbarComponent::Direction),
			Properties::Make("Value",         "Value",          &ScrollbarComponent::Value, scrollbarValueMeta),
			Properties::Make("Size",          "Size",           &ScrollbarComponent::Size, scrollbarSizeMeta),
			Properties::Make("NumberOfSteps", "Number Of Steps",&ScrollbarComponent::NumberOfSteps, scrollbarStepsMeta),
			Properties::Make("IsReadOnly",    "Read Only",      &ScrollbarComponent::IsReadOnly),
			Properties::MakeVariant("TransitionMode", "Transition Mode",
				&ScrollbarComponent::TransitionMode,
				{
					Properties::Branch(UITransitionMode::ColorSwap, {
						Properties::Make("NormalColor",   "Normal Color",   &ScrollbarComponent::NormalColor),
						Properties::Make("HoveredColor",  "Hovered Color",  &ScrollbarComponent::HoveredColor),
						Properties::Make("PressedColor",  "Pressed Color",  &ScrollbarComponent::PressedColor),
						Properties::Make("DisabledColor", "Disabled Color", &ScrollbarComponent::DisabledColor),
						Properties::Make("FocusedColor",  "Focused Color",  &ScrollbarComponent::FocusedColor),
					}),
					Properties::Branch(UITransitionMode::SpriteSwap, {
						MakeUiSpriteRef<ScrollbarComponent>("NormalSprite",   "Normal Sprite",   &ScrollbarComponent::NormalSprite),
						MakeUiSpriteRef<ScrollbarComponent>("HoveredSprite",  "Hovered Sprite",  &ScrollbarComponent::HoveredSprite),
						MakeUiSpriteRef<ScrollbarComponent>("PressedSprite",  "Pressed Sprite",  &ScrollbarComponent::PressedSprite),
						MakeUiSpriteRef<ScrollbarComponent>("DisabledSprite", "Disabled Sprite", &ScrollbarComponent::DisabledSprite),
						MakeUiSpriteRef<ScrollbarComponent>("FocusedSprite",  "Focused Sprite",  &ScrollbarComponent::FocusedSprite),
					}),
					Properties::Branch(UITransitionMode::None, {}),
				}),
		};
		scrollbarInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<ScrollbarComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("handleEntity", UIEntityHandleToJson(e, c.HandleEntity));
			v.AddMember("direction",      Json::Value(static_cast<int>(c.Direction)));
			v.AddMember("value",          Json::Value(c.Value));
			v.AddMember("size",           Json::Value(c.Size));
			v.AddMember("numberOfSteps",  Json::Value(c.NumberOfSteps));
			v.AddMember("isReadOnly",     Json::Value(c.IsReadOnly));
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
		scrollbarInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<ScrollbarComponent>();
			if (const Json::Value* m = v.FindMember("handleEntity")) UIEntityHandleFromJson(e, *m, c.HandleEntity);
			if (const Json::Value* m = v.FindMember("direction"))     c.Direction     = static_cast<ScrollbarDirection>(m->AsIntOr(static_cast<int>(c.Direction)));
			if (const Json::Value* m = v.FindMember("value"))         c.Value         = static_cast<float>(m->AsDoubleOr(c.Value));
			if (const Json::Value* m = v.FindMember("size"))          c.Size          = static_cast<float>(m->AsDoubleOr(c.Size));
			if (const Json::Value* m = v.FindMember("numberOfSteps")) c.NumberOfSteps = m->AsIntOr(c.NumberOfSteps);
			if (const Json::Value* m = v.FindMember("isReadOnly"))    c.IsReadOnly    = m->AsBoolOr(c.IsReadOnly);
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
		scrollbarInfo.onAdd = &InheritImageColorIntoNormal<ScrollbarComponent>;
		registry.Register<ScrollbarComponent>(scrollbarInfo);

		ComponentInfo scrollRectInfo{ "Scroll Rect", "UI", ComponentCategory::Component };
		scrollRectInfo.serializedName = "ScrollRect";
		PropertyMetadata scrollRectElasticityMeta   = Properties::Meta::Clamp(0.0, 1.0, 0.01f);
		PropertyMetadata scrollRectDecelerationMeta = Properties::Meta::Clamp(0.0, 1.0, 0.005f);
		scrollRectInfo.properties = {
			MakeUiEntityRef<ScrollRectComponent>("Content",  "Content",  &ScrollRectComponent::Content),
			Properties::Make("Horizontal",        "Horizontal",         &ScrollRectComponent::Horizontal),
			Properties::Make("Vertical",          "Vertical",           &ScrollRectComponent::Vertical),
			Properties::Make("MovementType",      "Movement Type",      &ScrollRectComponent::MovementType),
			Properties::Make("Elasticity",        "Elasticity",         &ScrollRectComponent::Elasticity, scrollRectElasticityMeta),
			Properties::Make("Inertia",           "Inertia",            &ScrollRectComponent::Inertia),
			Properties::Make("DecelerationRate",  "Deceleration Rate",  &ScrollRectComponent::DecelerationRate, scrollRectDecelerationMeta),
			Properties::Make("ScrollSensitivity", "Scroll Sensitivity", &ScrollRectComponent::ScrollSensitivity),
			MakeUiEntityRef<ScrollRectComponent>("Viewport", "Viewport", &ScrollRectComponent::Viewport),
			MakeUiEntityRef<ScrollRectComponent>("HorizontalScrollbar", "Horizontal Scrollbar", &ScrollRectComponent::HorizontalScrollbar),
			Properties::Make("HorizontalScrollbarVisibility", "Horizontal Visibility", &ScrollRectComponent::HorizontalScrollbarVisibility),
			Properties::Make("HorizontalScrollbarSpacing",    "Horizontal Spacing",    &ScrollRectComponent::HorizontalScrollbarSpacing),
			MakeUiEntityRef<ScrollRectComponent>("VerticalScrollbar",   "Vertical Scrollbar",   &ScrollRectComponent::VerticalScrollbar),
			Properties::Make("VerticalScrollbarVisibility",   "Vertical Visibility",   &ScrollRectComponent::VerticalScrollbarVisibility),
			Properties::Make("VerticalScrollbarSpacing",      "Vertical Spacing",      &ScrollRectComponent::VerticalScrollbarSpacing),
		};
		scrollRectInfo.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<ScrollRectComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("content",            UIEntityHandleToJson(e, c.Content));
			v.AddMember("viewport",           UIEntityHandleToJson(e, c.Viewport));
			v.AddMember("horizontal",         Json::Value(c.Horizontal));
			v.AddMember("vertical",           Json::Value(c.Vertical));
			v.AddMember("movementType",       Json::Value(static_cast<int>(c.MovementType)));
			v.AddMember("elasticity",         Json::Value(c.Elasticity));
			v.AddMember("inertia",            Json::Value(c.Inertia));
			v.AddMember("decelerationRate",   Json::Value(c.DecelerationRate));
			v.AddMember("scrollSensitivity",  Json::Value(c.ScrollSensitivity));
			v.AddMember("horizontalScrollbar", UIEntityHandleToJson(e, c.HorizontalScrollbar));
			v.AddMember("verticalScrollbar",   UIEntityHandleToJson(e, c.VerticalScrollbar));
			v.AddMember("horizontalScrollbarVisibility", Json::Value(static_cast<int>(c.HorizontalScrollbarVisibility)));
			v.AddMember("verticalScrollbarVisibility",   Json::Value(static_cast<int>(c.VerticalScrollbarVisibility)));
			v.AddMember("horizontalScrollbarSpacing",    Json::Value(c.HorizontalScrollbarSpacing));
			v.AddMember("verticalScrollbarSpacing",      Json::Value(c.VerticalScrollbarSpacing));
			Json::Value np = Json::Value::MakeObject();
			np.AddMember("x", Json::Value(c.NormalizedPosition.x));
			np.AddMember("y", Json::Value(c.NormalizedPosition.y));
			v.AddMember("normalizedPosition", std::move(np));
			return v;
		};
		scrollRectInfo.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<ScrollRectComponent>();
			if (const Json::Value* m = v.FindMember("content"))  UIEntityHandleFromJson(e, *m, c.Content);
			if (const Json::Value* m = v.FindMember("viewport")) UIEntityHandleFromJson(e, *m, c.Viewport);
			if (const Json::Value* m = v.FindMember("horizontal"))         c.Horizontal         = m->AsBoolOr(c.Horizontal);
			if (const Json::Value* m = v.FindMember("vertical"))           c.Vertical           = m->AsBoolOr(c.Vertical);
			if (const Json::Value* m = v.FindMember("movementType"))       c.MovementType       = static_cast<ScrollRectMovementType>(m->AsIntOr(static_cast<int>(c.MovementType)));
			if (const Json::Value* m = v.FindMember("elasticity"))         c.Elasticity         = static_cast<float>(m->AsDoubleOr(c.Elasticity));
			if (const Json::Value* m = v.FindMember("inertia"))            c.Inertia            = m->AsBoolOr(c.Inertia);
			if (const Json::Value* m = v.FindMember("decelerationRate"))   c.DecelerationRate   = static_cast<float>(m->AsDoubleOr(c.DecelerationRate));
			if (const Json::Value* m = v.FindMember("scrollSensitivity"))  c.ScrollSensitivity  = static_cast<float>(m->AsDoubleOr(c.ScrollSensitivity));
			if (const Json::Value* m = v.FindMember("horizontalScrollbar")) UIEntityHandleFromJson(e, *m, c.HorizontalScrollbar);
			if (const Json::Value* m = v.FindMember("verticalScrollbar"))   UIEntityHandleFromJson(e, *m, c.VerticalScrollbar);
			if (const Json::Value* m = v.FindMember("horizontalScrollbarVisibility")) c.HorizontalScrollbarVisibility = static_cast<ScrollbarVisibility>(m->AsIntOr(static_cast<int>(c.HorizontalScrollbarVisibility)));
			if (const Json::Value* m = v.FindMember("verticalScrollbarVisibility"))   c.VerticalScrollbarVisibility   = static_cast<ScrollbarVisibility>(m->AsIntOr(static_cast<int>(c.VerticalScrollbarVisibility)));
			if (const Json::Value* m = v.FindMember("horizontalScrollbarSpacing"))    c.HorizontalScrollbarSpacing    = static_cast<float>(m->AsDoubleOr(c.HorizontalScrollbarSpacing));
			if (const Json::Value* m = v.FindMember("verticalScrollbarSpacing"))      c.VerticalScrollbarSpacing      = static_cast<float>(m->AsDoubleOr(c.VerticalScrollbarSpacing));
			if (const Json::Value* m = v.FindMember("normalizedPosition"); m && m->IsObject()) {
				if (const Json::Value* x = m->FindMember("x")) c.NormalizedPosition.x = static_cast<float>(x->AsDoubleOr(c.NormalizedPosition.x));
				if (const Json::Value* y = m->FindMember("y")) c.NormalizedPosition.y = static_cast<float>(y->AsDoubleOr(c.NormalizedPosition.y));
			}
		};
		registry.Register<ScrollRectComponent>(scrollRectInfo);
	}
}
