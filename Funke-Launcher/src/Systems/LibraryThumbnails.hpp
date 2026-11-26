#pragma once
#include "Graphics/Texture2D.hpp"
#include "Core/UUID.hpp"
#include "Utils/PackageToolPath.hpp"
#include "Utils/Process.hpp"
#include <chrono>
#include <filesystem>
#include <future>
#include <functional>
#include <utility>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

namespace Funke {
	class LibraryThumbnails {
		struct Entry {
			std::future<std::unique_ptr<ImageData>> Pending;
			Texture2D Texture;
			std::filesystem::path File;
			bool Cached = false;
			~Entry() {
				std::error_code ec;
				if (!File.empty()) std::filesystem::remove(File, ec);
			}
			uint64_t LastFrame = 0;
			uint64_t Generation = 0;
		};
	public:
		enum class State { Missing, Loading, Ready, Failed };
		using Loader = std::function<bool(const std::string&, const std::filesystem::path&)>;
		explicit LibraryThumbnails(Loader loader = Download) : m_Loader(std::move(loader)) {}
		State GetState(const std::string& url, const std::string& version) const {
			const auto it = m_Entries.find(url + "\n" + version);
			if (url.empty() || it == m_Entries.end() || it->second.Generation != m_Generation) return State::Missing;
			if (it->second.Pending.valid()) return State::Loading;
			return it->second.Cached ? State::Ready : State::Failed;
		}
		void Refresh() { ++m_Generation; }
		void Pump() {
			++m_Frame;
			for (auto it = m_Entries.begin(); it != m_Entries.end();) {
				auto& entry = it->second;
				if (entry.Pending.valid() && entry.Pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
					auto image = entry.Pending.get();
					--m_Loading;
					entry.Cached = image && entry.Generation == m_Generation;
					if (entry.Cached) entry.Texture.LoadFromImage(*image, "", false);
					entry.Cached = entry.Texture.GetHandle() != 0;
				}
				if (!entry.Pending.valid() && entry.Generation != m_Generation) it = m_Entries.erase(it);
				else {
					if (entry.Texture.GetHandle() && entry.LastFrame + 120 < m_Frame) entry.Texture.Destroy();
					++it;
				}
			}
		}
		const Texture2D* Request(const std::string& url, const std::string& version) {
			if (url.empty()) return nullptr;
			const std::string key = url + "\n" + version;
			auto it = m_Entries.find(key);
			if (it != m_Entries.end()) {
				auto& entry = it->second;
				entry.LastFrame = m_Frame;
				if (entry.Generation != m_Generation || entry.Pending.valid() || !entry.Cached) return nullptr;
				if (entry.Texture.GetHandle()) return &entry.Texture;
			}
			if (m_Loading >= 2 || (it == m_Entries.end() && m_Entries.size() >= 512)) return nullptr;
			size_t textures = 0;
			for (const auto& [_, entry] : m_Entries) textures += entry.Texture.GetHandle() != 0;
			if (textures + m_Loading >= 32) return nullptr;
			auto& entry = m_Entries[key];
			entry.Generation = m_Generation;
			entry.LastFrame = m_Frame;
			++m_Loading;
			entry.Pending = std::async(std::launch::async, [url, &entry, cached = entry.Cached, loader = m_Loader] {
				try {
					if (!cached) {
						entry.File = std::filesystem::temp_directory_path() / ("funke-thumbnail-" + std::to_string(static_cast<uint64_t>(UUID())) + ".tmp");
						if (!loader(url, entry.File)) return std::unique_ptr<ImageData>{};
					}
					return Texture2D::DecodeFileToCpu(entry.File.string().c_str(), false, 1024 * 1024);
				} catch (...) { return std::unique_ptr<ImageData>{}; }
			});
			return nullptr;
		}

		void Shutdown() {
			for (auto& [_, entry] : m_Entries) if (entry.Pending.valid()) entry.Pending.wait();
			m_Entries.clear();
			m_Loading = 0;
		}
		~LibraryThumbnails() { Shutdown(); }
	private:
		static bool Download(const std::string& url, const std::filesystem::path& path) {
			const auto tool = PackageTool::ResolveExecutable();
			if (tool.empty()) return false;
			std::vector<std::string> command;
			if (tool.extension() == ".dll") command.push_back("dotnet");
			command.insert(command.end(), { tool.string(), "thumbnail-download", url, path.string() });
			return Process::Run(command, std::filesystem::path{}, std::chrono::seconds(10)).Succeeded();
		}
		Loader m_Loader;
		std::unordered_map<std::string, Entry> m_Entries;
		uint64_t m_Generation = 0;
		uint64_t m_Frame = 0;
		size_t m_Loading = 0;
	};
}
