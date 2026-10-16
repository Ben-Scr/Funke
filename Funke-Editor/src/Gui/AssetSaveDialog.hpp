#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace Funke {

	// OS "Save as" dialog for a new project asset. Non-blocking: the editor keeps drawing while it is open and the owner
	// polls once per frame for the answer.
	class AssetSaveDialog {
	public:
		// typeName labels the dialog and its filter ("Animation Clip"); extension includes the dot (".anim").
		void Open(const std::filesystem::path& suggestedPath, const std::string& typeName, const std::string& extension);
		bool IsOpen() const { return m_State != nullptr; }
		// Once the dialog closed: the chosen file with the extension enforced. Nullopt while open or after cancel; a path
		// outside Assets/ reopens the dialog there.
		std::optional<std::filesystem::path> Poll();

		// folder/stem.ext, or "stem 1.ext", "stem 2.ext" ... when taken.
		static std::filesystem::path UniquePath(const std::filesystem::path& folder, const std::string& stem, const std::string& extension);
		// The dialog's answer with the extension appended when missing (case-insensitive); numbered instead of
		// replacing an existing file, since the overwrite prompt only covered the name as typed.
		static std::filesystem::path WithExtension(const std::filesystem::path& picked, const std::string& extension);
		// True for paths strictly below folder; resolves "..", symlinks and casing of the parts that exist.
		static bool IsInside(const std::filesystem::path& path, const std::filesystem::path& folder);

	private:
		struct State {
			std::mutex Mutex;
			bool Done = false;
			std::optional<std::filesystem::path> Path;
		};

		void Show(const std::filesystem::path& suggestedPath, const std::string& title);

		std::shared_ptr<State> m_State;
		std::string m_TypeName;
		std::string m_Extension;
	};

}
