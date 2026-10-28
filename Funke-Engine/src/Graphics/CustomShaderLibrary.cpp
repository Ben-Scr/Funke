#include "pch.hpp"
#include "Graphics/CustomShaderLibrary.hpp"
#include "Graphics/CustomShaderGpu.hpp"

#include "Assets/AssetRegistry.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Graphics/Backend/WebGPUBackend.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/Texture2D.hpp"
#include "Graphics/TextureManager.hpp"
#include "Serialization/File.hpp"
#include "Serialization/FileStamp.hpp"

#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Funke {
	namespace {
		using Clock = std::chrono::steady_clock;

		struct ShaderEntry {
			std::string Path;
			std::string Source;
			// Last header that parsed; Program may lag behind it while a WGSL error is unfixed.
			std::optional<ShaderDescription> Description;
			std::vector<ShaderDiagnostic> Diagnostics;
			std::unique_ptr<CustomShaderProgram> Program;
			bool CompilePending = false;
			std::unordered_set<int> WarnedTargets;
			Clock::time_point LastLoad{};
			Clock::time_point LastPoll{};
		};

		struct CompileMessage {
			int Line = 0;
			std::string Text;
		};

		std::unordered_map<uint64_t, ShaderEntry>& Entries() {
			static std::unordered_map<uint64_t, ShaderEntry> s_Entries;
			return s_Entries;
		}

		std::array<std::unique_ptr<CustomShaderProgram>, 5>& Fallbacks() {
			// One per ShaderTarget.
			static std::array<std::unique_ptr<CustomShaderProgram>, 5> s_Fallbacks;
			return s_Fallbacks;
		}

		std::optional<bool> g_HotReloadOverride;
		uint32_t g_Generation = 1;
		uint64_t g_NextProgramId = 1;

		// Windows editors like to save a UTF-8 BOM, which Tint rejects as an invalid character.
		std::string ReadShaderSource(const std::string& path) {
			std::string text = File::ReadAllText(path);
			if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB
				&& static_cast<unsigned char>(text[2]) == 0xBF) {
				text.erase(0, 3);
			}
			return text;
		}

		std::string FromStringView(wgpu::StringView view) {
			if (!view.data) return {};
			return std::string(view.data, view.length == WGPU_STRLEN ? std::strlen(view.data) : view.length);
		}

		// "Assets/Shaders/Water.wgsl" reads better in the Console than an absolute path.
		std::string DisplayPath(const std::string& path) {
			std::string generic = path;
			std::replace(generic.begin(), generic.end(), '\\', '/');
			const size_t assets = generic.rfind("/Assets/");
			return assets == std::string::npos ? generic : generic.substr(assets + 1);
		}

		bool TargetsMatch(ShaderTarget shader, ShaderTarget requested) {
			auto isQuad = [](ShaderTarget t) { return t == ShaderTarget::Sprite2D || t == ShaderTarget::UI; };
			return shader == requested || (isQuad(shader) && isQuad(requested));
		}

		// Compiles inside a validation error scope so a broken shader neither reaches the uncaptured-error log nor
		// replaces a working module; messages carry the composed line number.
		wgpu::ShaderModule CompileModule(wgpu::Instance instance, wgpu::Device device, const std::string& label,
			const std::string& wgsl, std::vector<CompileMessage>& outErrors)
		{
			if (!device) return nullptr;
			wgpu::ShaderSourceWGSL source{};
			source.code = wgsl.c_str();
			wgpu::ShaderModuleDescriptor desc{};
			desc.nextInChain = &source;
			desc.label = label.c_str();

			device.PushErrorScope(wgpu::ErrorFilter::Validation);
			wgpu::ShaderModule module = device.CreateShaderModule(&desc);
			std::string scopeError;
			wgpu::Future scopeFuture = device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
				[&scopeError](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type, wgpu::StringView message) {
					if (status == wgpu::PopErrorScopeStatus::Success && type != wgpu::ErrorType::NoError) {
						scopeError = FromStringView(message);
					}
				});
			if (instance) {
				wgpu::FutureWaitInfo wait{ scopeFuture };
				instance.WaitAny(1, &wait, UINT64_MAX);
			}
			if (!module) {
				outErrors.push_back({ 0, scopeError.empty() ? "the GPU device could not create the shader module" : scopeError });
				return nullptr;
			}

			if (instance) {
				wgpu::Future infoFuture = module.GetCompilationInfo(wgpu::CallbackMode::WaitAnyOnly,
					[&outErrors](wgpu::CompilationInfoRequestStatus status, const wgpu::CompilationInfo* info) {
						if (status != wgpu::CompilationInfoRequestStatus::Success || !info) return;
						for (size_t i = 0; i < info->messageCount; ++i) {
							const wgpu::CompilationMessage& message = info->messages[i];
							if (message.type != wgpu::CompilationMessageType::Error) continue;
							outErrors.push_back({ static_cast<int>(message.lineNum), FromStringView(message.message) });
						}
					});
				wgpu::FutureWaitInfo wait{ infoFuture };
				instance.WaitAny(1, &wait, UINT64_MAX);
			}
			if (outErrors.empty() && !scopeError.empty()) outErrors.push_back({ 0, scopeError });
			return outErrors.empty() ? module : nullptr;
		}

		void AppendCompileErrors(const ComposedShader& composed, const std::vector<CompileMessage>& errors, std::vector<ShaderDiagnostic>& out) {
			for (const CompileMessage& error : errors) {
				const int userLine = composed.ToUserLine(error.Line);
				std::string text = error.Text;
				if (userLine == 0 && error.Line > 0) {
					text += " (engine template line " + std::to_string(error.Line)
						+ ": check the signatures of fn surface / fn vertex and the names you declared)";
				}
				out.push_back({ userLine, std::move(text), true });
			}
		}

		void LogDiagnostics(const std::string& path, const std::vector<ShaderDiagnostic>& diagnostics) {
			for (const ShaderDiagnostic& diagnostic : diagnostics) {
				const std::string text = CustomShaderLibrary::FormatDiagnostic(DisplayPath(path), diagnostic);
				if (diagnostic.IsError) FNK_ERROR_TAG("Shader", "{}", text);
				else FNK_WARN_TAG("Shader", "{}", text);
			}
		}

		bool HotReloadEnabled() {
			return g_HotReloadOverride.value_or(Application::IsEditor());
		}

		void LoadSource(uint64_t guid, ShaderEntry& entry) {
			entry.LastLoad = Clock::now();
			entry.LastPoll = entry.LastLoad;
			entry.Path = AssetRegistry::ResolvePath(guid);
			if (entry.Path.empty() || !File::Exists(entry.Path)) {
				entry.Diagnostics = { { 0, "shader file not found for GUID " + std::to_string(guid), true } };
				return;
			}
			KnownFileStamps::Record(entry.Path);
			entry.Source = ReadShaderSource(entry.Path);
			ShaderParseResult parsed = ParseCustomShader(entry.Source);
			entry.Diagnostics = parsed.Diagnostics;
			LogDiagnostics(entry.Path, entry.Diagnostics);
			if (parsed.HasErrors()) return;
			entry.Description = std::move(parsed.Description);
			entry.CompilePending = true;
			++g_Generation;
		}

		ShaderEntry* Ensure(uint64_t guid) {
			if (guid == 0) return nullptr;
			auto& entries = Entries();
			auto it = entries.find(guid);
			const Clock::time_point now = Clock::now();
			if (it == entries.end()) {
				it = entries.emplace(guid, ShaderEntry{}).first;
				LoadSource(guid, it->second);
				return &it->second;
			}
			ShaderEntry& entry = it->second;
			if (entry.Path.empty()) {
				// Unknown GUID or missing file: retry once a second (the asset may still be importing).
				if (now - entry.LastLoad > std::chrono::seconds(1)) LoadSource(guid, entry);
			}
			else if (HotReloadEnabled() && now - entry.LastPoll > std::chrono::milliseconds(500)) {
				entry.LastPoll = now;
				if (KnownFileStamps::HasChangedOnDisk(entry.Path)) {
					FNK_INFO_TAG("Shader", "Reloading {}", DisplayPath(entry.Path));
					LoadSource(guid, entry);
				}
			}
			return &entry;
		}

		void CompilePending(uint64_t guid, ShaderEntry& entry) {
			if (!entry.CompilePending || !entry.Description || !WebGPUBackend::IsInitialized()) return;
			entry.CompilePending = false;
			const ComposedShader composed = ComposeCustomShader(*entry.Description, entry.Source);
			std::vector<CompileMessage> errors;
			wgpu::ShaderModule module = CompileModule(WebGPUBackend::GetInstance(), WebGPUBackend::GetDevice(),
				DisplayPath(entry.Path), composed.Wgsl, errors);
			if (!module) {
				std::vector<ShaderDiagnostic> diagnostics;
				AppendCompileErrors(composed, errors, diagnostics);
				LogDiagnostics(entry.Path, diagnostics);
				if (entry.Program) {
					FNK_WARN_TAG("Shader", "{}: keeping the last version that compiled", DisplayPath(entry.Path));
				}
				entry.Diagnostics.insert(entry.Diagnostics.end(), diagnostics.begin(), diagnostics.end());
				return;
			}
			auto program = std::make_unique<CustomShaderProgram>();
			program->Guid = guid;
			program->Id = g_NextProgramId++;
			program->Description = *entry.Description;
			program->Module = std::move(module);
			entry.Program = std::move(program);
			++g_Generation;
		}

		// A private device for `funke validate`, released when the check ends: Dawn objects must not outlive main.
		struct HeadlessDevice {
			wgpu::Instance Instance;
			wgpu::Adapter Adapter;
			wgpu::Device Device;

			bool Create() {
				const wgpu::InstanceFeatureName features[] = { wgpu::InstanceFeatureName::TimedWaitAny };
				wgpu::InstanceDescriptor instanceDesc{};
				instanceDesc.requiredFeatureCount = 1;
				instanceDesc.requiredFeatures = features;
				Instance = wgpu::CreateInstance(&instanceDesc);
				if (!Instance) return false;

				for (wgpu::BackendType backend : { wgpu::BackendType::Null, wgpu::BackendType::Undefined }) {
					wgpu::RequestAdapterOptions options{};
					options.backendType = backend;
					wgpu::Future future = Instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
						[this](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView) {
							if (status == wgpu::RequestAdapterStatus::Success) Adapter = std::move(adapter);
						});
					wgpu::FutureWaitInfo wait{ future };
					Instance.WaitAny(1, &wait, UINT64_MAX);
					if (Adapter) break;
				}
				if (!Adapter) return false;

				wgpu::DeviceDescriptor deviceDesc{};
				deviceDesc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView) {});
				wgpu::Future future = Adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
					[this](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView) {
						if (status == wgpu::RequestDeviceStatus::Success) Device = std::move(device);
					});
				wgpu::FutureWaitInfo wait{ future };
				Instance.WaitAny(1, &wait, UINT64_MAX);
				return static_cast<bool>(Device);
			}

			~HeadlessDevice() {
				Device = nullptr;
				Adapter = nullptr;
				Instance = nullptr;
			}
		};
	}

	const ShaderDescription* CustomShaderLibrary::GetDescription(uint64_t guid) {
		ShaderEntry* entry = Ensure(guid);
		return entry && entry->Description ? &*entry->Description : nullptr;
	}

	std::vector<ShaderDiagnostic> CustomShaderLibrary::GetDiagnostics(uint64_t guid) {
		ShaderEntry* entry = Ensure(guid);
		if (!entry) return {};
		CompilePending(guid, *entry);
		return entry->Diagnostics;
	}

	std::string CustomShaderLibrary::GetPath(uint64_t guid) {
		const ShaderEntry* entry = Ensure(guid);
		return entry ? entry->Path : std::string{};
	}

	void CustomShaderLibrary::SetHotReloadEnabled(bool enabled) {
		g_HotReloadOverride = enabled;
	}

	bool CustomShaderLibrary::IsHotReloadEnabled() {
		return HotReloadEnabled();
	}

	uint32_t CustomShaderLibrary::GetGeneration() {
		return g_Generation;
	}

	void CustomShaderLibrary::Invalidate(uint64_t guid) {
		Entries().erase(guid);
		++g_Generation;
	}

	void CustomShaderLibrary::Clear() {
		Entries().clear();
		for (auto& fallback : Fallbacks()) fallback.reset();
	}

	std::string CustomShaderLibrary::FormatDiagnostic(const std::string& path, const ShaderDiagnostic& diagnostic) {
		std::string text = path;
		if (diagnostic.Line > 0) text += "(" + std::to_string(diagnostic.Line) + ")";
		text += diagnostic.IsError ? ": error: " : ": warning: ";
		text += diagnostic.Message;
		return text;
	}

	bool CustomShaderLibrary::ValidateFile(const std::string& path, std::vector<ShaderDiagnostic>& outDiagnostics, bool compile) {
		if (!File::Exists(path)) {
			outDiagnostics.push_back({ 0, "file not found", true });
			return false;
		}
		const std::string source = ReadShaderSource(path);
		ShaderParseResult parsed = ParseCustomShader(source);
		outDiagnostics.insert(outDiagnostics.end(), parsed.Diagnostics.begin(), parsed.Diagnostics.end());
		if (parsed.HasErrors() || !compile) return !parsed.HasErrors();

		const ComposedShader composed = ComposeCustomShader(parsed.Description, source);
		std::vector<CompileMessage> errors;
		if (WebGPUBackend::IsInitialized()) {
			CompileModule(WebGPUBackend::GetInstance(), WebGPUBackend::GetDevice(), DisplayPath(path), composed.Wgsl, errors);
		}
		else {
			HeadlessDevice headless;
			if (!headless.Create()) {
				outDiagnostics.push_back({ 0, "WGSL not checked: no WebGPU adapter available", false });
				return true;
			}
			CompileModule(headless.Instance, headless.Device, DisplayPath(path), composed.Wgsl, errors);
		}
		AppendCompileErrors(composed, errors, outDiagnostics);
		return errors.empty();
	}

	namespace CustomShaderGpu {
		const CustomShaderProgram* GetFallback(ShaderTarget target) {
			if (!WebGPUBackend::IsInitialized()) return nullptr;
			std::unique_ptr<CustomShaderProgram>& slot = Fallbacks()[static_cast<size_t>(target)];
			if (slot) return slot.get();

			const std::string_view source = GetFallbackShaderSource(target);
			ShaderParseResult parsed = ParseCustomShader(source);
			parsed.Description.Target = target;
			parsed.Description.Blend = target == ShaderTarget::Sprite2D || target == ShaderTarget::UI ? ShaderBlend::Alpha : ShaderBlend::Opaque;
			parsed.Description.Cull = ShaderCull::None;
			parsed.Description.DepthWrite = parsed.Description.Blend == ShaderBlend::Opaque;
			const ComposedShader composed = ComposeCustomShader(parsed.Description, source);
			std::vector<CompileMessage> errors;
			wgpu::ShaderModule module = CompileModule(WebGPUBackend::GetInstance(), WebGPUBackend::GetDevice(),
				std::string("fallback-") + ShaderTargetToString(target), composed.Wgsl, errors);
			if (!module) {
				for (const CompileMessage& error : errors) {
					FNK_CORE_ERROR_TAG("Shader", "Fallback shader for {} failed (line {}): {}", ShaderTargetToString(target), error.Line, error.Text);
				}
				return nullptr;
			}
			slot = std::make_unique<CustomShaderProgram>();
			slot->Id = g_NextProgramId++;
			slot->Description = parsed.Description;
			slot->Module = std::move(module);
			slot->IsFallback = true;
			return slot.get();
		}

		const CustomShaderProgram* GetProgram(uint64_t shaderGuid, ShaderTarget target) {
			if (!WebGPUBackend::IsInitialized()) return nullptr;
			ShaderEntry* entry = Ensure(shaderGuid);
			if (!entry) return GetFallback(target);
			CompilePending(shaderGuid, *entry);
			if (!entry->Program) return GetFallback(target);
			if (!TargetsMatch(entry->Program->Description.Target, target)) {
				if (entry->WarnedTargets.insert(static_cast<int>(target)).second) {
					FNK_WARN_TAG("Shader", "{} targets {} but is used for {}; drawing the fallback",
						DisplayPath(entry->Path), ShaderTargetToString(entry->Program->Description.Target), ShaderTargetToString(target));
				}
				return GetFallback(target);
			}
			return entry->Program.get();
		}

		wgpu::RenderPipeline CreatePipeline(const wgpu::RenderPipelineDescriptor& descriptor, const CustomShaderProgram& program) {
			wgpu::Device device = WebGPUBackend::GetDevice();
			wgpu::Instance instance = WebGPUBackend::GetInstance();
			if (!device) return nullptr;
			device.PushErrorScope(wgpu::ErrorFilter::Validation);
			wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&descriptor);
			std::string error;
			wgpu::Future future = device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
				[&error](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type, wgpu::StringView message) {
					if (status == wgpu::PopErrorScopeStatus::Success && type != wgpu::ErrorType::NoError) error = FromStringView(message);
				});
			if (instance) {
				wgpu::FutureWaitInfo wait{ future };
				instance.WaitAny(1, &wait, UINT64_MAX);
			}
			if (!pipeline || !error.empty()) {
				const auto it = Entries().find(program.Guid);
				const std::string path = it != Entries().end() ? DisplayPath(it->second.Path) : std::string("fallback shader");
				FNK_ERROR_TAG("Shader", "{}: the render pipeline could not be created: {}", path, error.empty() ? "unknown error" : error);
				return nullptr;
			}
			return pipeline;
		}

		wgpu::BlendState MakeBlendState(ShaderBlend blend) {
			wgpu::BlendState state{};
			state.color.operation = wgpu::BlendOperation::Add;
			state.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
			state.color.dstFactor = blend == ShaderBlend::Additive ? wgpu::BlendFactor::One : wgpu::BlendFactor::OneMinusSrcAlpha;
			// Keeps the target's alpha opaque: the editor composites its viewport texture with ImGui.
			state.alpha.operation = wgpu::BlendOperation::Add;
			state.alpha.srcFactor = blend == ShaderBlend::Additive ? wgpu::BlendFactor::Zero : wgpu::BlendFactor::One;
			state.alpha.dstFactor = blend == ShaderBlend::Additive ? wgpu::BlendFactor::One : wgpu::BlendFactor::OneMinusSrcAlpha;
			return state;
		}

		wgpu::CullMode ToCullMode(ShaderCull cull) {
			switch (cull) {
				case ShaderCull::Back: return wgpu::CullMode::Back;
				case ShaderCull::Front: return wgpu::CullMode::Front;
				case ShaderCull::None: break;
			}
			return wgpu::CullMode::None;
		}
	}

	// ── CustomMaterialBindings ──────────────────────────────────────────

	CustomMaterialBindings::~CustomMaterialBindings() {
		Shutdown();
	}

	bool CustomMaterialBindings::Initialize(const char* label, bool sceneSamplers) {
		if (m_GlobalsBuffer) return true;
		wgpu::Device device = WebGPUBackend::GetDevice();
		if (!device) return false;
		m_Label = label ? label : "custom-shader";
		m_SceneSamplers = sceneSamplers;
		wgpu::BufferDescriptor desc{};
		desc.size = sizeof(ShaderGlobals);
		desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
		const std::string bufferLabel = m_Label + "-globals";
		desc.label = bufferLabel.c_str();
		m_GlobalsBuffer = device.CreateBuffer(&desc);
		if (!m_GlobalsBuffer) return false;

		// Bind groups hold texture views; drop them before a view dies.
		m_DestroyToken = TextureManager::AddDestroyListener([this](TextureHandle handle) {
			m_Groups.clear();
			std::erase_if(m_Textures, [handle](const auto& pair) { return pair.second.Handle == handle; });
		});
		return true;
	}

	void CustomMaterialBindings::Shutdown() {
		if (m_DestroyToken != 0) TextureManager::RemoveDestroyListener(m_DestroyToken);
		m_DestroyToken = 0;
		m_Groups.clear();
		m_Layouts.clear();
		m_Textures.clear();
		for (uint64_t& runtime : m_DefaultTextures) {
			if (runtime != 0) TextureManager::DestroyRuntimeTexture(runtime);
			runtime = 0;
		}
		m_GlobalsBuffer = nullptr;
	}

	ShaderGlobals CustomMaterialBindings::MakeGlobals(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition,
		uint32_t targetWidth, uint32_t targetHeight)
	{
		ShaderGlobals globals;
		globals.View = view;
		globals.Projection = projection;
		globals.ViewProjection = projection * view;
		globals.InverseViewProjection = glm::inverse(globals.ViewProjection);
		globals.CameraPosition = glm::vec4(cameraPosition, 1.0f);
		globals.Time = Time::GetElapsedTime();
		globals.DeltaTime = Time::GetUnscaledDeltaTime();
		globals.Frame = static_cast<float>(Time::GetFrameCount());
		const float width = static_cast<float>(std::max(targetWidth, 1u));
		const float height = static_cast<float>(std::max(targetHeight, 1u));
		globals.ScreenSize = glm::vec4(width, height, 1.0f / width, 1.0f / height);
		return globals;
	}

	void CustomMaterialBindings::WriteGlobals(const ShaderGlobals& globals) {
		if (!m_GlobalsBuffer) return;
		WebGPUBackend::GetQueue().WriteBuffer(m_GlobalsBuffer, 0, &globals, sizeof(globals));
	}

	wgpu::BindGroupLayout CustomMaterialBindings::GetLayout(const ShaderDescription& description) {
		const uint32_t textureCount = std::min(description.TextureCount, k_MaxShaderTextureProperties);
		if (auto it = m_Layouts.find(textureCount); it != m_Layouts.end()) return it->second;

		std::vector<wgpu::BindGroupLayoutEntry> entries(2 + textureCount * 2);
		const wgpu::ShaderStage stages = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
		entries[0].binding = 0;
		entries[0].visibility = stages;
		entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
		entries[0].buffer.minBindingSize = sizeof(ShaderGlobals);
		entries[1].binding = 1;
		entries[1].visibility = stages;
		entries[1].buffer.type = wgpu::BufferBindingType::Uniform;
		for (uint32_t i = 0; i < textureCount; ++i) {
			wgpu::BindGroupLayoutEntry& texture = entries[2 + i * 2];
			texture.binding = k_CustomShaderFirstTextureBinding + i * 2;
			texture.visibility = stages;
			texture.texture.sampleType = wgpu::TextureSampleType::Float;
			texture.texture.viewDimension = wgpu::TextureViewDimension::e2D;
			wgpu::BindGroupLayoutEntry& sampler = entries[3 + i * 2];
			sampler.binding = texture.binding + 1;
			sampler.visibility = stages;
			sampler.sampler.type = wgpu::SamplerBindingType::Filtering;
		}
		wgpu::BindGroupLayoutDescriptor desc{};
		desc.entryCount = entries.size();
		desc.entries = entries.data();
		const std::string label = m_Label + "-material-bgl";
		desc.label = label.c_str();
		wgpu::BindGroupLayout layout = WebGPUBackend::GetDevice().CreateBindGroupLayout(&desc);
		if (layout) m_Layouts.emplace(textureCount, layout);
		return layout;
	}

	TextureHandle CustomMaterialBindings::ResolveTexture(uint64_t guid) {
		TextureEntry& entry = m_Textures[guid];
		if (TextureManager::IsValid(entry.Handle)) return entry.Handle;
		const Clock::time_point now = Clock::now();
		// Missing textures are retried once a second instead of hitting the disk every frame.
		if (entry.LastAttempt != Clock::time_point{} && now - entry.LastAttempt < std::chrono::seconds(1)) return TextureHandle{};
		entry.LastAttempt = now;
		entry.Handle = TextureManager::LoadTextureByUUID(guid, Filter::Bilinear, Wrap::Repeat, Wrap::Repeat);
		return entry.Handle;
	}

	TextureHandle CustomMaterialBindings::GetDefaultTexture(ShaderDefaultTexture type) {
		if (type == ShaderDefaultTexture::White) return TextureManager::GetDefaultTexture(DefaultTexture::Pixel);
		uint64_t& runtime = m_DefaultTextures[static_cast<size_t>(type)];
		if (runtime == 0) {
			uint8_t rgba[4] = { 0, 0, 0, 255 };
			if (type == ShaderDefaultTexture::Gray) rgba[0] = rgba[1] = rgba[2] = 128;
			if (type == ShaderDefaultTexture::Normal) { rgba[0] = 128; rgba[1] = 128; rgba[2] = 255; }
			runtime = TextureManager::CreateRuntimeTexture(1, 1, rgba, Filter::Bilinear, Wrap::Repeat, Wrap::Repeat);
		}
		const TextureHandle handle = runtime != 0 ? ResolveTexture(runtime) : TextureHandle{};
		return TextureManager::IsValid(handle) ? handle : TextureManager::GetDefaultTexture(DefaultTexture::Pixel);
	}

	wgpu::BindGroup CustomMaterialBindings::GetGroup(uint64_t materialGuid, const Material* material, const CustomShaderProgram& program) {
		if (!m_GlobalsBuffer) return nullptr;
		wgpu::Device device = WebGPUBackend::GetDevice();
		const ShaderDescription& description = program.Description;
		wgpu::BindGroupLayout layout = GetLayout(description);
		if (!device || !layout) return nullptr;

		static const Material k_DefaultMaterial{};
		const Material& values = material ? *material : k_DefaultMaterial;
		PackMaterialProperties(values, description, m_Scratch);

		const uint32_t textureCount = std::min(description.TextureCount, k_MaxShaderTextureProperties);
		std::array<wgpu::TextureView, k_MaxShaderTextureProperties> views;
		std::array<wgpu::Sampler, k_MaxShaderTextureProperties> samplers;
		std::vector<WGPUTextureView> viewKeys(textureCount, nullptr);
		std::vector<WGPUSampler> samplerKeys(textureCount, nullptr);
		for (const ShaderProperty& property : description.Properties) {
			if (property.Type != ShaderPropertyType::Texture || property.TextureSlot >= textureCount) continue;
			const uint64_t textureGuid = ResolveMaterialProperty(values, property).Texture;
			TextureHandle handle = textureGuid != 0 ? ResolveTexture(textureGuid) : TextureHandle{};
			if (!TextureManager::IsValid(handle)) handle = GetDefaultTexture(property.DefaultTexture);
			Texture2D* texture = TextureManager::GetTexture(handle);
			const auto lookup = texture && texture->IsValid() ? WebGPUBackend::LookupTexture2D(texture->GetHandle()) : WebGPUBackend::TextureLookup{};
			const wgpu::Sampler& sampler = m_SceneSamplers ? lookup.SceneSampler : lookup.Sampler;
			if (!lookup.Valid || !lookup.View || !sampler) return nullptr;
			views[property.TextureSlot] = lookup.View;
			samplers[property.TextureSlot] = sampler;
			viewKeys[property.TextureSlot] = lookup.View.Get();
			samplerKeys[property.TextureSlot] = sampler.Get();
		}

		// Keyed by both ids: one material can be drawn through its shader and a fallback in the same frame.
		const uint64_t key = materialGuid * 0x9E3779B97F4A7C15ull ^ program.Id;
		GroupEntry& entry = m_Groups[key];
		const Clock::time_point now = Clock::now();
		entry.LastUsed = now;
		const bool rebuild = !entry.Group || !entry.Uniforms || entry.Bytes.size() != m_Scratch.size() || entry.Views != viewKeys
			|| entry.Samplers != samplerKeys;
		if (rebuild) {
			if (!entry.Uniforms || entry.Bytes.size() != m_Scratch.size()) {
				wgpu::BufferDescriptor bufferDesc{};
				bufferDesc.size = m_Scratch.size();
				bufferDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
				const std::string bufferLabel = m_Label + "-material-ubo";
				bufferDesc.label = bufferLabel.c_str();
				entry.Uniforms = device.CreateBuffer(&bufferDesc);
				entry.Bytes.clear();
			}
			std::vector<wgpu::BindGroupEntry> entries(2 + textureCount * 2);
			entries[0].binding = 0;
			entries[0].buffer = m_GlobalsBuffer;
			entries[0].size = sizeof(ShaderGlobals);
			entries[1].binding = 1;
			entries[1].buffer = entry.Uniforms;
			entries[1].size = m_Scratch.size();
			for (uint32_t i = 0; i < textureCount; ++i) {
				entries[2 + i * 2].binding = k_CustomShaderFirstTextureBinding + i * 2;
				entries[2 + i * 2].textureView = views[i];
				entries[3 + i * 2].binding = k_CustomShaderFirstTextureBinding + i * 2 + 1;
				entries[3 + i * 2].sampler = samplers[i];
			}
			wgpu::BindGroupDescriptor desc{};
			desc.layout = layout;
			desc.entryCount = entries.size();
			desc.entries = entries.data();
			const std::string groupLabel = m_Label + "-material-bg";
			desc.label = groupLabel.c_str();
			entry.Group = entry.Uniforms ? device.CreateBindGroup(&desc) : nullptr;
			entry.Views = viewKeys;
			entry.Samplers = samplerKeys;
		}
		if (entry.Uniforms && entry.Bytes != m_Scratch) {
			WebGPUBackend::GetQueue().WriteBuffer(entry.Uniforms, 0, m_Scratch.data(), m_Scratch.size());
			entry.Bytes = m_Scratch;
		}

		// Groups of recompiled shaders and removed materials age out instead of piling up.
		if (m_Groups.size() > 64) {
			std::erase_if(m_Groups, [now](const auto& pair) { return now - pair.second.LastUsed > std::chrono::seconds(10); });
		}
		auto it = m_Groups.find(key);
		return it != m_Groups.end() ? it->second.Group : nullptr;
	}

}
