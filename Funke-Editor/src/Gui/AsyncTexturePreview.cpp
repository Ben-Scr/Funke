#include <pch.hpp>
#include "Gui/AsyncTexturePreview.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <numeric>

namespace Funke {
	namespace {
		std::unique_ptr<ImageData> ReducePreview(std::unique_ptr<ImageData> image, Filter filter, int limit) {
			if (!image || !image->Pixels || limit <= 0 || std::max(image->Width, image->Height) <= limit) return image;
			const int width = image->Width;
			const int height = image->Height;
			int outWidth = width;
			int outHeight = height;
			if (filter == Filter::Point) {
				// Only remove exactly repeated pixel blocks; thin details must survive unchanged.
				int grid = std::gcd(width, height);
				for (int y = 0; y < height && grid > 1; ++y) {
					const auto* row = image->Pixels + static_cast<size_t>(y) * width * 4;
					if (y != 0 && std::memcmp(row, row - static_cast<size_t>(width) * 4, static_cast<size_t>(width) * 4) != 0)
						grid = std::gcd(grid, y);
					for (int x = 1; x < width && grid > 1; ++x)
						if (std::memcmp(row + static_cast<size_t>(x) * 4, row + static_cast<size_t>(x - 1) * 4, 4) != 0)
							grid = std::gcd(grid, x);
				}
				if (grid <= 1) return image;
				outWidth = width / grid;
				outHeight = height / grid;
			}
			else {
				const double scale = static_cast<double>(limit) / std::max(width, height);
				outWidth = std::max(1, static_cast<int>(width * scale));
				outHeight = std::max(1, static_cast<int>(height * scale));
			}
			std::vector<unsigned char> pixels(static_cast<size_t>(outWidth) * outHeight * 4);
			for (int y = 0; y < outHeight; ++y) {
				for (int x = 0; x < outWidth; ++x) {
					const int x0 = static_cast<int>(static_cast<int64_t>(x) * width / outWidth);
					const int y0 = static_cast<int>(static_cast<int64_t>(y) * height / outHeight);
					auto* target = pixels.data() + (static_cast<size_t>(y) * outWidth + x) * 4;
					if (filter == Filter::Point) {
						std::memcpy(target, image->Pixels + (static_cast<size_t>(y0) * width + x0) * 4, 4);
						continue;
					}
					const int x1 = static_cast<int>(static_cast<int64_t>(x + 1) * width / outWidth);
					const int y1 = static_cast<int>(static_cast<int64_t>(y + 1) * height / outHeight);
					uint64_t alpha = 0, colors[3]{};
					for (int sy = y0; sy < y1; ++sy) for (int sx = x0; sx < x1; ++sx) {
						const auto* source = image->Pixels + (static_cast<size_t>(sy) * width + sx) * 4;
						alpha += source[3];
						for (int channel = 0; channel < 3; ++channel) colors[channel] += static_cast<uint64_t>(source[channel]) * source[3];
					}
					for (int channel = 0; channel < 3; ++channel) target[channel] = alpha ? static_cast<unsigned char>((colors[channel] + alpha / 2) / alpha) : 0;
					const uint64_t samples = static_cast<uint64_t>(x1 - x0) * (y1 - y0);
					target[3] = static_cast<unsigned char>((alpha + samples / 2) / samples);
				}
			}
			return std::make_unique<ImageData>(outWidth, outHeight, std::move(pixels));
		}
	}

	PreviewDecodeQueue::PreviewDecodeQueue(Decoder decoder)
		: m_Decoder(decoder ? std::move(decoder) : Decoder([](const std::string& path) {
			return Texture2D::DecodeFileToCpu(path.c_str(), false);
		})) {}

	PreviewDecodeQueue::~PreviewDecodeQueue() { Shutdown(); }

	void PreviewDecodeQueue::Enqueue(const std::shared_ptr<TexturePreview>& request, bool priority) {
		std::lock_guard lock(m_Mutex);
		std::erase_if(m_Pending, [](const auto& pending) { return pending.expired(); });
		if (!m_Worker.joinable()) {
			m_Stop = false;
			m_Worker = std::thread([this] { Run(); });
		}
		if (priority) m_Pending.push_front(request);
		else m_Pending.push_back(request);
		m_Wake.notify_one();
	}

	bool PreviewDecodeQueue::Take(Result& result) {
		std::lock_guard lock(m_Mutex);
		if (!m_HasReady) return false;
		result = std::move(m_Ready);
		m_HasReady = false;
		m_Wake.notify_one();
		return true;
	}

	void PreviewDecodeQueue::Run() {
		std::unique_lock lock(m_Mutex);
		while (!m_Stop) {
			// One completed image provides backpressure while the GPU upload catches up.
			if (m_HasReady && m_Ready.Request.expired()) {
				m_Ready = {};
				m_HasReady = false;
			}
			if (m_HasReady || m_Pending.empty()) {
				m_Wake.wait_for(lock, std::chrono::milliseconds(20));
				continue;
			}
			auto weak = m_Pending.front();
			m_Pending.pop_front();
			auto request = weak.lock();
			if (!request) continue;
			const std::string path = request->Path;
			const int limit = request->KeepPixels ? 0 : request->MaxDimension;
			const Filter sampling = request->Sampling;
			request.reset();
			lock.unlock();
			std::unique_ptr<ImageData> image;
			int sourceWidth = 0, sourceHeight = 0;
			try {
				image = m_Decoder(path);
				if (image) { sourceWidth = image->Width; sourceHeight = image->Height; }
				image = ReducePreview(std::move(image), sampling, limit);
			}
			catch (...) {}
			lock.lock();
			if (!m_Stop && !weak.expired()) {
				m_Ready = { weak, std::move(image), sourceWidth, sourceHeight };
				m_HasReady = true;
			}
		}
	}

	void PreviewDecodeQueue::Shutdown() {
		{
			std::lock_guard lock(m_Mutex);
			m_Stop = true;
			m_Pending.clear();
			m_Wake.notify_one();
		}
		if (m_Worker.joinable()) m_Worker.join();
		m_Ready = {};
		m_HasReady = false;
	}

	AsyncTexturePreviews& AsyncTexturePreviews::Get() {
		static AsyncTexturePreviews previews;
		return previews;
	}

	std::shared_ptr<TexturePreview> AsyncTexturePreviews::Request(const std::string& path,
		Filter filter, bool keepPixels, bool priority, int maxDimension) {
		auto request = std::make_shared<TexturePreview>(path, filter, keepPixels, maxDimension);
		m_Decode.Enqueue(request, priority);
		return request;
	}

	void AsyncTexturePreviews::Retire(std::shared_ptr<TexturePreview> preview) {
		if (preview) m_Retired.push_back(std::move(preview));
	}

	void AsyncTexturePreviews::Pump() {
		// Previous-frame ImGui commands may still reference a replaced sprite preview.
		m_Retired.clear();
		if (m_Upload.Request.expired()) m_Upload = {};
		if (!m_Upload.Image) {
			if (!m_Decode.Take(m_Upload)) return;
			m_UploadedRows = 0;
		}
		auto request = m_Upload.Request.lock();
		if (!request) { m_Upload = {}; return; }
		auto& image = m_Upload.Image;
		request->SourceWidth = m_Upload.SourceWidth;
		request->SourceHeight = m_Upload.SourceHeight;
		if (!image || !image->Pixels || image->Width <= 0 || image->Height <= 0) {
			request->State = TexturePreview::Status::Failed;
			m_Upload = {};
			return;
		}
		if (!request->Texture) {
			request->Texture = std::make_unique<Texture2D>();
			if (!request->Texture->CreateForUpload(image->Width, image->Height, request->Sampling)) {
				request->State = TexturePreview::Status::Failed;
				request->Texture.reset();
				m_Upload = {};
				return;
			}
		}
		constexpr size_t kUploadBytesPerFrame = 4 * 1024 * 1024;
		const size_t rowBytes = static_cast<size_t>(image->Width) * 4u;
		const int rows = std::min(image->Height - m_UploadedRows,
			static_cast<int>(std::max(size_t{1}, kUploadBytesPerFrame / rowBytes)));
		if (!request->Texture->UploadRows(m_UploadedRows, rows,
			image->Pixels + rowBytes * m_UploadedRows, rowBytes * rows)) {
			request->State = TexturePreview::Status::Failed;
			request->Texture.reset();
			m_Upload = {};
			return;
		}
		m_UploadedRows += rows;
		if (m_UploadedRows == image->Height) {
			request->State = TexturePreview::Status::Ready;
			if (request->KeepPixels) request->Pixels = std::move(image);
			m_Upload = {};
		}
	}

	void AsyncTexturePreviews::Shutdown() {
		m_Retired.clear();
		m_Decode.Shutdown();
		m_Upload = {};
		m_UploadedRows = 0;
	}
}
