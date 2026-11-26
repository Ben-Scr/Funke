#pragma once
#include "Gui/StringTableDocument.hpp"
#include "Undo/AssetFileHistory.hpp"
#include <chrono>
#include <map>
#include <set>

namespace Funke {
	class StringTablePanel {
	public:
		void Render(bool* open);
		void Shutdown();
	private:
		void Scan();
		std::filesystem::path m_Directory;
		StringTableDocument m_Document;
		AssetFileHistory m_History;
		std::vector<std::filesystem::path> m_Files;
		std::set<std::string> m_Keys;
		std::vector<std::string> m_Diagnostics;
		std::string m_Selected, m_Status;
		std::chrono::steady_clock::time_point m_NextScan{};
		char m_Search[256]{}, m_NewKey[256]{}, m_NewCode[40]{}, m_NewName[128]{};
		std::vector<char> m_Text;
		bool m_MissingOnly = false;
	};
}
