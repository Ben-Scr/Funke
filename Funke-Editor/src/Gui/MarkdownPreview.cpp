#include "Gui/MarkdownPreview.hpp"

#include "Gui/EditorTheme.hpp"
#include "Utils/CharConv.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <md4c.h>
#include <SDL3/SDL_misc.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <fstream>
#include <unordered_map>

namespace Funke {
	namespace {
		constexpr size_t k_MaxPreviewBytes = 1024 * 1024;

		std::string DecodeEntity(std::string_view text) {
			if (text == "&amp;") return "&";
			if (text == "&lt;") return "<";
			if (text == "&gt;") return ">";
			if (text == "&quot;") return "\"";
			if (text == "&apos;") return "'";
			if (text == "&nbsp;") return "\xc2\xa0";
			if (text.size() > 3 && text[1] == '#') {
				const bool hex = text[2] == 'x' || text[2] == 'X';
				unsigned codepoint = 0;
				const char* end = text.data() + text.size() - 1;
				const auto result = std::from_chars(text.data() + (hex ? 3 : 2), end, codepoint, hex ? 16 : 10);
				if (result.ec == std::errc{} && result.ptr == end) {
					if (codepoint == 0 || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) codepoint = 0xfffd;
					char buffer[5]{};
					ImTextCharToUtf8(buffer, codepoint);
					return buffer;
				}
			}
			return std::string(text);
		}

		bool IsWebLink(const std::string& link) {
			return link.starts_with("https://") || link.starts_with("http://") || link.starts_with("mailto:");
		}

		std::string DecodeEntities(std::string_view text) {
			std::string result;
			for (size_t i = 0; i < text.size();) {
				const size_t end = text[i] == '&' ? text.find(';', i) : std::string_view::npos;
				if (end != std::string_view::npos && end - i < 50) {
					result += DecodeEntity(text.substr(i, end - i + 1));
					i = end + 1;
				}
				else result += text[i++];
			}
			return result;
		}

		std::string Lowercase(std::string text) {
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		struct HtmlTag {
			std::string Name;
			bool Closing = false;
			std::unordered_map<std::string, std::string> Attributes;
		};

		HtmlTag ParseTag(std::string_view text) {
			HtmlTag tag;
			size_t i = 0;
			const auto skipSpaces = [&]() { while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i; };
			skipSpaces();
			if (i < text.size() && text[i] == '/') { tag.Closing = true; ++i; }
			const size_t nameStart = i;
			while (i < text.size() && std::isalnum(static_cast<unsigned char>(text[i]))) ++i;
			tag.Name = Lowercase(std::string(text.substr(nameStart, i - nameStart)));
			while (i < text.size()) {
				skipSpaces();
				if (i == text.size() || text[i] == '/') break;
				const size_t start = i;
				while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])) && text[i] != '=') ++i;
				std::string name = Lowercase(std::string(text.substr(start, i - start)));
				skipSpaces();
				std::string value;
				if (i < text.size() && text[i] == '=') {
					++i; skipSpaces();
					const char quote = i < text.size() && (text[i] == '\'' || text[i] == '"') ? text[i++] : 0;
					const size_t valueStart = i;
					while (i < text.size() && (quote ? text[i] != quote : !std::isspace(static_cast<unsigned char>(text[i])))) ++i;
					value = DecodeEntities(text.substr(valueStart, i - valueStart));
					if (quote && i < text.size()) ++i;
				}
				tag.Attributes[std::move(name)] = std::move(value);
			}
			return tag;
		}

		float ParseDimension(std::string_view text, bool& percent) {
			percent = text.ends_with('%');
			if (percent) text.remove_suffix(1);
			float value = 0;
			const auto parsed = CharConv::ParseFloat(text.data(), text.data() + text.size(), value);
			return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(value) && value > 0
				? std::min(value, percent ? 100.0f : 8192.0f) : 0;
		}

		std::filesystem::path ResolveImagePath(const std::filesystem::path& document, std::string_view source) {
			if (source.empty() || source.starts_with("//")) return {};
			const size_t colon = source.find(':');
			if (colon != std::string_view::npos && colon != 1) return {};
			source = source.substr(0, source.find_first_of("?#"));
			std::string decoded;
			for (size_t i = 0; i < source.size(); ++i) {
				unsigned value = 0;
				if (source[i] == '%' && i + 2 < source.size()) {
					const auto result = std::from_chars(source.data() + i + 1, source.data() + i + 3, value, 16);
					if (result.ec == std::errc{} && result.ptr == source.data() + i + 3 && value != 0) {
						decoded += static_cast<char>(value); i += 2; continue;
					}
				}
				decoded += source[i];
			}
			const std::filesystem::path path(std::u8string(decoded.begin(), decoded.end()));
			return (path.is_absolute() ? path : document.parent_path() / path).lexically_normal();
		}

		struct TextStyle {
			int Bold = 0;
			int Italic = 0;
			int Strike = 0;
			int Code = 0;
			std::string Link;
		};

		void DrawText(std::string_view text, const TextStyle& style, bool& sameLine) {
			const char* cursor = text.data();
			const char* end = cursor + text.size();
			while (cursor < end) {
				if (sameLine) ImGui::SameLine(0, 0);
				const float available = std::max(1.0f, ImGui::GetContentRegionAvail().x);
				const char* wrap = ImGui::GetFont()->CalcWordWrapPosition(ImGui::GetFontSize(), cursor, end, available);
				if (sameLine && wrap < end && std::find_if(cursor, wrap, [](char c) { return c == ' ' || c == '\t'; }) == wrap) {
					ImGui::NewLine();
					sameLine = false;
					continue;
				}
				if (wrap == cursor) {
					if (sameLine) { ImGui::NewLine(); sameLine = false; continue; }
					wrap = cursor + 1;
					while (wrap < end && (static_cast<unsigned char>(*wrap) & 0xc0) == 0x80) ++wrap;
				}
				const ImVec2 pos = ImGui::GetCursorScreenPos();
				ImDrawList* draw = ImGui::GetWindowDrawList();
				const ImVec2 size = ImGui::CalcTextSize(cursor, wrap, false);
				if (style.Code) draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg), 2.0f);
				const ImU32 color = style.Link.empty() ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(EditorTheme::Colors::Highlight());
				const int vertexStart = draw->VtxBuffer.Size;
				draw->AddText(pos, color, cursor, wrap);
				if (style.Bold) draw->AddText(ImVec2(pos.x + ImGui::GetFontSize() * 0.035f, pos.y), color, cursor, wrap);
				if (style.Italic) {
					for (int i = vertexStart; i < draw->VtxBuffer.Size; ++i)
						draw->VtxBuffer[i].pos.x += (pos.y + size.y - draw->VtxBuffer[i].pos.y) * 0.15f;
				}
				ImGui::Dummy(size);
				if (style.Strike) draw->AddLine(ImVec2(pos.x, pos.y + size.y * 0.55f), ImVec2(pos.x + size.x, pos.y + size.y * 0.55f), color);
				if (!style.Link.empty()) {
					draw->AddLine(ImVec2(pos.x, pos.y + size.y), ImVec2(pos.x + size.x, pos.y + size.y), color);
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("%s", style.Link.c_str());
						if (IsWebLink(style.Link)) {
							ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
							if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) SDL_OpenURL(style.Link.c_str());
						}
					}
				}
				cursor = wrap;
				sameLine = true;
				if (cursor < end) {
					while (cursor < end && (*cursor == ' ' || *cursor == '\t' || *cursor == '\n')) ++cursor;
					sameLine = false;
				}
			}
		}
	}

	bool MarkdownPreview::SetText(std::string_view text, const std::filesystem::path& path) {
		m_Path = path;
		m_Events.clear();
		m_Error.clear();
		if (text.size() > k_MaxPreviewBytes) {
			m_Error = "Markdown preview is limited to 1 MB.";
			return false;
		}
		if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
		MD_PARSER parser{};
		parser.flags = MD_FLAG_COLLAPSEWHITESPACE | MD_FLAG_TABLES | MD_FLAG_TASKLISTS | MD_FLAG_STRIKETHROUGH;
		parser.enter_block = [](MD_BLOCKTYPE type, void* detail, void* user) {
			Event event{ EventKind::EnterBlock, type };
			if (type == MD_BLOCK_H) event.Value = static_cast<MD_BLOCK_H_DETAIL*>(detail)->level;
			if (type == MD_BLOCK_OL) event.Value = static_cast<MD_BLOCK_OL_DETAIL*>(detail)->start;
			if (type == MD_BLOCK_TABLE) event.Value = static_cast<MD_BLOCK_TABLE_DETAIL*>(detail)->col_count;
			if (type == MD_BLOCK_LI) {
				const auto& item = *static_cast<MD_BLOCK_LI_DETAIL*>(detail);
				if (item.is_task) event.Value = item.task_mark == ' ' ? 1 : 2;
			}
			static_cast<MarkdownPreview*>(user)->m_Events.push_back(std::move(event));
			return 0;
		};
		parser.leave_block = [](MD_BLOCKTYPE type, void*, void* user) {
			static_cast<MarkdownPreview*>(user)->m_Events.push_back({ EventKind::LeaveBlock, type }); return 0;
		};
		parser.enter_span = [](MD_SPANTYPE type, void* detail, void* user) {
			Event event{ EventKind::EnterSpan, type };
			if (type == MD_SPAN_A) {
				const auto& href = static_cast<MD_SPAN_A_DETAIL*>(detail)->href;
				event.Text.assign(href.text, href.size);
			}
			if (type == MD_SPAN_IMG) {
				const auto& src = static_cast<MD_SPAN_IMG_DETAIL*>(detail)->src;
				event.Text = DecodeEntities(std::string_view(src.text, src.size));
			}
			static_cast<MarkdownPreview*>(user)->m_Events.push_back(std::move(event)); return 0;
		};
		parser.leave_span = [](MD_SPANTYPE type, void*, void* user) {
			static_cast<MarkdownPreview*>(user)->m_Events.push_back({ EventKind::LeaveSpan, type }); return 0;
		};
		parser.text = [](MD_TEXTTYPE type, const MD_CHAR* data, MD_SIZE size, void* user) {
			std::string value(data, size);
			if (type == MD_TEXT_ENTITY) value = DecodeEntity(value);
			if (type == MD_TEXT_NULLCHAR) value = "\xef\xbf\xbd";
			if (type == MD_TEXT_SOFTBR) value = " ";
			auto& events = static_cast<MarkdownPreview*>(user)->m_Events;
			if (type != MD_TEXT_BR && !events.empty() && events.back().Kind == EventKind::Text && events.back().Type == type)
				events.back().Text += value;
			else
				events.push_back({ EventKind::Text, type, 0, std::move(value) });
			return 0;
		};
		if (md_parse(text.data(), static_cast<MD_SIZE>(text.size()), &parser, this) != 0) {
			m_Events.clear();
			m_Error = "Markdown preview could not be parsed.";
			return false;
		}
		NormalizeImagesAndHtml();
		return true;
	}

	void MarkdownPreview::NormalizeImagesAndHtml() {
		std::vector<Event> events;
		std::vector<size_t> details;
		bool summary = false;
		const auto appendText = [&](std::string_view text) {
			if (summary && !details.empty()) events[details.back()].Text += DecodeEntities(text);
			else if (text.find_first_not_of(" \t\r\n") != std::string_view::npos)
				events.push_back({ EventKind::Text, MD_TEXT_NORMAL, 0, DecodeEntities(text) });
		};
		for (size_t index = 0; index < m_Events.size(); ++index) {
			auto& event = m_Events[index];
			if (event.Kind == EventKind::EnterSpan && event.Type == MD_SPAN_IMG) {
				Event image{ EventKind::Image, 0, 0, std::move(event.Text) };
				int depth = 1;
				while (++index < m_Events.size()) {
					const auto& child = m_Events[index];
					if (child.Kind == EventKind::EnterSpan && child.Type == MD_SPAN_IMG) ++depth;
					if (child.Kind == EventKind::LeaveSpan && child.Type == MD_SPAN_IMG && --depth == 0) break;
					if (child.Kind == EventKind::Text) image.Alt += child.Text;
				}
				events.push_back(std::move(image));
				continue;
			}
			if (event.Kind != EventKind::Text || event.Type != MD_TEXT_HTML) {
				events.push_back(std::move(event)); continue;
			}
			std::string_view html = event.Text;
			while (!html.empty()) {
				const size_t start = html.find('<');
				appendText(html.substr(0, start));
				if (start == std::string_view::npos) break;
				html.remove_prefix(start);
				if (html.starts_with("<!--")) {
					const size_t end = html.find("-->");
					if (end == std::string_view::npos) break;
					html.remove_prefix(end + 3); continue;
				}
				char quote = 0;
				size_t end = 1;
				for (; end < html.size(); ++end) {
					if (quote) { if (html[end] == quote) quote = 0; }
					else if (html[end] == '\'' || html[end] == '"') quote = html[end];
					else if (html[end] == '>') break;
				}
				if (end == html.size()) { appendText(html); break; }
				auto tag = ParseTag(html.substr(1, end - 1));
				if (tag.Name == "img" && !tag.Closing) {
					Event image{ EventKind::Image, 0, 0, tag.Attributes["src"], tag.Attributes["alt"] };
					image.Width = ParseDimension(tag.Attributes["width"], image.WidthPercent);
					bool percent = false;
					image.Height = ParseDimension(tag.Attributes["height"], percent);
					if (percent) image.Height = 0;
					events.push_back(std::move(image));
				}
				else if (tag.Name == "details") {
					summary = false;
					if (!tag.Closing) {
						details.push_back(events.size());
						events.push_back({ EventKind::DetailsOpen, tag.Attributes.contains("open") ? 1 : 0 });
					}
					else if (!details.empty()) {
						events[details.back()].Value = static_cast<unsigned>(events.size());
						details.pop_back(); events.push_back({ EventKind::DetailsClose, 0 });
					}
				}
				else if (tag.Name == "summary") summary = !tag.Closing;
				else if (!summary && (tag.Name == "p" || tag.Name == "div" || tag.Name == "br")) events.push_back({ EventKind::Break, 0 });
				else if (!summary) appendText(html.substr(0, end + 1));
				html.remove_prefix(end + 1);
			}
		}
		while (!details.empty()) {
			events[details.back()].Value = static_cast<unsigned>(events.size());
			details.pop_back(); events.push_back({ EventKind::DetailsClose, 0 });
		}
		m_Events = std::move(events);
	}

	void MarkdownPreview::Refresh(const std::filesystem::path& path) {
		const auto now = std::chrono::steady_clock::now();
		const bool changedPath = path != m_Path;
		if (!changedPath && now < m_NextCheck) return;
		m_NextCheck = now + std::chrono::milliseconds(500);
		std::error_code error;
		const auto stamp = std::filesystem::last_write_time(path, error);
		const auto size = error ? 0 : std::filesystem::file_size(path, error);
		if (!changedPath && !error && m_Error.empty() && stamp == m_WriteTime && size == m_Size) return;
		m_Path = path;
		m_WriteTime = stamp;
		m_Size = size;
		m_Events.clear();
		m_Error.clear();
		if (error) { m_Error = "Markdown file could not be read."; return; }
		if (size > k_MaxPreviewBytes) { m_Error = "Markdown preview is limited to 1 MB."; return; }
		std::ifstream stream(path, std::ios::binary);
		std::string text(static_cast<size_t>(size), '\0');
		if (!stream || (size && !stream.read(text.data(), static_cast<std::streamsize>(size)))) {
			m_Error = "Markdown file could not be read."; return;
		}
		SetText(text, path);
	}

	void MarkdownPreview::Render(const std::filesystem::path& path, const ImageResolver& resolveImage) {
		Refresh(path);
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::PushID(path.string().c_str());
		if (ImGui::BeginChild("MarkdownPreview", ImVec2(0, 0))) DrawDocument(resolveImage);
		ImGui::EndChild();
		ImGui::PopID();
	}

	void MarkdownPreview::DrawDocument(const ImageResolver& resolveImage) const {
		if (!m_Error.empty()) { ImGui::TextWrapped("%s", m_Error.c_str()); return; }
		if (m_Events.size() <= 2) { ImGui::TextDisabled("This Markdown file is empty."); return; }
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0));
		TextStyle style;
		bool sameLine = false;
		bool codeBlock = false;
		bool table = false;
		bool tableHeader = false;
		std::vector<std::pair<bool, unsigned>> lists;
		int id = 0;
		float imageRowWidth = 0;
		float imageRowUsed = 0;
		const auto separate = [&]() { sameLine = false; imageRowUsed = 0; ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight() * 0.45f)); };
		for (size_t eventIndex = 0; eventIndex < m_Events.size(); ++eventIndex) {
			const auto& event = m_Events[eventIndex];
			if (event.Kind == EventKind::Image) {
				sameLine = false;
				const auto path = ResolveImagePath(m_Path, event.Text);
				const auto image = !path.empty() && resolveImage ? resolveImage(path) : ImageInfo{};
				if (imageRowUsed == 0) imageRowWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
				const float zoom = ImGui::GetFontSize() / 15.0f;
				float width = event.Width > 0 ? (event.WidthPercent ? imageRowWidth * event.Width / 100.0f : event.Width * zoom)
					: image.Width > 0 ? image.Width : std::min(240.0f * zoom, imageRowWidth);
				float height = event.Height > 0 ? event.Height * zoom : image.Width > 0 ? width * image.Height / image.Width : width * 0.5625f;
				if (event.Width == 0 && event.Height > 0 && image.Height > 0) width = height * image.Width / image.Height;
				const float scale = std::min(1.0f, imageRowWidth / std::max(1.0f, width));
				const ImVec2 size(std::max(1.0f, width * scale), std::max(1.0f, height * scale));
				const float gap = ImGui::GetStyle().ItemSpacing.x;
				if (imageRowUsed > 0 && imageRowUsed + gap + size.x <= imageRowWidth) {
					ImGui::SameLine(0, gap); imageRowUsed += gap;
				}
				else imageRowUsed = 0;
				if (image.Texture) ImGui::Image(static_cast<ImTextureID>(image.Texture), size,
					ImVec2(0, image.FlipY ? 1.0f : 0.0f), ImVec2(1, image.FlipY ? 0.0f : 1.0f));
				else {
					const ImVec2 pos = ImGui::GetCursorScreenPos();
					ImGui::Dummy(size);
					ImGui::GetWindowDrawList()->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(ImGuiCol_Border));
					const char* status = event.Text.empty() ? "Image source is missing" : path.empty() ? "Remote images are not supported"
						: image.Failed ? "Image could not be loaded" : "Loading image...";
					ImGui::RenderTextClipped(ImVec2(pos.x + 4, pos.y + 4), ImVec2(pos.x + size.x - 4, pos.y + size.y - 4), status, nullptr, nullptr);
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", event.Alt.c_str(), event.Text.c_str());
				imageRowUsed += size.x;
				continue;
			}
			if (event.Kind == EventKind::DetailsOpen) {
				separate();
				ImGui::PushID(static_cast<int>(eventIndex));
				const std::string label = (event.Text.empty() ? "Details" : event.Text) + "###Details";
				const bool open = ImGui::CollapsingHeader(label.c_str(), event.Type ? ImGuiTreeNodeFlags_DefaultOpen : 0);
				ImGui::PopID();
				if (open) ImGui::Indent();
				else eventIndex = event.Value;
				continue;
			}
			if (event.Kind == EventKind::DetailsClose) { ImGui::Unindent(); separate(); continue; }
			if (event.Kind == EventKind::Break) { if (sameLine || imageRowUsed > 0) separate(); continue; }
			if (event.Kind == EventKind::Text) {
				imageRowUsed = 0;
				if (event.Type == MD_TEXT_BR) { if (!sameLine) ImGui::NewLine(); sameLine = false; }
				else if (codeBlock) ImGui::TextUnformatted(event.Text.data(), event.Text.data() + event.Text.size());
				else DrawText(event.Text, style, sameLine);
				continue;
			}
			if (event.Kind == EventKind::EnterSpan || event.Kind == EventKind::LeaveSpan) {
				const int delta = event.Kind == EventKind::EnterSpan ? 1 : -1;
				switch (event.Type) {
				case MD_SPAN_STRONG: style.Bold += delta; break;
				case MD_SPAN_EM: style.Italic += delta; break;
				case MD_SPAN_DEL: style.Strike += delta; break;
				case MD_SPAN_CODE: style.Code += delta; break;
				case MD_SPAN_A: style.Link = delta > 0 ? event.Text : ""; break;
				default: break;
				}
				continue;
			}
			if (event.Kind == EventKind::EnterBlock) {
				switch (event.Type) {
				case MD_BLOCK_P: if (sameLine) separate(); break;
				case MD_BLOCK_H:
					separate();
					ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * (1.0f + (7 - event.Value) * 0.1f));
					++style.Bold; break;
				case MD_BLOCK_QUOTE: separate(); ImGui::Indent(); ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); break;
				case MD_BLOCK_UL: case MD_BLOCK_OL:
					separate(); lists.emplace_back(event.Type == MD_BLOCK_OL, event.Value); ImGui::Indent(); break;
				case MD_BLOCK_LI:
					sameLine = false;
					if (event.Value) {
						bool checked = event.Value == 2;
						ImGui::PushID(id++);
						ImGui::BeginDisabled(); ImGui::Checkbox("##task", &checked); ImGui::EndDisabled();
						ImGui::PopID();
					}
					else if (!lists.empty() && lists.back().first) ImGui::Text("%u.", lists.back().second++);
					else ImGui::Bullet();
					ImGui::SameLine(); ImGui::BeginGroup(); break;
				case MD_BLOCK_HR: separate(); ImGui::Separator(); break;
				case MD_BLOCK_CODE:
					separate(); ImGui::PushID(id++);
					ImGui::BeginChild("Code", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
					codeBlock = true; break;
				case MD_BLOCK_TABLE:
					separate(); ImGui::PushID(id++);
					table = event.Value <= 64 && ImGui::BeginTable("Table", static_cast<int>(event.Value), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame);
					break;
				case MD_BLOCK_THEAD: tableHeader = true; ++style.Bold; break;
				case MD_BLOCK_TR: sameLine = false; if (table) ImGui::TableNextRow(tableHeader ? ImGuiTableRowFlags_Headers : 0); break;
				case MD_BLOCK_TH: case MD_BLOCK_TD: sameLine = false; if (table) ImGui::TableNextColumn(); break;
				default: break;
				}
			}
			else {
				switch (event.Type) {
				case MD_BLOCK_H: ImGui::PopFont(); --style.Bold; separate(); break;
				case MD_BLOCK_P: separate(); break;
				case MD_BLOCK_QUOTE: ImGui::PopStyleColor(); ImGui::Unindent(); separate(); break;
				case MD_BLOCK_UL: case MD_BLOCK_OL: ImGui::Unindent(); lists.pop_back(); separate(); break;
				case MD_BLOCK_LI: ImGui::EndGroup(); sameLine = false; break;
				case MD_BLOCK_CODE: ImGui::EndChild(); ImGui::PopID(); codeBlock = false; separate(); break;
				case MD_BLOCK_TABLE: if (table) ImGui::EndTable(); ImGui::PopID(); table = false; separate(); break;
				case MD_BLOCK_THEAD: tableHeader = false; --style.Bold; break;
				default: break;
				}
			}
		}
		ImGui::PopStyleVar();
	}
}
