#pragma once
#include "Core/Export.hpp"
#include "Graphics/CustomShader.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Index {

	// User .wgsl shader assets by GUID (main thread). Parsing works without a GPU; renderers fetch compiled programs
	// through CustomShaderGpu. Errors go to the Console as `<file>(<line>): <message>`; a broken edit keeps the last
	// working version, and a shader that never compiled renders magenta.
	class INDEX_API CustomShaderLibrary {
	public:
		CustomShaderLibrary() = delete;

		// Header and properties of the last version that parsed; nullptr for 0, unknown GUIDs and files that never parsed.
		static const ShaderDescription* GetDescription(uint64_t guid);
		// Problems of the current file content (parse and, once compiled, WGSL errors), with user-file line numbers.
		static std::vector<ShaderDiagnostic> GetDiagnostics(uint64_t guid);
		static std::string GetPath(uint64_t guid);

		// Picks up edits made outside the engine: every get re-checks the file at most twice a second while enabled.
		// On in the editor; shipped games never change their shaders.
		static void SetHotReloadEnabled(bool enabled);
		static bool IsHotReloadEnabled();
		// Bumps on every successful parse or compile of any shader; the editor redraws dependent views on change.
		static uint32_t GetGeneration();

		static void Invalidate(uint64_t guid);
		// Releases GPU modules; call before RenderApi::Shutdown.
		static void Clear();

		// Parses and compiles a shader file with a private headless device when the engine has none (CLI validate).
		// Returns false when the file has errors; compile=false stops after parsing.
		static bool ValidateFile(const std::string& path, std::vector<ShaderDiagnostic>& outDiagnostics, bool compile = true);
		// Formats diagnostics like the Console does: "<path>(<line>): error: <message>".
		static std::string FormatDiagnostic(const std::string& path, const ShaderDiagnostic& diagnostic);
	};

}
