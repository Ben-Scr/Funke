#include <pch.hpp>
#include "Gui/AsyncTexturePreview.hpp"
#include <algorithm>
#include <chrono>

namespace Funke {
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
			request.reset();
			lock.unlock();
			std::unique_ptr<ImageData> image;
			try { image = m_Decoder(path); }
			catch (...) {}
			lock.lock();
			if (!m_Stop && !weak.expired()) {
				m_Ready = { weak, std::move(image) };
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
		Filter filter, bool keepPixels, bool priority) {
		auto request = std::make_shared<TexturePreview>(path, filter, keepPixels);
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
