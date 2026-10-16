#include "pch.hpp"
#include "Gui/AssetSaveDialog.hpp"

#include "Core/Platform.hpp"
#include "Project/IndexProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/Path.hpp"

#include <algorithm>
#include <cctype>

namespace Index {
	namespace fs = std::filesystem;

	namespace {
		std::string ToLower(std::string text) {
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		fs::path Canonical(const fs::path& path) {
			std::error_code ec;
			fs::path result = fs::weakly_canonical(path, ec);
			return ec ? path.lexically_normal() : result;
		}
	}

	void AssetSaveDialog::Open(const fs::path& suggestedPath, const std::string& typeName, const std::string& extension) {
		if (IsOpen()) return;
		m_TypeName = typeName;
		m_Extension = ToLower(extension);
		Show(suggestedPath, "Save " + typeName);
	}

	void AssetSaveDialog::Show(const fs::path& suggestedPath, const std::string& title) {
		auto state = std::make_shared<State>();
		m_State = state;
		const std::string pattern = m_Extension.substr(m_Extension.starts_with('.') ? 1 : 0);
		Platform::SaveFileAsync([state](std::optional<fs::path> picked) {
			std::scoped_lock lock(state->Mutex);
			state->Path = std::move(picked);
			state->Done = true;
		}, suggestedPath, { { m_TypeName + " (*" + m_Extension + ")", pattern } }, title);
	}

	std::optional<fs::path> AssetSaveDialog::Poll() {
		if (!m_State) return std::nullopt;
		std::optional<fs::path> picked;
		{
			std::scoped_lock lock(m_State->Mutex);
			if (!m_State->Done) return std::nullopt;
			picked = std::move(m_State->Path);
		}
		m_State.reset();
		const IndexProject* project = ProjectManager::GetCurrentProject();
		if (!picked || !project) return std::nullopt;

		const fs::path path = WithExtension(*picked, m_Extension);
		// AssetRegistry only tracks files under Assets/; anything else would never get a GUID.
		const fs::path assets = Path::FromUtf8(project->AssetsDirectory);
		if (!IsInside(path, assets)) {
			IDX_WARN_TAG("Editor", "Assets must be saved inside the project's Assets folder, not '{}'", Path::ToUtf8(path));
			Show(assets / path.filename(), "Save " + m_TypeName + " inside the Assets folder");
			return std::nullopt;
		}
		return path;
	}

	fs::path AssetSaveDialog::UniquePath(const fs::path& folder, const std::string& stem, const std::string& extension) {
		std::error_code ec;
		fs::path path = folder / Path::FromUtf8(stem + extension);
		for (int i = 1; fs::exists(path, ec); ++i) path = folder / Path::FromUtf8(stem + " " + std::to_string(i) + extension);
		return path;
	}

	fs::path AssetSaveDialog::WithExtension(const fs::path& picked, const std::string& extension) {
		if (ToLower(Path::ToUtf8(picked.extension())) == ToLower(extension)) return picked;
		return UniquePath(picked.parent_path(), Path::ToUtf8(picked.filename()), extension);
	}

	bool AssetSaveDialog::IsInside(const fs::path& path, const fs::path& folder) {
		const fs::path relative = Canonical(path).lexically_relative(Canonical(folder));
		return !relative.empty() && relative != "." && *relative.begin() != "..";
	}

}
