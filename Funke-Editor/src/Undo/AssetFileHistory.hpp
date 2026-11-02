#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Funke {
	class AssetFileHistory {
	public:
		struct Change {
			~Change();
			std::string Label;
			std::filesystem::path Storage;
			std::vector<std::filesystem::path> Paths;
			std::vector<std::string> Before;
			std::vector<std::string> After;
			std::function<void(bool)> Validate;
			std::function<void(bool)> Applied;
			bool KeepRecoveryFiles = false;
		};
		void Initialize(const std::filesystem::path& assetsRoot);
		std::shared_ptr<Change> Begin(const std::vector<std::string>& paths, std::string label);
		void Commit(const std::shared_ptr<Change>& change);
		void RecordCreated(const std::vector<std::string>& paths, std::string label);
		bool CanUndo() const { return !m_Undo.empty(); }
		bool CanRedo() const { return !m_Redo.empty(); }
		std::string Undo();
		std::string Redo();
		void Clear();
		static bool MoveAsset(const std::filesystem::path& from, const std::filesystem::path& to);
		static bool DestinationAvailable(const std::filesystem::path& path);
	private:
		static std::string Fingerprint(const std::filesystem::path& path);
		static void Capture(Change& change, bool after);
		static void Apply(Change& change, bool redo);
		std::filesystem::path m_Storage;
		std::vector<std::shared_ptr<Change>> m_Undo;
		std::vector<std::shared_ptr<Change>> m_Redo;
	};
}
