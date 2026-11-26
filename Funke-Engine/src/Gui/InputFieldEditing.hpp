#pragma once
#include "Core/Export.hpp"
#include "Components/UI/InputFieldComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include <entt/entity/fwd.hpp>
#include <string_view>
#include <utility>
#include <vector>

namespace Funke { class Input; class Font; }

// Stateless text editing and caret geometry. Focus, capture and dispatch belong to UIEventSystem.
namespace Funke::InputFieldEditing {
		struct InputTextLayout {
			bool Valid = false;
			float OriginX = 0.0f;
			float Scale = 1.0f;
			float LetterSpacing = 0.0f;
			bool Bold = false;
			float LineSpacing = 0.0f;
			Font* FontPtr = nullptr;
			TextAlignment Align = TextAlignment::Left;
			// Multiline geometry: line 0 is centred at CenterY; each subsequent line steps down by LineHeight.
			float CenterY = 0.0f;
			float LineHeight = 0.0f;
			// Right edge of the field's content area (rect right minus inset); with OriginX
			// it bounds the horizontal-scroll window for left-aligned text.
			float ContentRightX = 0.0f;
			TextWrapMode WrapMode = TextWrapMode::None;
			float WrapWidthPixels = 0.0f;
		};

	FUNKE_API int Utf8CodepointLength(std::string_view s, int idx);
	FUNKE_API bool Utf8Decode(std::string_view s, int idx, uint32_t& outCp, int& outLen);
	FUNKE_API int Utf8PrevByte(std::string_view s, int byteIdx);
	FUNKE_API int Utf8NextByte(std::string_view s, int byteIdx);
	FUNKE_API int Utf8CountCodepoints(std::string_view s);
	FUNKE_API std::string MaskTextForSecret(std::string_view src);
	FUNKE_API bool ContentTypeAllowsCodepoint(InputContentType type, std::uint32_t cp,
			std::string_view existing, int caretByte, bool allowNewline);
	FUNKE_API std::string FilterByContentType(InputContentType type, std::string_view src,
			std::string_view existing, int caretByte, bool allowNewline);
	FUNKE_API std::pair<int, int> SelectionRange(const InputFieldComponent& field);
	FUNKE_API bool HasSelection(const InputFieldComponent& field);
	FUNKE_API void ClampCaret(InputFieldComponent& field);
	FUNKE_API void SelectWordAt(InputFieldComponent& field, int byte);
	FUNKE_API void DeleteSelection(InputFieldComponent& field);
	FUNKE_API void InsertAtCaret(InputFieldComponent& field, std::string_view text);
	FUNKE_API void BackspaceOnce(InputFieldComponent& field);
	FUNKE_API void DeleteOnce(InputFieldComponent& field);
	FUNKE_API int LineIndexAtByte(std::string_view text, int byteIdx);
	FUNKE_API int LineCount(std::string_view text);
	FUNKE_API std::pair<int, int> LineRange(std::string_view text, int lineIndex);
	FUNKE_API float MeasureUpToByte(const Font& font, std::string_view text, int targetByte,
			float letterSpacing, bool bold);
	FUNKE_API int ByteFromRelativeX(const Font& font, std::string_view text,
			float relX, float scale, float letterSpacing, bool bold);
	FUNKE_API InputTextLayout ResolveInputTextLayout(entt::registry& registry,
			const InputFieldComponent& field);
	FUNKE_API std::vector<std::pair<int, int>> InputVisualLines(const InputTextLayout& layout, std::string_view text);
	FUNKE_API int VisualLineOfByte(const std::vector<std::pair<int, int>>& lines, int b);
	FUNKE_API float ComputeInputScrollX(entt::registry& registry, const InputFieldComponent& field);
	FUNKE_API void UpdateInputVerticalScroll(entt::registry& registry, const Input& input,
			InputFieldComponent& field, bool hovered,
			std::vector<std::pair<EntityHandle, bool>>& deferredEnable);
	FUNKE_API int ByteFromMouseX(entt::registry& registry, const InputFieldComponent& field, Vec2 mouseUi);
	FUNKE_API void ApplyInputFieldVisuals(entt::registry& registry, const InputFieldComponent& field);
}
