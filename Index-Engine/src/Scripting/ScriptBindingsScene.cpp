#include "pch.hpp"
#include "Scripting/ScriptBindingGuard.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Core/Application.hpp"
#include "Core/Engine.hpp"
#include "Core/Input.hpp"
#include "Core/Time.hpp"
#include "Core/Window.hpp"
#include "Core/Log.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/File.hpp"
#include "Project/ProjectManager.hpp"
#include "Project/IndexProject.hpp"
#include "Scripting/ScriptSystem.hpp"
#include <Systems/AudioUpdateSystem.hpp>
#include "Scene/ComponentRegistry.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/Tags.hpp"
#include "Audio/AudioManager.hpp"
#include "Audio/AudioStreams.hpp"
#include "Graphics/ImageProcessing.hpp"
#include "Core/Platform.hpp"
#include "Graphics/TextureManager.hpp"
#include "Graphics/Gizmo.hpp"
#include "Graphics/RenderApi.hpp"
#include "Graphics/RenderSettings.hpp"
#include "Core/Version.hpp"

#include <algorithm>

namespace Index {
	EntityHandle ToEntityHandle(uint64_t id);
	uint64_t FromEntityHandle(EntityHandle handle);
	uint64_t GetEntityScriptId(const Scene& scene, EntityHandle handle);
	bool TryResolveEntityByUUID(const Scene& scene, uint64_t entityID, EntityHandle& outHandle);
	bool ResolveEntityReference(uint64_t entityID, Scene*& outScene, EntityHandle& outHandle);
	Scene* GetScene();
	extern thread_local std::string s_StringReturnBuffer;

	// Thread-local buffer for returning strings to managed code
	// ── Application Bindings ────────────────────────────────────────────

	static float Index_Application_GetDeltaTime()
	{
		return Time::GetDeltaTime();
	}

	static float Index_Application_GetTime()
	{
		return Time::GetElapsedTime();
	}

	static int Index_Application_GetScreenWidth()
	{
		auto* window = Application::GetWindow();
		return window ? window->GetWidth() : 0;
	}

	static void Index_Application_GetSafeAreaInsets(float* left, float* top, float* right, float* bottom)
	{
		const Window::SafeAreaInsets insets = Window::GetSafeAreaInsets();
		if (left) *left = insets.Left;
		if (top) *top = insets.Top;
		if (right) *right = insets.Right;
		if (bottom) *bottom = insets.Bottom;
	}

	static int Index_Application_GetScreenHeight()
	{
		auto* window = Application::GetWindow();
		return window ? window->GetHeight() : 0;
	}

	static float Index_Application_GetTargetFrameRate() { return Application::GetTargetFramerate(); }
	static void  Index_Application_SetTargetFrameRate(float fps) { Application::SetTargetFramerate(fps); }

	static void Index_Application_Quit() {
		Application* app = Application::GetInstance();
		if (!app) return;
		if (Application::IsEditor()) {
			// In editor: scripted Quit routes to RequestEditorStopPlay instead of closing the editor window; the flag is drained in the pre-render pass.
			Application::RequestEditorStopPlay();
			return;
		}
		Application::Quit();
	}

	static void Index_Application_Reload() {
		Application* app = Application::GetInstance();
		if (!app) return;
		if (Application::IsEditor()) {
			// In editor: scripted Reload routes to RequestEditorStopPlay to avoid tearing down the editor process.
			Application::RequestEditorStopPlay();
			return;
		}
		Application::Reload();
	}

	static void Index_Application_Restart() {
		Application* app = Application::GetInstance();
		if (!app) return;
		if (Application::IsEditor()) {
			Application::RequestEditorStopPlay();
			return;
		}
		Application::Restart();
	}

	static void Index_RenderSettings_Get(float* renderScale, int* upscaleFilter,
		int* postProcessingEnabled, int* postProcessingQuality, int* particleQuality) {
		const GraphicsSettings settings = RenderSettings::Get();
		if (renderScale) *renderScale = settings.RenderScale;
		if (upscaleFilter) *upscaleFilter = static_cast<int>(settings.UpscaleFilter);
		if (postProcessingEnabled) *postProcessingEnabled = settings.PostProcessingEnabled ? 1 : 0;
		if (postProcessingQuality) *postProcessingQuality = static_cast<int>(settings.PostProcessingQuality);
		if (particleQuality) *particleQuality = static_cast<int>(settings.ParticleQuality);
	}

	static int Index_RenderSettings_Apply(float renderScale, int upscaleFilter,
		int postProcessingEnabled, int postProcessingQuality, int particleQuality) {
		// Starts from the requested settings so the 3D fields (applied through RenderSettings_Apply3D) survive.
		GraphicsSettings settings = RenderSettings::Get();
		settings.RenderScale = renderScale;
		settings.UpscaleFilter = static_cast<UpscaleFilterMode>(std::clamp(upscaleFilter, 0, 1));
		settings.PostProcessingEnabled = std::clamp(postProcessingEnabled, 0, 1) != 0;
		settings.PostProcessingQuality = static_cast<PostProcessingQualityLevel>(
			std::clamp(postProcessingQuality, 0, 2));
		settings.ParticleQuality = static_cast<ParticleQualityLevel>(
			std::clamp(particleQuality, 0, 3));
		return static_cast<int>(RenderSettings::Apply(settings));
	}

	static void Index_RenderSettings_Get3D(int* shadowQuality, float* shadowDistance, int* maxLights,
		float* exposure, int* toneMapping) {
		const GraphicsSettings settings = RenderSettings::Get();
		if (shadowQuality) *shadowQuality = static_cast<int>(settings.ShadowQuality);
		if (shadowDistance) *shadowDistance = settings.ShadowDistance;
		if (maxLights) *maxLights = settings.MaxLights;
		if (exposure) *exposure = settings.Exposure;
		if (toneMapping) *toneMapping = static_cast<int>(settings.ToneMapping);
	}

	static int Index_RenderSettings_Apply3D(int shadowQuality, float shadowDistance, int maxLights,
		float exposure, int toneMapping) {
		GraphicsSettings settings = RenderSettings::Get();
		settings.ShadowQuality = static_cast<ShadowQualityLevel>(std::clamp(shadowQuality, 0, 3));
		settings.ShadowDistance = shadowDistance;
		settings.MaxLights = maxLights;
		settings.Exposure = exposure;
		settings.ToneMapping = static_cast<ToneMappingMode>(std::clamp(toneMapping, 0, 2));
		return static_cast<int>(RenderSettings::Apply(settings));
	}

	static int Index_RenderSettings_ResetToDefaults() {
		return static_cast<int>(RenderSettings::ResetToDefaults());
	}

	static int Index_Application_IsEditor() {
		return Application::IsEditor() ? 1 : 0;
	}

	static void Index_Application_RequestPermission(const char* permission) {
		Platform::RequestPermission(permission ? permission : "");
	}

	static int Index_Application_GetPermissionState(const char* permission) {
		return static_cast<int>(Platform::GetPermissionState(permission ? permission : ""));
	}

	// ── Audio Stream Bindings ───────────────────────────────────────────

	static uint32_t Index_AudioStream_Open(const char* path) {
		return path ? AudioStreams::Open(path) : 0;
	}

	static void Index_Audio_SetPlayInBackground(int enabled) { AudioManager::SetPlayInBackground(enabled != 0); }
	static int Index_Audio_GetPlayInBackground() { return AudioManager::GetPlayInBackground() ? 1 : 0; }

	// Returns the decoded byte count (0 = not an image); pixels are written only when they fit in capacity.
	static int Index_Image_Decode(const uint8_t* data, int size, int maxSize, uint8_t* outRgba, int capacity, int* outWidth, int* outHeight) {
		if (!data || size <= 0 || !outWidth || !outHeight) return 0;
		std::unique_ptr<ImageData> image = ImageProcessing::Decode({ data, static_cast<size_t>(size) });
		if (!image) return 0;
		const int longest = std::max(image->Width, image->Height);
		if (maxSize > 0 && longest > maxSize) {
			const float scale = static_cast<float>(maxSize) / static_cast<float>(longest);
			const int width = std::max(1, static_cast<int>(image->Width * scale + 0.5f));
			const int height = std::max(1, static_cast<int>(image->Height * scale + 0.5f));
			std::unique_ptr<ImageData> scaled = ImageProcessing::Resize(*image, width, height);
			if (!scaled) return 0;
			image = std::move(scaled);
		}
		*outWidth = image->Width;
		*outHeight = image->Height;
		const int bytes = image->Width * image->Height * 4;
		if (outRgba && capacity >= bytes) std::memcpy(outRgba, image->Pixels, static_cast<size_t>(bytes));
		return bytes;
	}

	static void Index_Media_SetNowPlaying(const char* title, const char* artist, const char* album, float durationSeconds,
		const uint8_t* rgba, int width, int height)
	{
		Platform::SetMediaNowPlaying(title ? title : "", artist ? artist : "", album ? album : "", durationSeconds, rgba, width, height);
	}

	static void Index_Media_SetPlaybackState(int playing, float positionSeconds) { Platform::SetMediaPlaybackState(playing != 0, positionSeconds); }

	static int Index_Media_PollCommand(float* seekSeconds) {
		float seek = 0.0f;
		const Platform::MediaCommand command = Platform::PollMediaCommand(seek);
		if (seekSeconds) *seekSeconds = seek;
		return static_cast<int>(command);
	}

	static void Index_AudioStream_Close(uint32_t stream) { AudioStreams::Close(stream); }
	static int Index_AudioStream_Play(uint32_t stream) { return AudioStreams::Play(stream) ? 1 : 0; }
	static void Index_AudioStream_Pause(uint32_t stream) { AudioStreams::Pause(stream); }
	static void Index_AudioStream_Stop(uint32_t stream) { AudioStreams::Stop(stream); }
	static int Index_AudioStream_IsPlaying(uint32_t stream) { return AudioStreams::IsPlaying(stream) ? 1 : 0; }
	static int Index_AudioStream_IsAtEnd(uint32_t stream) { return AudioStreams::IsAtEnd(stream) ? 1 : 0; }
	static float Index_AudioStream_GetCursor(uint32_t stream) { return AudioStreams::GetCursorSeconds(stream); }
	static float Index_AudioStream_GetLength(uint32_t stream) { return AudioStreams::GetLengthSeconds(stream); }
	static void Index_AudioStream_Seek(uint32_t stream, float seconds) { AudioStreams::Seek(stream, seconds); }
	static float Index_AudioStream_GetVolume(uint32_t stream) { return AudioStreams::GetVolume(stream); }
	static void Index_AudioStream_SetVolume(uint32_t stream, float volume) { AudioStreams::SetVolume(stream, volume); }
	static int Index_AudioStream_GetLoop(uint32_t stream) { return AudioStreams::IsLooping(stream) ? 1 : 0; }
	static void Index_AudioStream_SetLoop(uint32_t stream, int loop) { AudioStreams::SetLooping(stream, loop != 0); }

	static int Index_Application_GetProcessorCount() {
		unsigned int hw = std::thread::hardware_concurrency();
		return hw > 0 ? static_cast<int>(hw) : 4;
	}

	static float Index_Application_GetFixedDeltaTime() {
		return Time::GetFixedDeltaTime();
	}
	static void Index_Application_SetFixedDeltaTime(float step) {
		// Time::SetFixedDeltaTime clamps to [1/240, 1] and warns on a
		// non-positive step (which would otherwise disable FixedUpdate).
		Time::SetFixedDeltaTime(step);
	}
	static float Index_Application_GetUnscaledDeltaTime() {
		return Time::GetDeltaTimeUnscaled();
	}
	static float Index_Application_GetFixedUnscaledDeltaTime() {
		return Time::GetUnscaledFixedDeltaTime();
	}
	static float Index_Application_GetTimeScale() {
		return Time::GetTimeScale();
	}
	static void Index_Application_SetTimeScale(float scale) {
		Time::SetTimeScale(scale);
	}

	// Two-call buffer pattern: C# calls once with outBuffer=null/capacity=0 to learn the
	// required size, then again with a sized buffer. Stash into the thread-local so the
	// reported size matches the bytes copied on the second call.
	static int Index_Application_GetClipboardStringBuffer(char* outBuffer, int capacity)
	{
		auto* window = Application::GetWindow();
		s_StringReturnBuffer = window ? window->GetClipboardString() : std::string{};

		const int requiredBytes = static_cast<int>(std::min(
			s_StringReturnBuffer.size(),
			static_cast<size_t>(std::numeric_limits<int>::max())));
		if (outBuffer && capacity > 0) {
			const int bytesToCopy = std::min(requiredBytes, capacity - 1);
			if (bytesToCopy > 0) {
				std::memcpy(outBuffer, s_StringReturnBuffer.data(), static_cast<size_t>(bytesToCopy));
			}
			outBuffer[bytesToCopy] = '\0';
		}
		return requiredBytes;
	}

	static void Index_Application_SetClipboardString(const char* value)
	{
		auto* window = Application::GetWindow();
		if (!window) return;
		window->SetClipboardString(value ? std::string(value) : std::string{});
	}

	static int Index_Application_GetVsyncEnabled()
	{
		return Window::IsVsync() ? 1 : 0;
	}

	static void Index_Application_SetVsyncEnabled(int enabled)
	{
		Window::SetVsync(enabled != 0);
	}

	static int Index_Application_GetRunInBackground()
	{
		return Application::GetRunInBackground() ? 1 : 0;
	}

	static void Index_Application_SetRunInBackground(int enabled)
	{
		// The editor must keep ticking while unfocused; only builds honor this.
		if (Application::IsEditor()) return;
		Application::SetRunInBackground(enabled != 0);
	}

	// ── Engine Bindings ─────────────────────────────────────────────────

	// Same two-call buffer pattern as Application_GetClipboardStringBuffer.
	static int CopyToManagedBuffer(std::string_view source, char* outBuffer, int capacity)
	{
		s_StringReturnBuffer.assign(source);
		const int requiredBytes = static_cast<int>(std::min(
			s_StringReturnBuffer.size(),
			static_cast<size_t>(std::numeric_limits<int>::max())));
		if (outBuffer && capacity > 0) {
			const int bytesToCopy = std::min(requiredBytes, capacity - 1);
			if (bytesToCopy > 0) {
				std::memcpy(outBuffer, s_StringReturnBuffer.data(), static_cast<size_t>(bytesToCopy));
			}
			outBuffer[bytesToCopy] = '\0';
		}
		return requiredBytes;
	}

	// Application.DataPath → current project's absolute Assets directory ("<root>/Assets"); empty when no project is loaded.
	static int Index_Application_GetDataPathBuffer(char* outBuffer, int capacity)
	{
		const IndexProject* project = ProjectManager::GetCurrentProject();
		return CopyToManagedBuffer(project ? std::string_view(project->AssetsDirectory) : std::string_view{}, outBuffer, capacity);
	}

	// Application.Name → build Output Name (ExecutableName), falling back to the project name when empty; empty when no project.
	static int Index_Application_GetNameBuffer(char* outBuffer, int capacity)
	{
		const IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			return CopyToManagedBuffer(std::string_view{}, outBuffer, capacity);
		}
		// Bind a reference (not a copy) so the string_view stays valid for the synchronous CopyToManagedBuffer call.
		const std::string& name = project->ExecutableName.empty() ? project->Name : project->ExecutableName;
		return CopyToManagedBuffer(std::string_view(name), outBuffer, capacity);
	}

	// Application.Version → build settings Version; empty when no project.
	static int Index_Application_GetVersionBuffer(char* outBuffer, int capacity)
	{
		const IndexProject* project = ProjectManager::GetCurrentProject();
		return CopyToManagedBuffer(project ? std::string_view(project->BuildVersion) : std::string_view{}, outBuffer, capacity);
	}

	// Application.Company → build settings Company; empty when no project.
	static int Index_Application_GetCompanyBuffer(char* outBuffer, int capacity)
	{
		const IndexProject* project = ProjectManager::GetCurrentProject();
		return CopyToManagedBuffer(project ? std::string_view(project->BuildCompany) : std::string_view{}, outBuffer, capacity);
	}


	static int Index_Window_GetWidth()
	{
		auto* window = Application::GetWindow();
		return window ? window->GetWidth() : 0;
	}

	static int Index_Window_GetHeight()
	{
		auto* window = Application::GetWindow();
		return window ? window->GetHeight() : 0;
	}

	static int Index_Window_GetTitleBuffer(char* outBuffer, int capacity)
	{
		auto* window = Application::GetWindow();
		const std::string title = window ? window->GetTitle() : std::string{};
		return CopyToManagedBuffer(title, outBuffer, capacity);
	}

	// In the editor, scripts share the editor's own window; moving, resizing or fullscreening it would hijack the editor.
	static bool IgnoreWindowChangeInEditor(const char* api, bool& logged)
	{
		if (!Application::IsEditor()) return false;
		if (!logged) {
			logged = true;
			IDX_INFO_TAG("Window", "{} is ignored in the editor; it takes effect in the built game.", api);
		}
		return true;
	}

	static void Index_Window_SetTitle(const char* title)
	{
		static bool s_Logged = false;
		if (IgnoreWindowChangeInEditor("Window.Title", s_Logged)) return;
		auto* window = Application::GetWindow();
		if (!window) return;
		window->SetTitle(title ? std::string(title) : std::string{});
	}

	static void Index_Window_Minimize()
	{
		static bool s_Logged = false;
		if (IgnoreWindowChangeInEditor("Window.Minimize()", s_Logged)) return;
		auto* window = Application::GetWindow();
		if (window) window->MinimizeWindow();
	}

	static void Index_Window_Maximize()
	{
		static bool s_Logged = false;
		if (IgnoreWindowChangeInEditor("Window.Maximize()", s_Logged)) return;
		auto* window = Application::GetWindow();
		if (window) window->MaximizeWindow();
	}

	static void Index_Window_Restore()
	{
		static bool s_Logged = false;
		if (IgnoreWindowChangeInEditor("Window.Restore()", s_Logged)) return;
		auto* window = Application::GetWindow();
		if (window) window->RestoreWindow();
	}

	static int Index_Window_IsMaximized()
	{
		auto* window = Application::GetWindow();
		return (window && window->IsMaximized()) ? 1 : 0;
	}

	static int Index_Window_IsFullScreen()
	{
		auto* window = Application::GetWindow();
		return (window && window->IsFullScreen()) ? 1 : 0;
	}

	static void Index_Window_SetFullScreen(int enabled)
	{
		static bool s_Logged = false;
		if (IgnoreWindowChangeInEditor("Window.IsFullScreen", s_Logged)) return;
		auto* window = Application::GetWindow();
		if (window) window->SetFullScreen(enabled != 0);
	}

	static void Index_Window_GetPosition(int* outX, int* outY)
	{
		auto* window = Application::GetWindow();
		const Vec2Int pos = window ? window->GetPosition() : Vec2Int{ 0, 0 };
		if (outX) *outX = pos.x;
		if (outY) *outY = pos.y;
	}

	static void Index_Window_SetPosition(int x, int y)
	{
		static bool s_Logged = false;
		if (IgnoreWindowChangeInEditor("Window.Position", s_Logged)) return;
		auto* window = Application::GetWindow();
		if (window) window->SetPosition(Vec2Int{ x, y });
	}

	static void Index_Window_Focus()
	{
		auto* window = Application::GetWindow();
		if (window) window->Focus();
	}

	// Desktop mode of the window's display — Window.ScreenCenter derives
	// its value from this (mirroring Window::GetScreenCenter).
	static void Index_Window_GetScreenSize(int* outWidth, int* outHeight)
	{
		auto* window = Application::GetWindow();
		int w = 0, h = 0;
		if (window) {
			if (const std::optional<DisplayMode> mode = window->GetDisplayMode()) {
				w = mode->Width;
				h = mode->Height;
			}
		}
		if (outWidth)  *outWidth  = w;
		if (outHeight) *outHeight = h;
	}

	static int Index_Cursor_GetMode()
	{
		auto* window = Application::GetWindow();
		return window ? window->GetCursorMode() : 0;
	}

	static void Index_Cursor_SetMode(int mode)
	{
		auto* window = Application::GetWindow();
		if (window) window->SetCursorMode(mode);
	}

	static uint64_t Index_Cursor_GetTexture()
	{
		auto* window = Application::GetWindow();
		return window ? window->GetCursorTextureAsset() : 0;
	}

	static void Index_Cursor_SetTexture(uint64_t assetId, int hotspotX, int hotspotY)
	{
		auto* window = Application::GetWindow();
		if (!window) return;

		if (assetId == 0) {
			window->SetCursorImage(nullptr);
			window->SetCursorTextureAsset(0);
			return;
		}

		TextureHandle handle = TextureManager::LoadTextureByUUID(assetId);
		Texture2D* texture = TextureManager::GetTexture(handle);
		if (!texture || !texture->IsValid()) {
			IDX_WARN_TAG("Script", "Cursor texture asset {} could not be loaded", assetId);
			return;
		}

		window->SetCursorImage(texture, { std::max(0, hotspotX), std::max(0, hotspotY) });
		window->SetCursorTextureAsset(assetId);
	}

	static int Index_Engine_GetVersionBuffer(char* outBuffer, int capacity)
	{
		return CopyToManagedBuffer(IDX_VERSION, outBuffer, capacity);
	}

	static int Index_Engine_GetVersionLongBuffer(char* outBuffer, int capacity)
	{
		return CopyToManagedBuffer(Engine::GetVersionLong(), outBuffer, capacity);
	}

	static int Index_Engine_GetBuildConfiguration()
	{
		if (Application::IsEditor()) return 0;
		auto* project = ProjectManager::GetCurrentProject();
		if (project && project->ActiveBuildProfile == IndexProject::BuildProfile::Release) return 2;
		return 1;
	}

	static int Index_Engine_GetPlatformBuffer(char* outBuffer, int capacity)
	{
		return CopyToManagedBuffer(IDX_PLATFORM_NAME, outBuffer, capacity);
	}

	static int Index_Engine_GetGraphicsApiBuffer(char* outBuffer, int capacity)
	{
		// Returns the version string set by the active backend.
		// Empty until the renderer initializes.
		return CopyToManagedBuffer(RenderApi::GetVersionString(), outBuffer, capacity);
	}

	static int Index_Engine_GetGpuVendorBuffer(char* outBuffer, int capacity)
	{
		return CopyToManagedBuffer(RenderApi::GetVendorString(), outBuffer, capacity);
	}

	static int Index_Engine_GetGpuRendererBuffer(char* outBuffer, int capacity)
	{
		return CopyToManagedBuffer(RenderApi::GetRendererString(), outBuffer, capacity);
	}

	// ── Time Bindings ───────────────────────────────────────────────────

	static int Index_Time_GetFrameCount()
	{
		return Time::GetFrameCount();
	}

	static float Index_Time_GetTimeSinceStartup()
	{
		return Time::GetTimeSinceStartup();
	}

	static float Index_Time_GetRealtimeSinceStartup()
	{
		return Time::GetRealtimeSinceStartup();
	}

	// ── Log Bindings ────────────────────────────────────────────────────

	// Guard the managed message pointer: a null const char* converts to std::string_view
	// via std::char_traits::length (strlen) on nullptr -> AV read at 0x0. C# normally
	// coalesces null (InternalCalls.CallStringBinding does `message ??= ""`), but native
	// scripts/packages can call these directly, so never strlen an unchecked boundary pointer.
	static void Index_Log_Trace(const char* message) { Log::PrintMessageTag(Log::Type::Client, Log::Level::Trace, "Script", message ? message : ""); }
	static void Index_Log_Info(const char* message)  { Log::PrintMessageTag(Log::Type::Client, Log::Level::Info, "Script", message ? message : ""); }
	static void Index_Log_Warn(const char* message)  { Log::PrintMessageTag(Log::Type::Client, Log::Level::Warn, "Script", message ? message : ""); }
	static void Index_Log_Error(const char* message) { Log::PrintMessageTag(Log::Type::Client, Log::Level::Error, "Script", message ? message : ""); }

	// ── Input Bindings ──────────────────────────────────────────────────

	static bool CanReadScriptInput()
	{
		auto* app = Application::GetInstance();
		return app && ScriptBindings::IsScriptInputEnabled();
	}

	static int Index_Input_GetKey(int keyCode)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetKey(static_cast<KeyCode>(keyCode)) ? 1 : 0) : 0;
	}

	static int Index_Input_GetKeyDown(int keyCode)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetKeyDown(static_cast<KeyCode>(keyCode)) ? 1 : 0) : 0;
	}

	static int Index_Input_GetKeyUp(int keyCode)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetKeyUp(static_cast<KeyCode>(keyCode)) ? 1 : 0) : 0;
	}

	static int Index_Input_GetAnyKey()
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetAnyKey() ? 1 : 0) : 0;
	}

	static int Index_Input_GetMouseButton(int button)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetMouse(static_cast<MouseButton>(button)) ? 1 : 0) : 0;
	}

	static int Index_Input_GetMouseButtonDown(int button)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetMouseDown(static_cast<MouseButton>(button)) ? 1 : 0) : 0;
	}

	static int Index_Input_GetMouseButtonUp(int button)
	{
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? (app->GetInput().GetMouseUp(static_cast<MouseButton>(button)) ? 1 : 0) : 0;
	}

	static void Index_Input_GetMousePosition(float* outX, float* outY)
	{
		auto* app = Application::GetInstance();
		if (!CanReadScriptInput()) { *outX = 0.0f; *outY = 0.0f; return; }

		const Vec2 pos = app->GetInput().GetMousePosition();
		*outX = pos.x; *outY = pos.y;
	}

	static void Index_Input_GetAxis(float* outX, float* outY) {
		auto* app = Application::GetInstance();
		if (CanReadScriptInput()) { Vec2 axis = app->GetInput().GetAxis(); *outX = axis.x; *outY = axis.y; }
		else { *outX = 0.0f; *outY = 0.0f; }
	}
	static void Index_Input_GetMouseDelta(float* outX, float* outY) {
		auto* app = Application::GetInstance();
		if (CanReadScriptInput()) { Vec2 delta = app->GetInput().GetMouseDelta(); *outX = delta.x; *outY = delta.y; }
		else { *outX = 0.0f; *outY = 0.0f; }
	}
	static float Index_Input_GetScrollWheelDelta() {
		auto* app = Application::GetInstance();
		return CanReadScriptInput() ? app->GetInput().ScrollValue() : 0.0f;
	}

	static int Index_Input_IsGamepadConnected(int gamepadIndex) {
		return Input::IsGamepadConnected(gamepadIndex) ? 1 : 0;
	}
	static int Index_Input_GetGamepadButton(int button, int gamepadIndex) {
		return CanReadScriptInput() && Input::GetGamepadButton(static_cast<GamepadButton>(button), gamepadIndex) ? 1 : 0;
	}
	static int Index_Input_GetGamepadButtonDown(int button, int gamepadIndex) {
		return CanReadScriptInput() && Input::GetGamepadButtonDown(static_cast<GamepadButton>(button), gamepadIndex) ? 1 : 0;
	}
	static int Index_Input_GetGamepadButtonUp(int button, int gamepadIndex) {
		return CanReadScriptInput() && Input::GetGamepadButtonUp(static_cast<GamepadButton>(button), gamepadIndex) ? 1 : 0;
	}
	static float Index_Input_GetGamepadAxis(int axis, int gamepadIndex) {
		return CanReadScriptInput() ? Input::GetGamepadAxis(static_cast<GamepadAxis>(axis), gamepadIndex) : 0.0f;
	}

	// ── Entity Bindings ─────────────────────────────────────────────────

	static int Index_Entity_IsValid(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		return ResolveEntityReference(entityID, scene, handle) ? 1 : 0;
	}

	static uint64_t Index_Entity_FindByName(const char* name)
	{
		Scene* scene = GetScene();
		if (!scene || !name) return 0;
		std::string targetName(name);
		auto& registry = scene->GetRegistry();
		auto view = registry.view<NameComponent>();
		for (auto [entity, nameComp] : view.each())
			if (nameComp.Name == targetName) return GetEntityScriptId(*scene, entity);
		return 0;
	}

	static void Index_Entity_Destroy(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return;
		scene->DestroyEntity(handle);
	}

	static void Index_Entity_DestroyDelayed(uint64_t entityID, float delay)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return;
		scene->DestroyEntity(handle, delay);
	}

	static uint64_t Index_Entity_Create(const char* name)
	{
		Scene* scene = GetScene();
		if (!scene) return 0;
		Entity entity = scene->CreateRuntimeEntity(name ? name : "");
		return GetEntityScriptId(*scene, entity.GetHandle());
	}

	void PopulateNonComponentBindings(NativeBindings& b)
	{
		b.Application_GetDeltaTime = IDX_GUARDED_BINDING(Index_Application_GetDeltaTime);
		b.Application_GetElapsedTime = IDX_GUARDED_BINDING(Index_Application_GetTime);
		b.Application_GetScreenWidth = IDX_GUARDED_BINDING(Index_Application_GetScreenWidth);
		b.Application_GetScreenHeight = IDX_GUARDED_BINDING(Index_Application_GetScreenHeight);
		b.Application_GetSafeAreaInsets = IDX_GUARDED_BINDING(Index_Application_GetSafeAreaInsets);
		b.Application_IsEditor = IDX_GUARDED_BINDING(Index_Application_IsEditor);
		b.Application_GetProcessorCount = IDX_GUARDED_BINDING(Index_Application_GetProcessorCount);
		b.Application_GetTargetFrameRate = IDX_GUARDED_BINDING(Index_Application_GetTargetFrameRate);
		b.Application_SetTargetFrameRate = IDX_GUARDED_BINDING(Index_Application_SetTargetFrameRate);
		b.Application_Quit = IDX_GUARDED_BINDING(Index_Application_Quit);
		b.Application_Reload = IDX_GUARDED_BINDING(Index_Application_Reload);
		b.Application_Restart = IDX_GUARDED_BINDING(Index_Application_Restart);
		b.RenderSettings_Get = IDX_GUARDED_BINDING(Index_RenderSettings_Get);
		b.RenderSettings_Apply = IDX_GUARDED_BINDING(Index_RenderSettings_Apply);
		b.RenderSettings_ResetToDefaults = IDX_GUARDED_BINDING(Index_RenderSettings_ResetToDefaults);
		b.RenderSettings_Get3D = IDX_GUARDED_BINDING(Index_RenderSettings_Get3D);
		b.RenderSettings_Apply3D = IDX_GUARDED_BINDING(Index_RenderSettings_Apply3D);
		b.Application_RequestPermission = IDX_GUARDED_BINDING(Index_Application_RequestPermission);
		b.Application_GetPermissionState = IDX_GUARDED_BINDING(Index_Application_GetPermissionState);
		b.AudioStream_Open = IDX_GUARDED_BINDING(Index_AudioStream_Open);
		b.AudioStream_Close = IDX_GUARDED_BINDING(Index_AudioStream_Close);
		b.AudioStream_Play = IDX_GUARDED_BINDING(Index_AudioStream_Play);
		b.AudioStream_Pause = IDX_GUARDED_BINDING(Index_AudioStream_Pause);
		b.AudioStream_Stop = IDX_GUARDED_BINDING(Index_AudioStream_Stop);
		b.AudioStream_IsPlaying = IDX_GUARDED_BINDING(Index_AudioStream_IsPlaying);
		b.AudioStream_IsAtEnd = IDX_GUARDED_BINDING(Index_AudioStream_IsAtEnd);
		b.AudioStream_GetCursor = IDX_GUARDED_BINDING(Index_AudioStream_GetCursor);
		b.AudioStream_GetLength = IDX_GUARDED_BINDING(Index_AudioStream_GetLength);
		b.AudioStream_Seek = IDX_GUARDED_BINDING(Index_AudioStream_Seek);
		b.AudioStream_GetVolume = IDX_GUARDED_BINDING(Index_AudioStream_GetVolume);
		b.AudioStream_SetVolume = IDX_GUARDED_BINDING(Index_AudioStream_SetVolume);
		b.AudioStream_GetLoop = IDX_GUARDED_BINDING(Index_AudioStream_GetLoop);
		b.AudioStream_SetLoop = IDX_GUARDED_BINDING(Index_AudioStream_SetLoop);
		b.Audio_SetPlayInBackground = IDX_GUARDED_BINDING(Index_Audio_SetPlayInBackground);
		b.Audio_GetPlayInBackground = IDX_GUARDED_BINDING(Index_Audio_GetPlayInBackground);
		b.Image_Decode = IDX_GUARDED_BINDING(Index_Image_Decode);
		b.Media_SetNowPlaying = IDX_GUARDED_BINDING(Index_Media_SetNowPlaying);
		b.Media_SetPlaybackState = IDX_GUARDED_BINDING(Index_Media_SetPlaybackState);
		b.Media_PollCommand = IDX_GUARDED_BINDING(Index_Media_PollCommand);
		b.Application_GetFixedDeltaTime = IDX_GUARDED_BINDING(Index_Application_GetFixedDeltaTime);
		b.Application_SetFixedDeltaTime = IDX_GUARDED_BINDING(Index_Application_SetFixedDeltaTime);
		b.Application_GetUnscaledDeltaTime = IDX_GUARDED_BINDING(Index_Application_GetUnscaledDeltaTime);
		b.Application_GetFixedUnscaledDeltaTime = IDX_GUARDED_BINDING(Index_Application_GetFixedUnscaledDeltaTime);
		b.Application_GetTimeScale = IDX_GUARDED_BINDING(Index_Application_GetTimeScale);
		b.Application_GetNameBuffer = IDX_GUARDED_BINDING(Index_Application_GetNameBuffer);
		b.Application_GetVersionBuffer = IDX_GUARDED_BINDING(Index_Application_GetVersionBuffer);
		b.Application_GetCompanyBuffer = IDX_GUARDED_BINDING(Index_Application_GetCompanyBuffer);
		b.Application_SetTimeScale = IDX_GUARDED_BINDING(Index_Application_SetTimeScale);
		b.Application_GetClipboardStringBuffer = IDX_GUARDED_BINDING(Index_Application_GetClipboardStringBuffer);
		b.Application_SetClipboardString = IDX_GUARDED_BINDING(Index_Application_SetClipboardString);
		b.Application_GetVsyncEnabled = IDX_GUARDED_BINDING(Index_Application_GetVsyncEnabled);
		b.Application_SetVsyncEnabled = IDX_GUARDED_BINDING(Index_Application_SetVsyncEnabled);
		b.Application_GetDataPathBuffer = IDX_GUARDED_BINDING(Index_Application_GetDataPathBuffer);
		b.Application_GetRunInBackground = IDX_GUARDED_BINDING(Index_Application_GetRunInBackground);
		b.Application_SetRunInBackground = IDX_GUARDED_BINDING(Index_Application_SetRunInBackground);

		b.Window_GetWidth = IDX_GUARDED_BINDING(Index_Window_GetWidth);
		b.Window_GetHeight = IDX_GUARDED_BINDING(Index_Window_GetHeight);
		b.Window_GetTitleBuffer = IDX_GUARDED_BINDING(Index_Window_GetTitleBuffer);
		b.Window_SetTitle = IDX_GUARDED_BINDING(Index_Window_SetTitle);
		b.Window_Minimize = IDX_GUARDED_BINDING(Index_Window_Minimize);
		b.Window_Maximize = IDX_GUARDED_BINDING(Index_Window_Maximize);
		b.Window_Restore = IDX_GUARDED_BINDING(Index_Window_Restore);
		b.Window_IsMaximized = IDX_GUARDED_BINDING(Index_Window_IsMaximized);
		b.Window_IsFullScreen = IDX_GUARDED_BINDING(Index_Window_IsFullScreen);
		b.Window_SetFullScreen = IDX_GUARDED_BINDING(Index_Window_SetFullScreen);
		b.Window_GetPosition = IDX_GUARDED_BINDING(Index_Window_GetPosition);
		b.Window_SetPosition = IDX_GUARDED_BINDING(Index_Window_SetPosition);
		b.Window_Focus = IDX_GUARDED_BINDING(Index_Window_Focus);
		b.Window_GetScreenSize = IDX_GUARDED_BINDING(Index_Window_GetScreenSize);
		b.Cursor_GetMode = IDX_GUARDED_BINDING(Index_Cursor_GetMode);
		b.Cursor_SetMode = IDX_GUARDED_BINDING(Index_Cursor_SetMode);
		b.Cursor_GetTexture = IDX_GUARDED_BINDING(Index_Cursor_GetTexture);
		b.Cursor_SetTexture = IDX_GUARDED_BINDING(Index_Cursor_SetTexture);

		b.Engine_GetVersionBuffer = IDX_GUARDED_BINDING(Index_Engine_GetVersionBuffer);
		b.Engine_GetVersionLongBuffer = IDX_GUARDED_BINDING(Index_Engine_GetVersionLongBuffer);
		b.Engine_GetBuildConfiguration = IDX_GUARDED_BINDING(Index_Engine_GetBuildConfiguration);
		b.Engine_GetPlatformBuffer = IDX_GUARDED_BINDING(Index_Engine_GetPlatformBuffer);
		b.Engine_GetGraphicsApiBuffer = IDX_GUARDED_BINDING(Index_Engine_GetGraphicsApiBuffer);
		b.Engine_GetGpuVendorBuffer = IDX_GUARDED_BINDING(Index_Engine_GetGpuVendorBuffer);
		b.Engine_GetGpuRendererBuffer = IDX_GUARDED_BINDING(Index_Engine_GetGpuRendererBuffer);

		b.Time_GetFrameCount = IDX_GUARDED_BINDING(Index_Time_GetFrameCount);
		b.Time_GetTimeSinceStartup = IDX_GUARDED_BINDING(Index_Time_GetTimeSinceStartup);
		b.Time_GetRealtimeSinceStartup = IDX_GUARDED_BINDING(Index_Time_GetRealtimeSinceStartup);

		b.Log_Trace = IDX_GUARDED_BINDING(Index_Log_Trace);
		b.Log_Info = IDX_GUARDED_BINDING(Index_Log_Info);
		b.Log_Warn = IDX_GUARDED_BINDING(Index_Log_Warn);
		b.Log_Error = IDX_GUARDED_BINDING(Index_Log_Error);

		b.Input_GetKey = IDX_GUARDED_BINDING(Index_Input_GetKey);
		b.Input_GetKeyDown = IDX_GUARDED_BINDING(Index_Input_GetKeyDown);
		b.Input_GetKeyUp = IDX_GUARDED_BINDING(Index_Input_GetKeyUp);
		b.Input_GetAnyKey = IDX_GUARDED_BINDING(Index_Input_GetAnyKey);
		b.Input_GetMouseButton = IDX_GUARDED_BINDING(Index_Input_GetMouseButton);
		b.Input_GetMouseButtonDown = IDX_GUARDED_BINDING(Index_Input_GetMouseButtonDown);
		b.Input_GetMouseButtonUp = IDX_GUARDED_BINDING(Index_Input_GetMouseButtonUp);
		b.Input_GetMousePosition = IDX_GUARDED_BINDING(Index_Input_GetMousePosition);
		b.Input_GetAxis = IDX_GUARDED_BINDING(Index_Input_GetAxis);
		b.Input_GetMouseDelta = IDX_GUARDED_BINDING(Index_Input_GetMouseDelta);
		b.Input_GetScrollWheelDelta = IDX_GUARDED_BINDING(Index_Input_GetScrollWheelDelta);
		b.Input_IsGamepadConnected = IDX_GUARDED_BINDING(Index_Input_IsGamepadConnected);
		b.Input_GetGamepadButton = IDX_GUARDED_BINDING(Index_Input_GetGamepadButton);
		b.Input_GetGamepadButtonDown = IDX_GUARDED_BINDING(Index_Input_GetGamepadButtonDown);
		b.Input_GetGamepadButtonUp = IDX_GUARDED_BINDING(Index_Input_GetGamepadButtonUp);
		b.Input_GetGamepadAxis = IDX_GUARDED_BINDING(Index_Input_GetGamepadAxis);

		b.Entity_IsValid = IDX_GUARDED_BINDING(Index_Entity_IsValid);
		b.Entity_FindByName = IDX_GUARDED_BINDING(Index_Entity_FindByName);
		b.Entity_Destroy = IDX_GUARDED_BINDING(Index_Entity_Destroy);
		b.Entity_DestroyDelayed = IDX_GUARDED_BINDING(Index_Entity_DestroyDelayed);
		b.Entity_Create = IDX_GUARDED_BINDING(Index_Entity_Create);
	}

} // namespace Index
