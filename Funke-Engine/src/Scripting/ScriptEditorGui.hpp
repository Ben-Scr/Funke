#pragma once
#include "Core/Export.hpp"

#include <cstdint>

namespace Funke {

	struct NativeBindings;

	// Values are mirrored by C# EditorGUI.
	enum class EditorGuiLayoutOp : int {
		Separator = 0,
		SameLine = 1,
		Spacing = 2,
		BeginGroup = 3,
		EndGroup = 4,
		Indent = 5,
		Unindent = 6,
		PopId = 7,
		BeginDisabled = 8,
		EndDisabled = 9,
	};

	enum class EditorGuiQuery : int {
		IsPlaying = 0,
		AvailableWidth = 1,
		LineHeight = 2,
	};

	// EditorGUI.Label styles.
	enum class EditorGuiTextStyle : int {
		Normal = 0,
		Disabled = 1,
		Heading = 2,
		Wrapped = 3,
	};

	// The editor's ImGui implementation of the C# EditorGUI calls. The engine DLL has no ImGui, so the editor installs
	// this at startup; without it (runtime, tools) every EditorGUI call is a no-op.
	struct EditorGuiBackend {
		void  (*Text)(const char* text, int style) = nullptr;
		int   (*Button)(const char* label, float width, float height) = nullptr;
		int   (*Toggle)(const char* label, int value) = nullptr;
		float (*Slider)(const char* label, float value, float min, float max, const char* format) = nullptr;
		float (*VerticalSlider)(const char* label, float width, float height, float value, float min, float max, const char* format) = nullptr;
		float (*FloatField)(const char* label, float value, float speed) = nullptr;
		int   (*TextField)(const char* label, char* buffer, int capacity) = nullptr;
		void  (*ProgressBar)(float fraction, float width, float height, const char* overlay) = nullptr;
		void  (*Layout)(int op, float value) = nullptr;
		void  (*PushId)(const char* id) = nullptr;
		float (*Query)(int what) = nullptr;
		int   (*Popup)(const char* label, int selectedIndex, const char* options, int optionsBytes) = nullptr;
		int   (*Selectable)(const char* label, int selected) = nullptr;
	};

	class FUNKE_API ScriptEditorGui {
	public:
		// The backend must outlive every later EditorGUI call; nullptr uninstalls it.
		static void SetBackend(const EditorGuiBackend* backend);
		static const EditorGuiBackend* GetBackend();

		static void PopulateBindings(NativeBindings& bindings);
	};

}
