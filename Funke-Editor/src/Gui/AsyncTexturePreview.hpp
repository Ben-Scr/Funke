#pragma once
#include "Graphics/Texture2D.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Funke {
	struct TexturePreview {
		enum class Status { Loading, Ready, Failed };
		const std::string Path;
		const Filter Sampling;
		const bool KeepPixels;
		Status State = Status::Loading;
		std::unique_ptr<Texture2D> Texture;
		std::unique_ptr<ImageData> Pixels;
		TexturePreview(std::string path, Filter sampling, bool keepPixels)
			: Path(std::move(path)), Sampling(sampling), KeepPixels(keepPixels) {}
		Texture2D* GetTexture() const { return State == Status::Ready ? Texture.get() : nullptr; }
	};

	class PreviewDecodeQueue {
	public:
		using Decoder = std::function<std::unique_ptr<ImageData>(const std::string&)>;
		struct Result {
			std::weak_ptr<TexturePreview> Request;
			std::unique_ptr<ImageData> Image;
		};
		explicit PreviewDecodeQueue(Decoder decoder = {});
		~PreviewDecodeQueue();
		void Enqueue(const std::shared_ptr<TexturePreview>& request, bool priority);
		bool Take(Result& result);
		void Shutdown();
	private:
		void Run();
		Decoder m_Decoder;
		std::mutex m_Mutex;
		std::condition_variable m_Wake;
		std::deque<std::weak_ptr<TexturePreview>> m_Pending;
		Result m_Ready;
		bool m_HasReady = false;
		bool m_Stop = false;
		std::thread m_Worker;
	};

	// Request and Pump run on the UI thread; workers never own GPU resources.
	class AsyncTexturePreviews {
	public:
		static AsyncTexturePreviews& Get();
		std::shared_ptr<TexturePreview> Request(const std::string& path,
			Filter filter = Filter::Point, bool keepPixels = false, bool priority = true);
		void Pump();
		void Retire(std::shared_ptr<TexturePreview> preview);
		void Shutdown();
	private:
		PreviewDecodeQueue m_Decode;
		PreviewDecodeQueue::Result m_Upload;
		int m_UploadedRows = 0;
		std::vector<std::shared_ptr<TexturePreview>> m_Retired;
	};
}
