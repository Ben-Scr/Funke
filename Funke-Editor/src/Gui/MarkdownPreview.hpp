#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Funke {

	class MarkdownPreview {
	public:
		struct ImageInfo {
			uint64_t Texture = 0;
			float Width = 0;
			float Height = 0;
			bool FlipY = false;
			bool Failed = false;
		};
		using ImageResolver = std::function<ImageInfo(const std::filesystem::path&)>;

		void Render(const std::filesystem::path& path, const ImageResolver& resolveImage = {});
		bool SetText(std::string_view text, const std::filesystem::path& path = {});
		void DrawDocument(const ImageResolver& resolveImage = {}) const;

	private:
		enum class EventKind { EnterBlock, LeaveBlock, EnterSpan, LeaveSpan, Text, Image, Break, DetailsOpen, DetailsClose };
		struct Event {
			EventKind Kind;
			int Type;
			unsigned Value = 0;
			std::string Text;
			std::string Alt;
			float Width = 0;
			float Height = 0;
			bool WidthPercent = false;
		};

		void NormalizeImagesAndHtml();
		void Refresh(const std::filesystem::path& path);
		std::vector<Event> m_Events;
		std::filesystem::path m_Path;
		std::filesystem::file_time_type m_WriteTime{};
		std::uintmax_t m_Size = 0;
		std::chrono::steady_clock::time_point m_NextCheck{};
		std::string m_Error;
	};
}
