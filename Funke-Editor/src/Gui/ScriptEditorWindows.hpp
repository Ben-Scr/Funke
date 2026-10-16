#pragma once

#include "Editor/ScriptWindowTypes.hpp"

#include <string>
#include <vector>

namespace Funke {

	// Editor windows written in C# (Funke.Editor.EditorWindow subclasses in the game or a package): lists them for the
	// Tools menu and command palette, draws the open ones through ImGui and remembers them per project.
	class ScriptEditorWindows {
	public:
		using WindowType = ScriptWindowType;

		// Installs the ImGui backend for C# EditorGUI calls.
		void Initialize();
		void Shutdown();

		// Reads the project's open windows; call when a project is opened.
		void LoadProject(const std::string& projectRoot);

		// Re-reads the window classes at most once a second (after script reloads the list changes).
		void Refresh(bool force = false);
		const std::vector<WindowType>& GetTypes() const { return m_Types; }

		bool IsOpen(const std::string& type) const;
		void Open(const std::string& type);
		void Close(const std::string& type);

		// Menu items for every window class, nested by their menu path; nothing when there are none.
		void DrawMenuItems();
		void Render();

	private:
		std::string TitleOf(const std::string& type) const;
		void Save() const;

		std::vector<WindowType> m_Types;
		std::vector<std::string> m_Open;
		std::string m_ProjectRoot;
		double m_LastRefresh = -1.0;
		bool m_Initialized = false;
	};

	namespace ScriptEditorWindowsInternal {
		// Pops what an OnGUI left open (groups, IDs, indents) so an exception mid-draw cannot unbalance ImGui.
		// The backend counts every push.
		struct StackDepth {
			int Groups = 0;
			int Ids = 0;
			std::vector<float> Indents;
		};
		StackDepth& CurrentDepth();
		void UnwindTo(const StackDepth& target);
	}

}
