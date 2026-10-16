#include "pch.hpp"
#include "Scripting/ScriptEditorGui.hpp"

#include "Scripting/ScriptBindingGuard.hpp"
#include "Scripting/ScriptGlue.hpp"

#include <atomic>

namespace Funke {

	namespace {
		std::atomic<const EditorGuiBackend*> s_Backend{ nullptr };

		const EditorGuiBackend* Backend() { return s_Backend.load(std::memory_order_acquire); }

		void Funke_EditorGUI_Text(const char* text, int style) {
			if (const auto* b = Backend(); b && b->Text && text) b->Text(text, style);
		}
		int Funke_EditorGUI_Button(const char* label, float width, float height) {
			const auto* b = Backend();
			return b && b->Button && label ? b->Button(label, width, height) : 0;
		}
		int Funke_EditorGUI_Toggle(const char* label, int value) {
			const auto* b = Backend();
			return b && b->Toggle && label ? b->Toggle(label, value) : value;
		}
		float Funke_EditorGUI_Slider(const char* label, float value, float min, float max, const char* format) {
			const auto* b = Backend();
			return b && b->Slider && label ? b->Slider(label, value, min, max, format) : value;
		}
		float Funke_EditorGUI_VerticalSlider(const char* label, float width, float height, float value, float min, float max, const char* format) {
			const auto* b = Backend();
			return b && b->VerticalSlider && label ? b->VerticalSlider(label, width, height, value, min, max, format) : value;
		}
		float Funke_EditorGUI_FloatField(const char* label, float value, float speed) {
			const auto* b = Backend();
			return b && b->FloatField && label ? b->FloatField(label, value, speed) : value;
		}
		int Funke_EditorGUI_TextField(const char* label, char* buffer, int capacity) {
			const auto* b = Backend();
			return b && b->TextField && label && buffer && capacity > 0 ? b->TextField(label, buffer, capacity) : 0;
		}
		void Funke_EditorGUI_ProgressBar(float fraction, float width, float height, const char* overlay) {
			if (const auto* b = Backend(); b && b->ProgressBar) b->ProgressBar(fraction, width, height, overlay);
		}
		void Funke_EditorGUI_Layout(int op, float value) {
			if (const auto* b = Backend(); b && b->Layout) b->Layout(op, value);
		}
		void Funke_EditorGUI_PushId(const char* id) {
			if (const auto* b = Backend(); b && b->PushId && id) b->PushId(id);
		}
		float Funke_EditorGUI_Query(int what) {
			const auto* b = Backend();
			return b && b->Query ? b->Query(what) : 0.0f;
		}
	}

	void ScriptEditorGui::SetBackend(const EditorGuiBackend* backend) {
		s_Backend.store(backend, std::memory_order_release);
	}

	const EditorGuiBackend* ScriptEditorGui::GetBackend() {
		return Backend();
	}

	void ScriptEditorGui::PopulateBindings(NativeBindings& b) {
		b.EditorGUI_Text = FNK_GUARDED_BINDING(Funke_EditorGUI_Text);
		b.EditorGUI_Button = FNK_GUARDED_BINDING(Funke_EditorGUI_Button);
		b.EditorGUI_Toggle = FNK_GUARDED_BINDING(Funke_EditorGUI_Toggle);
		b.EditorGUI_Slider = FNK_GUARDED_BINDING(Funke_EditorGUI_Slider);
		b.EditorGUI_VerticalSlider = FNK_GUARDED_BINDING(Funke_EditorGUI_VerticalSlider);
		b.EditorGUI_FloatField = FNK_GUARDED_BINDING(Funke_EditorGUI_FloatField);
		b.EditorGUI_TextField = FNK_GUARDED_BINDING(Funke_EditorGUI_TextField);
		b.EditorGUI_ProgressBar = FNK_GUARDED_BINDING(Funke_EditorGUI_ProgressBar);
		b.EditorGUI_Layout = FNK_GUARDED_BINDING(Funke_EditorGUI_Layout);
		b.EditorGUI_PushId = FNK_GUARDED_BINDING(Funke_EditorGUI_PushId);
		b.EditorGUI_Query = FNK_GUARDED_BINDING(Funke_EditorGUI_Query);
	}

}
