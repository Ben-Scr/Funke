#pragma once

#include <string>
#include <vector>

namespace Index {

	// A C# EditorWindow class as the managed side lists it.
	struct ScriptWindowType {
		std::string Type;
		std::string Title;
		// Tools-menu path, '/'-separated; the last part is the item label.
		std::string Menu;
		// Size on first open; 0 = the editor's default.
		float Width = 0.0f;
		float Height = 0.0f;
	};

	// Parses the managed GetEditorWindowTypes JSON; entries without a type are skipped, missing titles and menus are
	// filled in. Sorted by menu path.
	std::vector<ScriptWindowType> ParseScriptWindowTypes(const std::string& json);

	// "Audio//Audio Mixer/" → { "Audio", "Audio Mixer" }.
	std::vector<std::string> SplitMenuPath(const std::string& path);

}
