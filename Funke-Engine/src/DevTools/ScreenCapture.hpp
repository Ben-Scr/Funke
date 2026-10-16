#pragma once
#include "Core/Export.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Funke {

	// PNG of the window's final frame: scene, game UI and overlays. Requests are served right before present.
	class FUNKE_API ScreenCapture {
	public:
		// Captures the next presented frame. A relative path (or bare file name) lands in GetDirectory().
		static void Request(const std::string& path);
		static bool HasPending();
		// Application calls this before SwapBuffers; writes every pending request.
		static void ProcessPending();

		// Default: %LOCALAPPDATA%/Funke/Screenshots (the platform's user data root elsewhere).
		static std::string GetDirectory();
		static void SetDirectory(const std::string& directory);
		static std::string ResolvePath(const std::string& path);

		static bool WritePng(const std::string& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba);
	};

} // namespace Funke
