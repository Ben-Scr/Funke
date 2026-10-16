#include "pch.hpp"
#include "Build/GameBuilder.hpp"
#include "Core/EngineModules.hpp"
#include "Serialization/SceneSerializerStorage.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"

#include "Core/Log.hpp"
#include "Editor/BuildAssetPruning.hpp"
#include "Editor/FunkePackageInstaller.hpp"
#include "Editor/PlatformBuilder.hpp"
#include "Graphics/ImageData.hpp"
#include "Graphics/ImageProcessing.hpp"
#include "Project/BuildOutputSafety.hpp"
#include "Project/BuildPlatformSupport.hpp"
#include "Project/SplashAssetResolve.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Process.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <unordered_set>

#include <stb_image_write.h>

#ifdef FNK_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Funke::GameBuilder {
	namespace {
		// Edits only the shipped project.json; the live project keeps its own values.
		bool PatchStagedProjectMember(const std::filesystem::path& projectJson, const char* name, const Json::Value& value) {
			Json::Value root;
			std::string parseError;
			if (!Json::TryParse(File::ReadAllText(Path::ToUtf8(projectJson)), root, &parseError) || !root.IsObject()) {
				FNK_ERROR_TAG("Build", "Staged project.json is not valid JSON: {}", parseError);
				return false;
			}
			if (Json::Value* existing = root.FindMember(name)) *existing = value;
			else root.AddMember(name, value);
			if (!File::WriteAllText(Path::ToUtf8(projectJson), Json::Stringify(root, true))) {
				FNK_ERROR_TAG("Build", "Could not write the staged project.json.");
				return false;
			}
			return true;
		}

		bool WritePng(const std::filesystem::path& path, const ImageData& image) {
			std::vector<std::uint8_t> bytes;
			const auto append = [](void* context, void* data, int length) {
				auto* out = static_cast<std::vector<std::uint8_t>*>(context);
				out->insert(out->end(), static_cast<std::uint8_t*>(data), static_cast<std::uint8_t*>(data) + length);
			};
			if (!stbi_write_png_to_func(append, &bytes, image.Width, image.Height, 4, image.Pixels, image.Width * 4)) return false;
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			return static_cast<bool>(out);
		}

		// Dawn's license tree alone nests past MAX_PATH under ordinary project folders; the \\?\ form lifts the 260-char limit.
		std::filesystem::path ToExtendedLengthPath(const std::filesystem::path& path) {
#ifdef FNK_PLATFORM_WINDOWS
			std::error_code error;
			const std::filesystem::path absolute = std::filesystem::absolute(path, error);
			if (error) return path;
			const std::wstring native = absolute.lexically_normal().make_preferred().native();
			if (native.rfind(L"\\\\?\\", 0) == 0) return path;
			if (native.rfind(L"\\\\", 0) == 0) return std::filesystem::path(L"\\\\?\\UNC\\" + native.substr(2));
			return std::filesystem::path(L"\\\\?\\" + native);
#else
			return path;
#endif
		}

		bool NeedsCopy(const std::filesystem::path& src, const std::filesystem::path& dest) {
			if (!std::filesystem::exists(dest)) return true;
			try {
				return std::filesystem::last_write_time(src) > std::filesystem::last_write_time(dest);
			}
			catch (...) {
				return true;
			}
		}

		struct DirectoryCopyResult {
			int Copied = 0;
			std::string Error;

			bool Succeeded() const { return Error.empty(); }
		};

		DirectoryCopyResult CopyDirIncremental(const std::filesystem::path& srcDirInput,
			const std::filesystem::path& destDirInput)
		{
			DirectoryCopyResult result;
			const std::filesystem::path srcDir = ToExtendedLengthPath(srcDirInput);
			const std::filesystem::path destDir = ToExtendedLengthPath(destDirInput);
			try {
				std::filesystem::create_directories(destDir);
				for (auto& entry : std::filesystem::recursive_directory_iterator(srcDir)) {
					const auto status = entry.symlink_status();
					if (std::filesystem::is_symlink(status)) {
						result.Error = "symbolic links are not supported in build inputs: " + entry.path().string();
						return result;
					}
					auto rel = entry.path().lexically_relative(srcDir);
					if (!BuildOutputSafety::IsSafeRelativePath(rel)) {
						result.Error = "unsafe relative build-input path: " + entry.path().string();
						return result;
					}
					auto dest = destDir / rel;
					if (std::filesystem::is_directory(status)) {
						std::filesystem::create_directories(dest);
					}
					else if (std::filesystem::is_regular_file(status) && NeedsCopy(entry.path(), dest)) {
						std::filesystem::create_directories(dest.parent_path());
						std::filesystem::copy_file(entry.path(), dest, std::filesystem::copy_options::overwrite_existing);
						result.Copied++;
					}
					else if (!std::filesystem::is_regular_file(status)) {
						result.Error = "unsupported build-input entry: " + entry.path().string();
						return result;
					}
				}
			}
			catch (const std::exception& e) {
				result.Error = e.what();
			}
			return result;
		}

		struct FilteredCopyResult : DirectoryCopyResult {
			int Skipped = 0;
			std::uintmax_t SkippedBytes = 0;
		};

		// Folders are created only for files that pass, so a fully filtered folder leaves no empty shell.
		FilteredCopyResult CopyDirIncrementalFiltered(const std::filesystem::path& srcDirInput,
			const std::filesystem::path& destDirInput,
			const std::function<bool(const std::filesystem::path& rel)>& includeFile)
		{
			FilteredCopyResult result;
			const std::filesystem::path srcDir = ToExtendedLengthPath(srcDirInput);
			const std::filesystem::path destDir = ToExtendedLengthPath(destDirInput);
			try {
				std::filesystem::create_directories(destDir);
				for (auto it = std::filesystem::recursive_directory_iterator(srcDir);
					it != std::filesystem::recursive_directory_iterator(); ++it)
				{
					const auto status = it->symlink_status();
					if (std::filesystem::is_symlink(status)) {
						result.Error = "symbolic links are not supported in build inputs: " + it->path().string();
						return result;
					}
					auto rel = it->path().lexically_relative(srcDir);
					if (!BuildOutputSafety::IsSafeRelativePath(rel)) {
						result.Error = "unsafe relative build-input path: " + it->path().string();
						return result;
					}
					if (std::filesystem::is_directory(status)) continue;
					if (std::filesystem::is_regular_file(status) && !includeFile(rel)) {
						std::error_code sizeError;
						const std::uintmax_t size = std::filesystem::file_size(it->path(), sizeError);
						if (!sizeError) result.SkippedBytes += size;
						result.Skipped++;
						continue;
					}
					auto dest = destDir / rel;
					if (std::filesystem::is_regular_file(status) && NeedsCopy(it->path(), dest)) {
						std::filesystem::create_directories(dest.parent_path());
						std::filesystem::copy_file(it->path(), dest, std::filesystem::copy_options::overwrite_existing);
						result.Copied++;
					}
					else if (!std::filesystem::is_regular_file(status)) {
						result.Error = "unsupported build-input entry: " + it->path().string();
						return result;
					}
				}
			}
			catch (const std::exception& e) {
				result.Error = e.what();
			}
			return result;
		}

		class BuildStagingDirectory {
		public:
			explicit BuildStagingDirectory(std::filesystem::path finalPath)
				: m_FinalPath(std::move(finalPath)) {}

			~BuildStagingDirectory() {
				if (m_StagingPath.empty()) return;
				std::error_code error;
				std::filesystem::remove_all(ToExtendedLengthPath(m_StagingPath), error);
			}

			bool Prepare(std::string& errorMessage) {
				std::error_code error;
				std::filesystem::create_directories(m_FinalPath.parent_path(), error);
				if (error) {
					errorMessage = error.message();
					return false;
				}

				m_StagingPath = MakeUniqueSibling("staging");
				std::filesystem::create_directories(m_StagingPath, error);
				if (error) {
					errorMessage = error.message();
					return false;
				}
				std::ofstream marker(m_StagingPath /
					std::string(BuildOutputSafety::OwnershipMarkerFilename), std::ios::trunc);
				marker << "Funke build output\n";
				if (!marker) {
					errorMessage = "failed to create the build ownership marker";
					return false;
				}
				return true;
			}

			bool Publish(std::string& errorMessage) {
				const BuildOutputSafety::OutputPathError ownershipError =
					BuildOutputSafety::ValidateExistingOutput(m_FinalPath);
				if (ownershipError != BuildOutputSafety::OutputPathError::None
					&& !(ownershipError == BuildOutputSafety::OutputPathError::UnownedExistingDirectory
						&& BuildOutputSafety::LooksLikeLegacyFunkeBuildOutput(m_FinalPath))) {
					errorMessage = BuildOutputSafety::GetErrorMessage(ownershipError);
					return false;
				}

				std::error_code error;
				std::filesystem::path backupPath;
				const bool hadExistingOutput = std::filesystem::exists(m_FinalPath, error);
				if (error) {
					errorMessage = error.message();
					return false;
				}

				if (hadExistingOutput) {
					backupPath = MakeUniqueSibling("backup");
					std::filesystem::rename(m_FinalPath, backupPath, error);
					if (error) {
						errorMessage = error.message();
						return false;
					}
				}

				std::filesystem::rename(m_StagingPath, m_FinalPath, error);
				if (error) {
					errorMessage = error.message();
					if (!backupPath.empty()) {
						std::error_code restoreError;
						std::filesystem::rename(backupPath, m_FinalPath, restoreError);
						if (restoreError) {
							errorMessage += "; previous output remains at " + backupPath.string();
						}
					}
					return false;
				}

				m_StagingPath.clear();
				if (!backupPath.empty()) {
					std::filesystem::remove_all(ToExtendedLengthPath(backupPath), error);
					if (error) {
						FNK_WARN_TAG("Build", "Could not remove previous build backup '{}': {}",
							backupPath.string(), error.message());
					}
				}
				return true;
			}

			const std::filesystem::path& GetPath() const { return m_StagingPath; }

		private:
			std::filesystem::path MakeUniqueSibling(std::string_view role) const {
				const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
				const std::string prefix = "." + m_FinalPath.filename().string() + "." + std::string(role)
					+ "-" + std::to_string(token);
				std::filesystem::path candidate = m_FinalPath.parent_path() / prefix;
				for (int suffix = 1; std::filesystem::exists(candidate); ++suffix) {
					candidate = m_FinalPath.parent_path() / (prefix + "-" + std::to_string(suffix));
				}
				return candidate;
			}

			std::filesystem::path m_FinalPath;
			std::filesystem::path m_StagingPath;
		};

		std::string GetRuntimeExecutableFilename() {
#if defined(FNK_PLATFORM_WINDOWS)
			return "Funke-Runtime.exe";
#else
			return "Funke-Runtime";
#endif
		}

		// premake's bin/<cfg>-<system>-<arch> folder (premake/architecture.lua).
		std::string GetRuntimeOutputFolderName(std::string_view configuration) {
#if defined(_M_ARM64) || defined(__aarch64__)
			constexpr std::string_view k_Architecture = "arm64";
#else
			constexpr std::string_view k_Architecture = "x86_64";
#endif
#if defined(FNK_PLATFORM_WINDOWS)
			constexpr std::string_view k_System = "windows";
#elif defined(FNK_PLATFORM_MACOS)
			constexpr std::string_view k_System = "macosx";
#else
			constexpr std::string_view k_System = "linux";
#endif
			return std::string(configuration) + "-" + std::string(k_System) + "-" + std::string(k_Architecture);
		}

		// Prebuilt package binaries live under Bin/<platform>-<arch>, the folder PackageHost probes.
		std::string GetPackageBinaryFolderName() {
			return std::string(FNK_PLATFORM_NAME) + "-" + FNK_ARCH_NAME;
		}

		// A premake --without-<module> workspace that compiles the runtime without the engine modules the
		// project does not use (Funke-Engine/EngineModules.json).
		struct RuntimeVariant {
			std::string Id;
			std::vector<std::string> PremakeOptions;
		};

		std::optional<RuntimeVariant> ResolveRuntimeVariant(const FunkeProject& project) {
			RuntimeVariant variant;
			for (EngineModule module : k_AllEngineModules) {
				const std::string option(EngineModules::GetOption(module));
				if (project.HasModule(EngineModules::GetId(module))) {
					variant.Id += (variant.Id.empty() ? "" : "-") + option;
				}
				else {
					variant.PremakeOptions.push_back("--without-" + option);
				}
			}
			if (variant.PremakeOptions.empty()) return std::nullopt;
			if (variant.Id.empty()) variant.Id = "nomodules";
			return variant;
		}

		// The shipped runtime has no code for modules the project leaves out, so their components would load
		// as nothing; the build refuses instead of shipping a silently broken scene.
		std::vector<std::string> FindDisabledModuleComponents(const std::filesystem::path& assetsDirectory,
			const FunkeProject& project) {
			constexpr size_t k_MaxReported = 20;
			std::vector<std::string> findings;
			const ComponentRegistry& registry = SceneManager::GetComponentRegistryOrBuiltIn();
			std::error_code ec;
			for (std::filesystem::recursive_directory_iterator it(assetsDirectory, ec), end; !ec && it != end; it.increment(ec)) {
				if (!it->is_regular_file(ec)) continue;
				const std::string extension = it->path().extension().string();
				if (extension != ".scene" && extension != ".prefab") continue;

				Json::Value root;
				if (!SceneSerializerStorage::ReadRootFromFile(Path::ToUtf8(it->path()), root)) continue;
				const Json::Value* entities = root.FindMember("entities");
				if (!entities) entities = root.FindMember("Entities");
				if (!entities || !entities->IsArray()) continue;

				for (const Json::Value& entity : entities->GetArray()) {
					if (!entity.IsObject()) continue;
					for (const auto& [memberName, memberValue] : entity.GetObject()) {
						(void)memberValue;
						const ComponentInfo* info = registry.FindBySerializedName(memberName);
						if (!info || !info->engineModule) continue;
						const std::string_view moduleId = EngineModules::GetId(*info->engineModule);
						if (project.HasModule(moduleId)) continue;
						const Json::Value* name = entity.FindMember("name");
						findings.push_back(Path::ToGenericUtf8(std::filesystem::relative(it->path(), assetsDirectory, ec))
							+ ": entity '" + (name ? name->AsStringOr() : std::string{}) + "' uses " + memberName
							+ " (" + std::string(moduleId) + ")");
						if (findings.size() >= k_MaxReported) return findings;
					}
				}
			}
			return findings;
		}

		std::filesystem::path GetRuntimeVariantWorkspace(const std::filesystem::path& engineRoot, const RuntimeVariant& variant) {
			return engineRoot / "premake" / "generated" / ("variant-" + variant.Id);
		}

		// Regenerated on every build: cheap, and it picks up engine sources added since the last one.
		bool GenerateRuntimeVariantWorkspace(const std::filesystem::path& engineRoot, const RuntimeVariant& variant) {
			const std::string premakePath = FunkeProject::GetPremakePath();
			if (premakePath.empty()) {
				FNK_WARN_TAG("Build", "premake5 was not found; cannot generate the '{}' runtime variant.", variant.Id);
				return false;
			}
			std::vector<std::string> command{ premakePath };
			command.insert(command.end(), variant.PremakeOptions.begin(), variant.PremakeOptions.end());
#if defined(FNK_PLATFORM_WINDOWS)
			command.push_back("vs2022");
#else
			command.push_back("gmake2");
#endif
			const Process::Result generation = Process::Run(command, engineRoot.string(), std::chrono::minutes(2));
			if (!generation.Succeeded()) {
				FNK_WARN_TAG("Build", "Generating the '{}' runtime variant failed (exit code {}): {}",
					variant.Id, generation.ExitCode, generation.Output);
				return false;
			}
			return true;
		}

#if !defined(FNK_PLATFORM_WINDOWS)
		// The engine's own gmake2 workspace (premake5 gmake2), or a runtime variant's; config names are lower case there.
		Process::Result RunEngineMake(const std::filesystem::path& makeDirectory, std::string_view configuration,
			const std::vector<std::string>& targets) {
			std::string config(configuration);
			std::transform(config.begin(), config.end(), config.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::vector<std::string> command{ "make", "-C", Path::ToUtf8(makeDirectory), "config=" + config,
				"-j" + std::to_string(std::max(1u, std::thread::hardware_concurrency() / 2)) };
			command.insert(command.end(), targets.begin(), targets.end());
			return Process::Run(command, makeDirectory, std::chrono::minutes(30));
		}

		bool HasEngineMakefile(const std::filesystem::path& makeDirectory) {
			std::error_code ec;
			return !makeDirectory.empty() && std::filesystem::is_regular_file(makeDirectory / "Makefile", ec);
		}
#endif

#if defined(FNK_PLATFORM_MACOS)
		std::string XmlEscape(std::string_view text) {
			std::string out;
			for (char c : text) {
				switch (c) {
					case '&': out += "&amp;"; break;
					case '<': out += "&lt;"; break;
					case '>': out += "&gt;"; break;
					case '"': out += "&quot;"; break;
					default: out += c; break;
				}
			}
			return out;
		}

		// AppIcon.icns from the staged icon through iconutil; false leaves the bundle with the default icon.
		bool WriteIcns(const ImageData& icon, const std::filesystem::path& workDirectory, const std::filesystem::path& icnsPath) {
			const std::filesystem::path iconset = workDirectory / "AppIcon.iconset";
			std::error_code ec;
			std::filesystem::remove_all(iconset, ec);
			std::filesystem::create_directories(iconset, ec);
			for (const int size : { 16, 32, 128, 256, 512 }) {
				for (const int scale : { 1, 2 }) {
					std::unique_ptr<ImageData> image = ImageProcessing::FitSquare(icon, size * scale);
					const std::string name = "icon_" + std::to_string(size) + "x" + std::to_string(size)
						+ (scale == 2 ? "@2x" : "") + ".png";
					if (!image || !WritePng(iconset / name, *image)) return false;
				}
			}
			const Process::Result result = Process::Run({ "iconutil", "-c", "icns", Path::ToUtf8(iconset),
				"-o", Path::ToUtf8(icnsPath) }, {}, std::chrono::seconds(60));
			std::filesystem::remove_all(iconset, ec);
			return result.Succeeded();
		}

		// Moves the flat desktop output into <Name>.app: executable and dylibs in Contents/MacOS (the
		// runtime's @executable_path rpath), everything else in Contents/Resources, which
		// SDL_GetBasePath reports as the content root inside a bundle.
		bool CreateMacAppBundle(const std::filesystem::path& outDir, const std::string& executableName,
			const FunkeProject& project, const ImageData* icon, std::filesystem::path& outBundleExecutable,
			std::string& outError) {
			const std::filesystem::path bundle = outDir / (executableName + ".app");
			const std::filesystem::path macosDir = bundle / "Contents" / "MacOS";
			const std::filesystem::path resourcesDir = bundle / "Contents" / "Resources";
			std::error_code ec;
			std::filesystem::create_directories(macosDir, ec);
			if (!ec) std::filesystem::create_directories(resourcesDir, ec);
			if (ec) {
				outError = "cannot create " + Path::ToUtf8(bundle) + ": " + ec.message();
				return false;
			}

			std::vector<std::filesystem::path> entries;
			for (const auto& entry : std::filesystem::directory_iterator(outDir, ec)) {
				if (entry.path() != bundle) entries.push_back(entry.path());
			}
			if (ec) {
				outError = "cannot list " + Path::ToUtf8(outDir) + ": " + ec.message();
				return false;
			}
			for (const std::filesystem::path& entry : entries) {
				const std::string name = Path::ToUtf8(entry.filename());
				const bool isBinary = name == executableName || entry.extension() == ".dylib";
				std::filesystem::rename(entry, (isBinary ? macosDir : resourcesDir) / entry.filename(), ec);
				if (ec) {
					outError = "cannot move " + name + " into the app bundle: " + ec.message();
					return false;
				}
			}

			const bool hasIcon = icon && WriteIcns(*icon, outDir, resourcesDir / "AppIcon.icns");
			if (icon && !hasIcon) FNK_WARN_TAG("Build", "Could not create AppIcon.icns (iconutil); the app keeps the default icon.");

			const std::string version = project.BuildVersion.empty() ? std::string("1.0.0") : project.BuildVersion;
			std::string plist =
				"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
				"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
				"<plist version=\"1.0\">\n<dict>\n"
				"\t<key>CFBundleDevelopmentRegion</key><string>en</string>\n"
				"\t<key>CFBundleExecutable</key><string>" + XmlEscape(executableName) + "</string>\n"
				"\t<key>CFBundleIdentifier</key><string>" + XmlEscape(project.GetApplicationIdentifier()) + "</string>\n"
				"\t<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>\n"
				"\t<key>CFBundleName</key><string>" + XmlEscape(project.Name) + "</string>\n"
				"\t<key>CFBundleDisplayName</key><string>" + XmlEscape(project.Name) + "</string>\n"
				"\t<key>CFBundlePackageType</key><string>APPL</string>\n"
				"\t<key>CFBundleShortVersionString</key><string>" + XmlEscape(version) + "</string>\n"
				"\t<key>CFBundleVersion</key><string>" + XmlEscape(version) + "</string>\n"
				"\t<key>LSApplicationCategoryType</key><string>public.app-category.games</string>\n"
				"\t<key>LSMinimumSystemVersion</key><string>11.0</string>\n"
				"\t<key>NSHighResolutionCapable</key><true/>\n"
				"\t<key>NSPrincipalClass</key><string>NSApplication</string>\n";
			if (hasIcon) plist += "\t<key>CFBundleIconFile</key><string>AppIcon</string>\n";
			plist += "</dict>\n</plist>\n";
			if (!File::WriteAllText(Path::ToUtf8(bundle / "Contents" / "Info.plist"), plist)) {
				outError = "cannot write Info.plist";
				return false;
			}

			// Apple Silicon refuses unsigned code; an ad-hoc signature seals the bundle after the moves.
			const Process::Result sign = Process::Run({ "codesign", "--force", "--deep", "--sign", "-",
				Path::ToUtf8(bundle) }, {}, std::chrono::minutes(2));
			if (!sign.Succeeded()) FNK_WARN_TAG("Build", "Ad-hoc code signing failed: {}", sign.Output);

			outBundleExecutable = macosDir / executableName;
			return true;
		}
#endif

#ifdef FNK_PLATFORM_WINDOWS
		// Multi-size ICO so Explorer, taskbar and title bar each get a crisp image instead of scaling one.
		// Entries below 256 are 32-bit DIBs ([BITMAPINFOHEADER, biHeight=2*H][BGRA bottom-up][zero AND mask]); 256 is PNG-compressed.
		std::string BuildIcoBytesFromImage(const ImageData& source, std::vector<std::uint8_t>& outIcoBytes)
		{
#pragma pack(push, 1)
			struct IconDirHdr {
				std::uint16_t Reserved;
				std::uint16_t Type;
				std::uint16_t Count;
			};
			struct IcoEntryHdr {
				std::uint8_t Width;
				std::uint8_t Height;
				std::uint8_t ColorCount;
				std::uint8_t Reserved;
				std::uint16_t Planes;
				std::uint16_t BitCount;
				std::uint32_t BytesInRes;
				std::uint32_t ImageOffset;
			};
			struct BitmapInfoHdr {
				std::uint32_t biSize;
				std::int32_t  biWidth;
				std::int32_t  biHeight;
				std::uint16_t biPlanes;
				std::uint16_t biBitCount;
				std::uint32_t biCompression;
				std::uint32_t biSizeImage;
				std::int32_t  biXPelsPerMeter;
				std::int32_t  biYPelsPerMeter;
				std::uint32_t biClrUsed;
				std::uint32_t biClrImportant;
			};
#pragma pack(pop)
			constexpr int k_Sizes[] = { 16, 20, 24, 32, 40, 48, 64, 96, 128, 256 };

			std::vector<std::vector<std::uint8_t>> payloads;
			for (int size : k_Sizes) {
				std::unique_ptr<ImageData> image = ImageProcessing::FitSquare(source, size);
				if (!image) return "failed to resample icon to " + std::to_string(size) + "px";

				std::vector<std::uint8_t> payload;
				if (size == 256) {
					const auto append = [](void* context, void* data, int length) {
						auto* out = static_cast<std::vector<std::uint8_t>*>(context);
						out->insert(out->end(), static_cast<std::uint8_t*>(data), static_cast<std::uint8_t*>(data) + length);
					};
					if (!stbi_write_png_to_func(append, &payload, size, size, 4, image->Pixels, size * 4)) {
						return "failed to encode 256px icon";
					}
				}
				else {
					const std::uint32_t pixelBytes = static_cast<std::uint32_t>(size) * size * 4u;
					// AND mask: 1 bit per pixel, rows padded to 4 bytes; all zero = "use the alpha channel".
					const std::uint32_t maskBytes = ((static_cast<std::uint32_t>(size) + 31u) / 32u) * 4u * size;
					payload.assign(sizeof(BitmapInfoHdr) + pixelBytes + maskBytes, 0);
					BitmapInfoHdr bih{};
					bih.biSize = sizeof(BitmapInfoHdr);
					bih.biWidth = size;
					bih.biHeight = size * 2;
					bih.biPlanes = 1;
					bih.biBitCount = 32;
					bih.biSizeImage = pixelBytes + maskBytes;
					std::memcpy(payload.data(), &bih, sizeof(bih));
					std::uint8_t* dst = payload.data() + sizeof(BitmapInfoHdr);
					for (int y = 0; y < size; ++y) {
						const std::uint8_t* srcRow = image->Pixels + static_cast<size_t>(size - 1 - y) * size * 4;
						std::uint8_t* dstRow = dst + static_cast<size_t>(y) * size * 4;
						for (int x = 0; x < size; ++x) {
							dstRow[x * 4 + 0] = srcRow[x * 4 + 2];
							dstRow[x * 4 + 1] = srcRow[x * 4 + 1];
							dstRow[x * 4 + 2] = srcRow[x * 4 + 0];
							dstRow[x * 4 + 3] = srcRow[x * 4 + 3];
						}
					}
				}
				payloads.push_back(std::move(payload));
			}

			const std::size_t headerBytes = sizeof(IconDirHdr) + payloads.size() * sizeof(IcoEntryHdr);
			outIcoBytes.assign(headerBytes, 0);
			IconDirHdr dir{ 0, 1, static_cast<std::uint16_t>(payloads.size()) };
			std::memcpy(outIcoBytes.data(), &dir, sizeof(dir));
			for (std::size_t i = 0; i < payloads.size(); ++i) {
				IcoEntryHdr entry{};
				entry.Width = static_cast<std::uint8_t>(k_Sizes[i] == 256 ? 0 : k_Sizes[i]);
				entry.Height = entry.Width;
				entry.Planes = 1;
				entry.BitCount = 32;
				entry.BytesInRes = static_cast<std::uint32_t>(payloads[i].size());
				entry.ImageOffset = static_cast<std::uint32_t>(outIcoBytes.size());
				std::memcpy(outIcoBytes.data() + sizeof(IconDirHdr) + i * sizeof(IcoEntryHdr), &entry, sizeof(entry));
				outIcoBytes.insert(outIcoBytes.end(), payloads[i].begin(), payloads[i].end());
			}
			return std::string{};
		}

		// Read an existing .ico file straight into a byte vector. Used when
		// the user supplied a real .ico (no decode/re-encode needed).
		std::string ReadIcoFileBytes(const std::filesystem::path& icoPath,
			std::vector<std::uint8_t>& outIcoBytes)
		{
			std::ifstream in(icoPath, std::ios::binary);
			if (!in) return "open .ico failed";
			outIcoBytes.assign((std::istreambuf_iterator<char>(in)),
				std::istreambuf_iterator<char>());
			return std::string{};
		}

		struct ExistingResName {
			bool IsNumeric = false;
			WORD NumericId = 0;
			std::string StringName;
		};

		std::vector<ExistingResName> EnumerateExistingResources(
			const std::string& exePath, WORD resType, std::string& outError)
		{
			std::vector<ExistingResName> out;
			HMODULE hMod = ::LoadLibraryExA(exePath.c_str(), nullptr,
				LOAD_LIBRARY_AS_DATAFILE);
			if (!hMod) {
				const DWORD err = ::GetLastError();
				outError = "LoadLibraryEx failed (error " +
					std::to_string(err) + ")";
				return out;
			}
			struct Ctx { std::vector<ExistingResName>* List; };
			Ctx ctx{ &out };
			auto cb = [](HMODULE, LPCSTR, LPSTR lpName, LONG_PTR lParam) -> BOOL {
				auto* c = reinterpret_cast<Ctx*>(lParam);
				ExistingResName r{};
				if ((reinterpret_cast<ULONG_PTR>(lpName) >> 16) == 0) {
					r.IsNumeric = true;
					r.NumericId = static_cast<WORD>(reinterpret_cast<ULONG_PTR>(lpName) & 0xFFFF);
				}
				else {
					r.IsNumeric = false;
					r.StringName = lpName;
				}
				c->List->push_back(std::move(r));
				return TRUE;
			};
			if (!::EnumResourceNamesA(hMod, MAKEINTRESOURCEA(resType),
				cb, reinterpret_cast<LONG_PTR>(&ctx)))
			{
				const DWORD err = ::GetLastError();
				// ERROR_RESOURCE_TYPE_NOT_FOUND (1813) just means the .exe
				// has no resources of this type — return an empty list.
				if (err != ERROR_RESOURCE_TYPE_NOT_FOUND
					&& err != ERROR_RESOURCE_DATA_NOT_FOUND)
				{
					outError = "EnumResourceNames failed (error " +
						std::to_string(err) + ")";
				}
			}
			::FreeLibrary(hMod);
			return out;
		}

		// Retries up to 6 times on error 1359/32/33 and BeginUpdateResource failures — AV/Explorer hold the
		// freshly copied exe open transiently; polling fixes the race without permanently failing the Build.
		std::string EmbedIconIntoExecutableOnce(const std::string& exePath,
			const std::vector<std::uint8_t>& icoBytes);

		std::string EmbedIconIntoExecutable(const std::string& exePath,
			const std::vector<std::uint8_t>& icoBytes)
		{
			constexpr int k_MaxAttempts = 6;
			constexpr int k_BaseDelayMs = 150;
			std::string lastErr;
			for (int attempt = 0; attempt < k_MaxAttempts; ++attempt) {
				lastErr = EmbedIconIntoExecutableOnce(exePath, icoBytes);
				if (lastErr.empty()) return {};
				// Only retry on the well-known transient failure modes;
				// other errors (malformed .ico, ico-out-of-bounds, etc.)
				// won't fix themselves with a delay.
				const bool transient = lastErr.find("error 1359") != std::string::npos
					|| lastErr.find("error 32") != std::string::npos       // ERROR_SHARING_VIOLATION
					|| lastErr.find("error 33") != std::string::npos       // ERROR_LOCK_VIOLATION
					|| lastErr.find("BeginUpdateResource") != std::string::npos;
				if (!transient) return lastErr;
				::Sleep(static_cast<DWORD>(k_BaseDelayMs * (attempt + 1)));
			}
			return lastErr.empty() ? std::string("icon embed retry exhausted") : lastErr;
		}

		std::string EmbedIconIntoExecutableOnce(const std::string& exePath,
			const std::vector<std::uint8_t>& icoBytes)
		{
#pragma pack(push, 1)
			struct IconDir {
				std::uint16_t Reserved;
				std::uint16_t Type;
				std::uint16_t Count;
			};
			struct IcoEntry {
				std::uint8_t Width;
				std::uint8_t Height;
				std::uint8_t ColorCount;
				std::uint8_t Reserved;
				std::uint16_t Planes;
				std::uint16_t BitCount;
				std::uint32_t BytesInRes;
				std::uint32_t ImageOffset;
			};
			// RT_GROUP_ICON wire format: trailing field is a 16-bit resource ID (not a 32-bit offset), so 14 bytes/entry; pack to avoid loader-rejected padding.
			struct GroupEntry {
				std::uint8_t Width;
				std::uint8_t Height;
				std::uint8_t ColorCount;
				std::uint8_t Reserved;
				std::uint16_t Planes;
				std::uint16_t BitCount;
				std::uint32_t BytesInRes;
				std::uint16_t Id;
			};
#pragma pack(pop)

			if (icoBytes.size() < sizeof(IconDir)) return "ico too small";

			IconDir dir{};
			std::memcpy(&dir, icoBytes.data(), sizeof(dir));
			if (dir.Type != 1 || dir.Count == 0) return "not a .ico";

			const std::size_t entriesEnd = sizeof(IconDir)
				+ static_cast<std::size_t>(dir.Count) * sizeof(IcoEntry);
			if (icoBytes.size() < entriesEnd) return "ico header truncated";

			std::vector<IcoEntry> entries(dir.Count);
			std::memcpy(entries.data(), icoBytes.data() + sizeof(IconDir),
				entries.size() * sizeof(IcoEntry));

			// Snapshot pre-existing icon ids BEFORE opening the update handle
			// (LoadLibraryEx + UpdateResource on the same file have to be
			// sequential). We only need these to choose fresh RT_ICON ids.
			std::string enumErr;
			std::vector<ExistingResName> existingIcons =
				EnumerateExistingResources(exePath, 3 /*RT_ICON*/, enumErr);
			if (!enumErr.empty()) return enumErr;

			HANDLE hUpdate = ::BeginUpdateResourceA(exePath.c_str(), FALSE);
			if (!hUpdate) {
				return "BeginUpdateResource failed (error " +
					std::to_string(::GetLastError()) + ")";
			}

			// Avoid the delete-then-add pattern: updating without deleting is more reliable on freshly copied exes.
			std::unordered_set<WORD> usedIconIds;
			for (const ExistingResName& r : existingIcons) {
				if (r.IsNumeric) usedIconIds.insert(r.NumericId);
			}
			std::uint32_t nextIconId = 256;
			std::vector<GroupEntry> groupEntries(dir.Count);
			for (std::size_t i = 0; i < entries.size(); ++i) {
				const IcoEntry& e = entries[i];
				if (e.ImageOffset + e.BytesInRes > icoBytes.size()) {
					::EndUpdateResourceA(hUpdate, TRUE);
					return "ico entry out of bounds";
				}
				while (nextIconId <= 0xFFFFu
					&& usedIconIds.find(static_cast<WORD>(nextIconId)) != usedIconIds.end())
				{
					++nextIconId;
				}
				if (nextIconId > 0xFFFFu) {
					::EndUpdateResourceA(hUpdate, TRUE);
					return "too many icon resources";
				}
				const WORD iconId = static_cast<WORD>(nextIconId++);
				usedIconIds.insert(iconId);
				// Use MAKEINTRESOURCEA (not RT_ICON macro): RT_ICON expands to WIDE under UNICODE, mismatching UpdateResourceA's LPCSTR.
				if (!::UpdateResourceA(hUpdate,
					MAKEINTRESOURCEA(3),
					MAKEINTRESOURCEA(iconId),
					MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
					const_cast<std::uint8_t*>(icoBytes.data()) + e.ImageOffset,
					e.BytesInRes))
				{
					const DWORD err = ::GetLastError();
					::EndUpdateResourceA(hUpdate, TRUE);
					return "UpdateResource(RT_ICON) failed (error " +
						std::to_string(err) + ")";
				}
				groupEntries[i] = GroupEntry{
					e.Width, e.Height, e.ColorCount, e.Reserved,
					e.Planes, e.BitCount, e.BytesInRes, iconId,
				};
			}

			std::vector<std::uint8_t> groupBlob(sizeof(IconDir)
				+ groupEntries.size() * sizeof(GroupEntry));
			std::memcpy(groupBlob.data(), &dir, sizeof(IconDir));
			std::memcpy(groupBlob.data() + sizeof(IconDir),
				groupEntries.data(),
				groupEntries.size() * sizeof(GroupEntry));

			// Group ID 1 matches Funke-Runtime/icon.rc. Overwriting that group
			// makes the runtime point at the fresh icon resources above.
			if (!::UpdateResourceA(hUpdate,
				MAKEINTRESOURCEA(14),
				MAKEINTRESOURCEA(1),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
				groupBlob.data(),
				static_cast<DWORD>(groupBlob.size())))
			{
				const DWORD err = ::GetLastError();
				::EndUpdateResourceA(hUpdate, TRUE);
				return "UpdateResource(RT_GROUP_ICON) failed (error " +
					std::to_string(err) + ")";
			}

			if (!::EndUpdateResourceA(hUpdate, FALSE)) {
				return "EndUpdateResource failed (error " +
					std::to_string(::GetLastError()) + ")";
			}
			return std::string{};
		}
#endif

		std::string GetEngineRuntimeFilename() {
#if defined(FNK_PLATFORM_WINDOWS)
			return "Funke-Engine.dll";
#elif defined(__APPLE__)
			return "libIndex-Engine.dylib";
#else
			return "libIndex-Engine.so";
#endif
		}

#if defined(FNK_PLATFORM_WINDOWS)
		std::string GetNetHostRuntimeFilename() {
			return "nethost.dll";
		}
#endif

		// Filename of a premake `kind "SharedLib"` target on the current platform.
		// Windows: <Name>.dll, Linux: lib<Name>.so, macOS: lib<Name>.dylib.
		// Matches the SONAME / install name the runtime links against (see premake/dependencies/sdl3.lua).
		std::string SDL3SharedLibraryFilename() {
#if defined(FNK_PLATFORM_WINDOWS)
			return "SDL3.dll";
#elif defined(FNK_PLATFORM_MACOS)
			return "libSDL3.0.dylib";
#else
			return "libSDL3.so.0";
#endif
		}

		std::string SharedLibraryFilename(std::string_view projectName) {
#if defined(FNK_PLATFORM_WINDOWS)
			return std::string(projectName) + ".dll";
#elif defined(__APPLE__)
			return "lib" + std::string(projectName) + ".dylib";
#else
			return "lib" + std::string(projectName) + ".so";
#endif
		}

		std::string BuildManagedDefinesForSnapshot(
			const FunkeProject& project,
			std::string_view primarySymbol,
			std::string_view configuration,
			BuildPlatform targetPlatform)
		{
			std::string defines(primarySymbol);
			const auto append = [&](std::string_view value) {
				if (value.empty()) return;
				if (!defines.empty()) defines += "%3B";
				defines.append(value);
			};
			if (configuration == "Debug") append("FNK_DEBUG");
			else if (configuration == "Release") append("FNK_RELEASE");
			else if (configuration == "Dist") append("FNK_DIST");
			append(project.GetActiveBuildProfileDefine());
			append(FunkeProject::GetManagedPlatformDefine(targetPlatform));
			for (const std::string& customDefine : project.CustomDefines) append(customDefine);
			return defines;
		}
	}

	std::vector<FunkeBuildProfile> LoadProfiles(const FunkeProject& project) {
		std::vector<FunkeBuildProfile> profiles;
		const std::string extension(FunkeBuildProfile::FileExtension);
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(Path::FromUtf8(project.GetBuildProfilesDirectory()),
				 std::filesystem::directory_options::skip_permission_denied, ec)) {
			if (!entry.is_regular_file(ec) || ec || entry.path().extension().string() != extension) {
				ec.clear();
				continue;
			}
			FunkeBuildProfile profile = FunkeBuildProfile::Load(Path::ToUtf8(entry.path()));
			// The file name, not the "name" field, is what ActiveBuildProfileName refers to.
			profile.Name = Path::ToUtf8(entry.path().stem());
			profiles.push_back(std::move(profile));
		}
		std::sort(profiles.begin(), profiles.end(),
			[](const FunkeBuildProfile& a, const FunkeBuildProfile& b) { return a.Name < b.Name; });
		return profiles;
	}

	std::string ResolveProfileName(const FunkeProject& project, std::string_view requested,
		std::optional<BuildPlatform> platform, std::string& outError)
	{
		const std::vector<FunkeBuildProfile> profiles = LoadProfiles(project);
		const auto findByName = [&](std::string_view name) -> const FunkeBuildProfile* {
			for (const FunkeBuildProfile& profile : profiles) {
				if (profile.Name == name) return &profile;
			}
			return nullptr;
		};

		if (!requested.empty()) {
			const FunkeBuildProfile* profile = findByName(requested);
			if (!profile) {
				outError = "Build profile '" + std::string(requested) + "' not found in " + project.GetBuildProfilesDirectory() + ".";
				return {};
			}
			if (platform && profile->Platform != *platform) {
				outError = "Build profile '" + profile->Name + "' targets "
					+ FunkeBuildProfile::PlatformToString(profile->Platform) + ", not "
					+ FunkeBuildProfile::PlatformToString(*platform) + ".";
				return {};
			}
			return profile->Name;
		}

		if (const FunkeBuildProfile* active = findByName(project.ActiveBuildProfileName);
			active && (!platform || active->Platform == *platform)) {
			return active->Name;
		}

		for (const FunkeBuildProfile& profile : profiles) {
			const bool matches = platform ? profile.Platform == *platform : BuildPlatformSupport::IsHostDesktop(profile.Platform);
			if (matches) return profile.Name;
		}
		outError = std::string("No ") + (platform ? FunkeBuildProfile::PlatformToString(*platform) : "host desktop")
			+ " build profile in " + project.GetBuildProfilesDirectory() + ".";
		return {};
	}

	bool Prepare(FunkeProject& project, Request& outRequest, std::string& outError) {
		const std::filesystem::path activeProfilePath =
			Path::FromUtf8(project.GetBuildProfilesDirectory()) /
			Path::FromUtf8(project.ActiveBuildProfileName + std::string(FunkeBuildProfile::FileExtension));
		std::error_code existsError;
		if (project.ActiveBuildProfileName.empty() || !std::filesystem::is_regular_file(activeProfilePath, existsError)) {
			outError = "The active build profile is missing.";
			return false;
		}
		const FunkeBuildProfile activeProfile = FunkeBuildProfile::Load(Path::ToUtf8(activeProfilePath));
		if (!BuildPlatformSupport::IsAvailable(activeProfile.Platform)) {
			outError = "Target platform is unavailable: " + BuildPlatformSupport::UnavailableReason(activeProfile.Platform);
			return false;
		}
		// Other platforms get their backend in the staged project.json only.
		if (BuildPlatformSupport::IsHostDesktop(activeProfile.Platform)) project.ActiveRenderBackend = activeProfile.RenderBackend;

		const std::vector<std::string> buildScenes = project.GetEnabledBuildScenes();
		if (buildScenes.empty()) {
			outError = "No scenes are selected for the build.";
			return false;
		}
		std::unordered_set<std::string> uniqueSceneNames;
		for (const std::string& sceneName : buildScenes) {
			if (!BuildOutputSafety::IsSafePathSegment(sceneName)) {
				outError = "Build scene '" + sceneName + "' is not a safe scene name.";
				return false;
			}
			std::string uniquenessKey = sceneName;
#if defined(FNK_PLATFORM_WINDOWS) || defined(FNK_PLATFORM_MACOS)
			std::transform(uniquenessKey.begin(), uniquenessKey.end(), uniquenessKey.begin(),
				[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
#endif
			if (!uniqueSceneNames.insert(std::move(uniquenessKey)).second) {
				outError = "Build scene '" + sceneName + "' is listed more than once.";
				return false;
			}
			const std::filesystem::path scenePath = Path::FromUtf8(project.GetSceneFilePath(sceneName));
			std::error_code sceneError;
			std::ifstream sceneInput(scenePath, std::ios::binary);
			if (!std::filesystem::is_regular_file(scenePath, sceneError) || sceneError || !sceneInput.good()) {
				outError = "Build scene '" + sceneName + "' is missing or unreadable at " + Path::ToUtf8(scenePath) + ".";
				return false;
			}
		}
		project.StartupScene = buildScenes.front();

		outRequest.Project = project;
		outRequest.Project.ActiveRenderBackend = activeProfile.RenderBackend;
		activeProfile.ApplyTo(outRequest.Project);
		if (outRequest.ConfigurationOverride) outRequest.Project.ActiveBuildProfile = *outRequest.ConfigurationOverride;
		outRequest.Platform = activeProfile.Platform;
		outRequest.PlatformPackage = BuildPlatformSupport::GetPackage(activeProfile.Platform);
		outRequest.PlatformOptions = activeProfile.PlatformOptions;
		return true;
	}

	std::string DefaultOutputDirectory(const FunkeProject& project, std::string_view profileName) {
		return Path::Combine(project.RootDirectory, "Builds", std::string(profileName));
	}

	bool IsSerializedAssetPath(const std::filesystem::path& path) {
		std::string ext = path.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return ext == ".scene" || ext == ".prefab" || ext == ".dataasset";
	}

	SceneSerializationFormat ToSceneSerializationFormat(FunkeProject::ProjectAssetSerializationFormat format) {
		return format == FunkeProject::ProjectAssetSerializationFormat::Binary
			? SceneSerializationFormat::Binary
			: SceneSerializationFormat::Json;
	}

	int ConvertSerializedAssets(const std::filesystem::path& rootDir, SceneSerializationFormat format,
		const std::function<void(int, int, const std::string&)>& onProgress, std::string* failureMessage)
	{
		if (!std::filesystem::exists(rootDir)) {
			if (failureMessage) *failureMessage = "asset directory does not exist";
			return 0;
		}

		// Collected first so progress is determinate; the walk alone is slow on large projects.
		std::vector<std::filesystem::path> targets;
		std::error_code ec;
		for (std::filesystem::recursive_directory_iterator it(rootDir,
				std::filesystem::directory_options::skip_permission_denied, ec), end;
			it != end;
			it.increment(ec))
		{
			if (ec) {
				if (failureMessage && failureMessage->empty()) {
					*failureMessage = "failed to enumerate assets: " + ec.message();
				}
				ec.clear();
				continue;
			}
			if (!it->is_regular_file(ec) || ec || !IsSerializedAssetPath(it->path())) {
				ec.clear();
				continue;
			}
			targets.push_back(it->path());
		}

		int converted = 0;
		const int total = static_cast<int>(targets.size());
		for (int i = 0; i < total; ++i) {
			if (onProgress) {
				onProgress(i, total, Path::ToUtf8(targets[i].filename()));
			}
			if (SceneSerializer::ConvertFileFormat(Path::ToUtf8(targets[i]), format)) {
				++converted;
			}
			else if (failureMessage && failureMessage->empty()) {
				*failureMessage = "failed to convert " + Path::ToUtf8(targets[i]);
			}
		}
		if (onProgress) {
			onProgress(total, total, {});
		}
		return converted;
	}

	Result Build(Request request, const ProgressCallback& onProgress) {
		const auto buildStart = std::chrono::steady_clock::now();
		Result result;
		const auto report = [&](float progress, std::string_view stage) {
			if (onProgress) onProgress(std::clamp(progress, 0.0f, 1.0f), stage);
		};
		auto failBuild = [&](std::string_view stage) {
			result.Succeeded = false;
			result.Stage.assign(stage);
			report(1.0f, stage);
		};

		FunkeProject* project = &request.Project;
		const BuildPlatform targetPlatform = request.Platform;
		std::optional<PlatformPackageInfo>& platformPackage = request.PlatformPackage;

		result.OutputDirectory = std::move(request.OutputDirectory);
		if (result.OutputDirectory.empty()) {
			FNK_ERROR_TAG("Build", "No output directory specified.");
			failBuild("No output directory");
			return result;
		}

		const BuildOutputSafety::OutputPathValidation outputValidation =
			BuildOutputSafety::ValidateOutputPath(
				project->RootDirectory, project->AssetsDirectory, result.OutputDirectory);
		if (!outputValidation.IsValid()) {
			FNK_ERROR_TAG("Build", "Unsafe output directory '{}': {}",
				result.OutputDirectory, BuildOutputSafety::GetErrorMessage(outputValidation.Error));
			failBuild("Unsafe output directory");
			return result;
		}

		const std::filesystem::path finalOutDir = outputValidation.CanonicalOutput;
		result.OutputDirectory = finalOutDir.string();
		BuildStagingDirectory stagedOutput(finalOutDir);
		std::string stagingError;
		if (!stagedOutput.Prepare(stagingError)) {
			FNK_ERROR_TAG("Build", "Failed to prepare build staging directory: {}", stagingError);
			failBuild("Output staging failed");
			return result;
		}
		const std::filesystem::path& outDir = stagedOutput.GetPath();
		// Platform packages get the platform-neutral content in a subfolder and write their artifact next to it.
		const bool platformPackageBuild = platformPackage.has_value();
		const std::filesystem::path contentDir = platformPackageBuild ? outDir / ".funke-content" : outDir;
		if (platformPackageBuild) {
			std::error_code contentError;
			std::filesystem::create_directories(contentDir, contentError);
			FNK_INFO_TAG("Build", "Target {} via {} {}", FunkeBuildProfile::PlatformDisplayName(targetPlatform),
				platformPackage->Name, platformPackage->Version);
		}
		if (!std::filesystem::exists(project->AssetsDirectory)) {
			FNK_ERROR_TAG("Build", "Project Assets directory not found at {}", project->AssetsDirectory);
			failBuild("Project Assets missing");
			return result;
		}
		const BuildOutputSafety::DirectoryFingerprint assetInputFingerprint =
			BuildOutputSafety::CaptureDirectoryFingerprint(project->AssetsDirectory);
		if (!assetInputFingerprint.Succeeded()) {
			FNK_ERROR_TAG("Build", "Failed to snapshot project Assets: {}", assetInputFingerprint.Error);
			failBuild("Project asset snapshot failed");
			return result;
		}
		try {
			std::filesystem::copy_file(project->ProjectFilePath, contentDir / "project.json",
				std::filesystem::copy_options::overwrite_existing);
		}
		catch (const std::exception& error) {
			FNK_ERROR_TAG("Build", "Failed to snapshot project.json: {}", error.what());
			failBuild("Project snapshot failed");
			return result;
		}
		// The snapshot decides these, not the file on disk: the CLI overrides them without saving.
		const std::filesystem::path stagedProjectJson = contentDir / "project.json";
		if (!PatchStagedProjectMember(stagedProjectJson, "startupScene", project->StartupScene)
			|| !PatchStagedProjectMember(stagedProjectJson, "buildProfile",
				FunkeProject::BuildProfileToString(project->ActiveBuildProfile))
			|| !PatchStagedProjectMember(stagedProjectJson, "renderBackend",
				FunkeProject::RenderBackendToString(project->ActiveRenderBackend))
			|| (project->ShowRuntimeStats && !PatchStagedProjectMember(stagedProjectJson, "showRuntimeStats", *project->ShowRuntimeStats))
			|| (project->ShowRuntimeLogs && !PatchStagedProjectMember(stagedProjectJson, "showRuntimeLogs", *project->ShowRuntimeLogs))
			|| (project->DevConsole && !PatchStagedProjectMember(stagedProjectJson, "devConsole", *project->DevConsole))
			|| (!project->PrefsProfile.empty() && !PatchStagedProjectMember(stagedProjectJson, "prefsProfile", project->PrefsProfile))) {
			failBuild("Project snapshot failed");
			return result;
		}

		// Every target ships the icon as one normalised PNG next to project.json, so the runtime finds it
		// wherever the source lived (outside Assets, funke:, absolute) and in any format (.ico included).
		std::filesystem::path appIconSource;
		std::unique_ptr<ImageData> appIcon;
		{
			std::string resolved = SplashAssetResolve::Resolve(project->AppIconPath, &*project);
			if (resolved.empty() && !project->AppIconPath.empty()) {
				FNK_WARN_TAG("Build", "App icon not found: {}", project->AppIconPath);
			}
			// Windows keeps the runtime's embedded Funke icon; packaged platforms and macOS bundles would otherwise ship none.
#if defined(FNK_PLATFORM_WINDOWS)
			const bool needsDefaultIcon = platformPackageBuild;
#else
			const bool needsDefaultIcon = true;
#endif
			if (resolved.empty() && needsDefaultIcon) resolved = SplashAssetResolve::DefaultLogoPath();
			if (!resolved.empty()) {
				appIconSource = std::filesystem::path(resolved);
				appIcon = ImageProcessing::LoadFile(appIconSource);
				if (!appIcon) FNK_WARN_TAG("Build", "App icon could not be decoded: {}", resolved);
			}
		}
		const std::filesystem::path stagedAppIcon = contentDir / "AppIcon.png";
		bool hasStagedAppIcon = false;
		if (appIcon) {
			// 1024 covers the largest store/dock size; smaller sources are not upscaled.
			const int size = std::min(1024, std::max(appIcon->Width, appIcon->Height));
			std::unique_ptr<ImageData> square = ImageProcessing::FitSquare(*appIcon, size);
			hasStagedAppIcon = square && WritePng(stagedAppIcon, *square)
				&& PatchStagedProjectMember(contentDir / "project.json", "appIcon", "AppIcon.png");
			if (!hasStagedAppIcon) FNK_WARN_TAG("Build", "Failed to stage the app icon.");
		}
		// The engine's built-in assets are staged first; the project's FunkeAssets copy (a snapshot taken when the
		// project was created) only overrides files that are newer than the engine's. Shipping the snapshot as is
		// kept old projects on outdated built-ins, e.g. the previous engine icon on the splash screen.
		std::string funkeAssetsSrc = Path::ResolveFunkeAssets("");
		std::string funkeAssetsOverrides;
		{
			std::error_code funkeAssetsError;
			const bool hasProjectCopy = std::filesystem::exists(project->FunkeAssetsDirectory)
				&& !std::filesystem::is_empty(project->FunkeAssetsDirectory, funkeAssetsError);
			if (funkeAssetsSrc.empty()) {
				if (hasProjectCopy) funkeAssetsSrc = project->FunkeAssetsDirectory;
			}
			else if (hasProjectCopy
				&& !std::filesystem::equivalent(Path::FromUtf8(funkeAssetsSrc), Path::FromUtf8(project->FunkeAssetsDirectory), funkeAssetsError)) {
				funkeAssetsOverrides = project->FunkeAssetsDirectory;
			}
		}

		// Planned between the two fingerprints so an edit during the scan also fails the snapshot check.
		BuildAssetPruning::Plan assetPlan = BuildAssetPruning::ComputeShippedAssets({
			Path::FromUtf8(project->RootDirectory),
			Path::FromUtf8(project->AssetsDirectory),
			Path::FromUtf8(funkeAssetsSrc),
			project->GetEnabledBuildScenes(),
			{ outDir, finalOutDir },
		});
		if (!assetPlan.Valid) {
			FNK_WARN_TAG("Build", "Unused-asset stripping disabled, shipping all assets: {}", assetPlan.Error);
		}
		const FilteredCopyResult initialAssetsCopy = CopyDirIncrementalFiltered(
			project->AssetsDirectory, contentDir / "Assets",
			[&](const std::filesystem::path& rel) { return !assetPlan.Valid || assetPlan.KeepsProjectFile(rel); });
		if (!initialAssetsCopy.Succeeded()) {
			FNK_ERROR_TAG("Build", "Failed to snapshot project Assets: {}", initialAssetsCopy.Error);
			failBuild("Project asset snapshot failed");
			return result;
		}
		if (const std::vector<std::string> disabledModuleUse = FindDisabledModuleComponents(contentDir / "Assets", *project);
			!disabledModuleUse.empty()) {
			FNK_ERROR_TAG("Build", "Shipped scenes use components of engine modules the project does not enable. "
				"Enable the module (Project Settings > Build > Engine Modules) or remove the components:");
			for (const std::string& finding : disabledModuleUse) {
				FNK_ERROR_TAG("Build", "  {}", finding);
			}
			failBuild("Scenes use disabled engine modules");
			return result;
		}
		const BuildOutputSafety::DirectoryFingerprint postCopyFingerprint =
			BuildOutputSafety::CaptureDirectoryFingerprint(project->AssetsDirectory);
		if (!(assetInputFingerprint == postCopyFingerprint)) {
			FNK_ERROR_TAG("Build", "Project Assets changed while the build snapshot was being captured.");
			failBuild("Project assets changed during snapshot");
			return result;
		}

		FNK_INFO_TAG("Build", "Starting build for '{}'...", project->Name);
		report(0.05f, "Preparing build");

		const std::filesystem::path exeDir(Path::ExecutableDir());
		const std::string buildConfiguration = FunkeProject::GetBuildConfigurationFor(project->ActiveBuildProfile);
		const std::string runtimeOutputFolder = GetRuntimeOutputFolderName(buildConfiguration);
		const std::string resolvedEngineRoot = FunkeProject::GetEngineRootDir();
		const std::filesystem::path engineRoot(resolvedEngineRoot);
		const std::vector<FunkePackageManifest> packageManifests =
			FunkePackageInstaller::EnumerateAll(project->RootDirectory);
		std::vector<std::string> engineNativePackageTargets;
		std::vector<const FunkePackageManifest*> projectLocalPackageManifests;
		for (const std::string& packageName : project->Packages) {
			if (!BuildOutputSafety::IsSafePathSegment(packageName)) {
				FNK_ERROR_TAG("Build", "Unsafe package identifier '{}'.", packageName);
				failBuild("Invalid package identifier");
				return result;
			}

			const auto manifest = std::find_if(packageManifests.begin(), packageManifests.end(),
				[&](const FunkePackageManifest& candidate) { return candidate.Name == packageName; });
			if (manifest == packageManifests.end()) {
				FNK_ERROR_TAG("Build", "Package '{}' has no readable funke-package.lua.", packageName);
				failBuild("Package manifest missing");
				return result;
			}
			if (!manifest->IsEngine) {
				projectLocalPackageManifests.push_back(&*manifest);
			}
			if (manifest->IsEngine && (manifest->HasNativeLayer || manifest->HasNativeStandaloneLayer)) {
				engineNativePackageTargets.push_back("Pkg." + packageName + ".Native");
			}
		}
		// The runtime would refuse to load such a package; project.json edited by hand can get here.
		{
			const std::vector<PackageManifest> allManifests = FunkePackageInstaller::ToPackageManifests(packageManifests);
			bool missingModule = false;
			for (const std::string& packageName : project->Packages) {
				for (const std::string& module : CollectRequiredModules(allManifests, packageName)) {
					if (project->HasModule(module)) continue;
					FNK_ERROR_TAG("Build", "Package '{}' needs the engine module {}, which the project does not enable "
						"(Package Manager > Engine Modules).", packageName, module);
					missingModule = true;
				}
			}
			if (missingModule) {
				failBuild("Packages need disabled engine modules");
				return result;
			}
		}

#if FUNKE_WITH_SCRIPTING
		if (!engineRoot.empty()) {
			const std::filesystem::path scriptCoreProject =
				engineRoot / "Funke-ScriptCore" / "Funke-ScriptCore.csproj";
			const std::filesystem::path scriptCoreSources = engineRoot / "Funke-ScriptCore" / "Source";
			if (std::filesystem::is_regular_file(scriptCoreProject)
				&& std::filesystem::is_directory(scriptCoreSources)) {
				report(0.08f, "Building scripting runtime");
				std::vector<std::string> scriptCoreCommand{
					"dotnet", "build", scriptCoreProject.string(),
					"-c", buildConfiguration,
					"--nologo", "-v", "q", "-nodeReuse:false",
					"-p:UseSharedCompilation=false"
				};
#if !defined(FNK_PLATFORM_WINDOWS)
				// The checked-in project names Windows output folders; land next to this host's runtime instead.
				const std::filesystem::path scriptCoreOutput = engineRoot / "bin" / runtimeOutputFolder / "Funke-ScriptCore";
				const std::filesystem::path scriptCoreObjects = engineRoot / "bin-int" / runtimeOutputFolder / "Funke-ScriptCore";
				scriptCoreCommand.push_back("-p:Platform=x64");
				scriptCoreCommand.push_back("-p:OutputPath=" + Path::ToUtf8(scriptCoreOutput) + "/");
				scriptCoreCommand.push_back("-p:IntermediateOutputPath=" + Path::ToUtf8(scriptCoreObjects) + "/");
#if defined(__aarch64__)
				scriptCoreCommand.push_back("-p:PlatformTarget=AnyCPU");
#endif
#endif
				const Process::Result scriptCoreBuild = Process::Run(scriptCoreCommand, engineRoot.string(), std::chrono::minutes(5));
				if (!scriptCoreBuild.Succeeded()) {
					FNK_ERROR_TAG("Build", "Funke-ScriptCore build failed (exit code {}).",
						scriptCoreBuild.ExitCode);
					if (!scriptCoreBuild.Output.empty()) FNK_ERROR_TAG("Build", "{}", scriptCoreBuild.Output);
					failBuild("Scripting runtime build failed");
					return result;
				}
			}
		}
#endif

		if (!projectLocalPackageManifests.empty()) {
			report(0.09f, "Compiling project packages");
			const FunkeProject::RegenerateResult packageGeneration =
				FunkeProject::RegeneratePackageWorkspaceForProject(project->RootDirectory);
			if (!packageGeneration.Succeeded) {
				FNK_ERROR_TAG("Build", "Project package generation failed: {}", packageGeneration.Output);
				failBuild("Project package generation failed");
				return result;
			}

			const FunkeProject::BuildResult packageBuild =
				FunkeProject::BuildProjectPackages(project->RootDirectory, buildConfiguration, "x64");
			if (!packageBuild.Succeeded) {
				FNK_ERROR_TAG("Build", "Project package compilation failed: {}", packageBuild.Output);
				failBuild("Project package compilation failed");
				return result;
			}

			for (const FunkePackageManifest* manifest : projectLocalPackageManifests) {
				if (!manifest->HasCSharpLayer) continue;

				const std::string targetName = "Pkg." + manifest->Name;
				const std::filesystem::path sourceDirectory =
					FunkeProject::GetProjectPackageOutputDirectory(
						project->RootDirectory, targetName, buildConfiguration);
				const std::filesystem::path assemblyPath = sourceDirectory / (targetName + ".dll");
				std::error_code assemblyError;
				if (!std::filesystem::is_regular_file(assemblyPath, assemblyError) || assemblyError) {
					FNK_ERROR_TAG("Build", "Managed project package artifact is missing: {}",
						assemblyPath.string());
					failBuild("Project package artifact missing");
					return result;
				}

				const std::filesystem::path destinationDirectory =
					std::filesystem::path(manifest->PackageDir) / "Bin" / GetPackageBinaryFolderName();
				const DirectoryCopyResult packageCopy =
					CopyDirIncremental(sourceDirectory, destinationDirectory);
				if (!packageCopy.Succeeded()) {
					FNK_ERROR_TAG("Build", "Failed to stage managed project package '{}': {}",
						manifest->Name, packageCopy.Error);
					failBuild("Project package staging failed");
					return result;
				}
			}
			FunkePackageInstaller::RegeneratePackageReferences(*project);
		}

		const bool hasManagedProject = std::filesystem::exists(project->CsprojPath);
		if (hasManagedProject) {
			report(0.10f, "Compiling C# scripts");
			FNK_INFO_TAG("Build", "Compiling C# scripts...");
			Process::Result buildResult = Process::Run({
				"dotnet",
				"build",
				project->CsprojPath,
				"-c", buildConfiguration,
				"--nologo",
				"-v", "q",
				"-nodeReuse:false",
				"-p:UseSharedCompilation=false",
				"-p:DefineConstants=" + BuildManagedDefinesForSnapshot(
					*project, "FUNKE_BUILD", buildConfiguration, targetPlatform)
			}, {}, std::chrono::minutes(5));
			if (!buildResult.Succeeded()) {
				FNK_ERROR_TAG("Build", "C# script compilation failed (exit code {}).", buildResult.ExitCode);
				if (!buildResult.Output.empty()) {
					FNK_ERROR_TAG("Build", "{}", buildResult.Output);
				}
				failBuild("C# compilation failed");
				return result;
			}
			FNK_INFO_TAG("Build", "C# scripts compiled.");
			report(0.30f, "C# scripts compiled");
		}

		if (platformPackageBuild && !engineNativePackageTargets.empty()) {
			FNK_WARN_TAG("Build", "Native package layers are not built for {} yet; {} native package(s) skipped.",
				FunkeBuildProfile::PlatformDisplayName(targetPlatform), engineNativePackageTargets.size());
		}
		if (!platformPackageBuild && !engineNativePackageTargets.empty()) {
#if defined(FNK_PLATFORM_WINDOWS)
			const std::filesystem::path solutionPath =
				std::filesystem::path(FunkeProject::GetEngineRootDir()) / "Funke.sln";
			if (!FunkeProject::GetMSBuildPath().empty() && std::filesystem::exists(solutionPath)) {
				report(0.40f, "Compiling native packages");
				const FunkeProject::BuildResult packageBuild = FunkeProject::BuildSolutionTargets(
					engineNativePackageTargets, buildConfiguration, "x64");
				if (!packageBuild.Succeeded) {
					FNK_ERROR_TAG("Build", "Native package compilation failed: {}", packageBuild.Output);
					failBuild("Native package compilation failed");
					return result;
				}
			}
			else {
				FNK_WARN_TAG("Build", "MSBuild is unavailable; staging existing native package binaries.");
			}
#else
			if (HasEngineMakefile(engineRoot)) {
				report(0.40f, "Compiling native packages");
				const Process::Result packageBuild = RunEngineMake(engineRoot, buildConfiguration, engineNativePackageTargets);
				if (!packageBuild.Succeeded()) {
					FNK_ERROR_TAG("Build", "Native package compilation failed: {}", packageBuild.Output);
					failBuild("Native package compilation failed");
					return result;
				}
			}
			else {
				FNK_WARN_TAG("Build", "No engine Makefile (premake5 gmake2); staging existing native package binaries.");
			}
#endif
		}

		// Desktop builds compile a runtime without the engine modules the project does not use.
		std::optional<RuntimeVariant> runtimeVariant;
		if (!platformPackageBuild && !engineRoot.empty()) {
			runtimeVariant = ResolveRuntimeVariant(*project);
			if (runtimeVariant && !engineNativePackageTargets.empty()) {
				// Native packages are linked against the full engine binary; PackageHost would reject them.
				FNK_WARN_TAG("Build", "The project uses native packages, so the build ships the full engine instead of "
					"the '{}' runtime variant.", runtimeVariant->Id);
				runtimeVariant.reset();
			}
			if (runtimeVariant) {
				report(0.43f, "Preparing runtime variant (" + runtimeVariant->Id + ")");
				if (!GenerateRuntimeVariantWorkspace(engineRoot, *runtimeVariant)) {
					FNK_WARN_TAG("Build", "Shipping the full engine instead of the '{}' runtime variant.", runtimeVariant->Id);
					runtimeVariant.reset();
				}
			}
		}
		const std::filesystem::path runtimeWorkspace = runtimeVariant
			? GetRuntimeVariantWorkspace(engineRoot, *runtimeVariant)
			: engineRoot;
		const std::string runtimeVariantFolder = runtimeVariant
			? runtimeOutputFolder + "-" + runtimeVariant->Id
			: runtimeOutputFolder;

		const std::filesystem::path developmentRuntimeDirectory = engineRoot.empty()
			? std::filesystem::path{}
			: engineRoot / "bin" / runtimeVariantFolder / "Funke-Runtime";
		std::filesystem::path runtimeOutputDirectory;
		if (platformPackageBuild) {
			// The package compiles its own runtime; this directory only serves the ScriptCore / license lookups.
			runtimeOutputDirectory = developmentRuntimeDirectory;
		}
		else {
			const std::filesystem::path runtimeProject = engineRoot.empty()
				? std::filesystem::path{}
				: runtimeWorkspace / "Funke-Runtime" / "Funke-Runtime.vcxproj";
			const std::string msbuildPath = FunkeProject::GetNativeMSBuildPath();
#if defined(FNK_PLATFORM_WINDOWS)
			if (!runtimeProject.empty() && std::filesystem::exists(runtimeProject)) {
				if (msbuildPath.empty()) {
					FNK_ERROR_TAG("Build", "MSBuild is required to produce a fresh runtime artifact.");
					failBuild("MSBuild unavailable");
					return result;
				}
				report(0.45f, "Building runtime (" + buildConfiguration + ")");
				FNK_INFO_TAG("Build", "Building Funke-Runtime ({}{}) via {}...", buildConfiguration,
					runtimeVariant ? ", modules variant " + runtimeVariant->Id : std::string{}, msbuildPath);
				std::vector<std::string> runtimeBuildCommand{
					msbuildPath, runtimeProject.string(),
					"/p:Configuration=" + buildConfiguration, "/p:Platform=x64",
					"/nr:false", "/v:minimal", "/nologo"
				};
				if (runtimeVariant) {
					// A variant's first build compiles the whole engine; serial compilation would take most of an hour.
					runtimeBuildCommand.push_back("/m");
				}
				else {
					runtimeBuildCommand.insert(runtimeBuildCommand.end(), { "/p:CL_MPCount=1", "/p:UseMultiToolTask=false", "/m:1" });
				}
				const Process::Result runtimeBuild = Process::Run(runtimeBuildCommand, engineRoot.string(),
					std::chrono::minutes(runtimeVariant ? 60 : 15));
				if (!runtimeBuild.Succeeded()) {
					FNK_ERROR_TAG("Build", "Runtime build ({}) failed (exit code {}).", buildConfiguration, runtimeBuild.ExitCode);
					if (!runtimeBuild.Output.empty()) FNK_ERROR_TAG("Build", "{}", runtimeBuild.Output);
					failBuild("Runtime build failed");
					return result;
				}
				runtimeOutputDirectory = developmentRuntimeDirectory;
				FNK_INFO_TAG("Build", "Runtime ({}) ready; staging from {}", buildConfiguration,
					runtimeOutputDirectory.string());
			}
#else
			(void)runtimeProject;
			(void)msbuildPath;
			if (HasEngineMakefile(runtimeWorkspace)) {
				report(0.45f, "Building runtime (" + buildConfiguration + ")");
				FNK_INFO_TAG("Build", "Building Funke-Runtime ({}{})...", buildConfiguration,
					runtimeVariant ? ", modules variant " + runtimeVariant->Id : std::string{});
				const Process::Result runtimeBuild = RunEngineMake(runtimeWorkspace, buildConfiguration, { "Funke-Runtime" });
				if (!runtimeBuild.Succeeded()) {
					FNK_ERROR_TAG("Build", "Runtime build ({}) failed (exit code {}).", buildConfiguration, runtimeBuild.ExitCode);
					if (!runtimeBuild.Output.empty()) FNK_ERROR_TAG("Build", "{}", runtimeBuild.Output);
					failBuild("Runtime build failed");
					return result;
				}
				runtimeOutputDirectory = developmentRuntimeDirectory;
				FNK_INFO_TAG("Build", "Runtime ({}) ready; staging from {}", buildConfiguration,
					runtimeOutputDirectory.string());
			}
#endif
			if (runtimeOutputDirectory.empty()) {
				const std::string runtimeExecutable = GetRuntimeExecutableFilename();
				const std::string engineLibrary = GetEngineRuntimeFilename();
				const std::string bundledRuntimeName =
					project->ActiveBuildProfile == FunkeProject::BuildProfile::Release ? "Runtime-Dist"
					: project->ActiveBuildProfile == FunkeProject::BuildProfile::Debug ? "Runtime-Debug"
					: "Runtime-Development";
				// A Debug build must not silently ship optimized binaries from the Dist fallbacks below.
				if (project->ActiveBuildProfile == FunkeProject::BuildProfile::Debug) {
					runtimeOutputDirectory = BuildOutputSafety::FindFirstCompleteArtifactDirectory({
						developmentRuntimeDirectory,
						engineRoot.empty() ? std::filesystem::path{} : engineRoot / bundledRuntimeName,
						exeDir / bundledRuntimeName,
					}, { runtimeExecutable, engineLibrary });
					if (runtimeOutputDirectory.empty()) {
						FNK_ERROR_TAG("Build", "Debug builds need the engine sources (Funke-Runtime.vcxproj) or a "
							"Runtime-Debug folder next to the editor; neither was found.");
						failBuild("Debug runtime unavailable");
						return result;
					}
				}
				if (runtimeOutputDirectory.empty()) runtimeOutputDirectory = BuildOutputSafety::FindFirstCompleteArtifactDirectory({
					developmentRuntimeDirectory,
					engineRoot.empty() ? std::filesystem::path{} : engineRoot / bundledRuntimeName,
					engineRoot.empty() ? std::filesystem::path{} : engineRoot / "Runtime-Dist",
					exeDir / bundledRuntimeName,
					exeDir / "Runtime-Dist",
					exeDir,
					exeDir.parent_path() / "Funke-Runtime",
				}, { runtimeExecutable, engineLibrary });
				if (runtimeOutputDirectory.empty()) {
					FNK_ERROR_TAG("Build", "No complete runtime artifact set is available for {}.",
						buildConfiguration);
					failBuild("Runtime artifacts unavailable");
					return result;
				}
				FNK_INFO_TAG("Build", "Using prebuilt runtime artifacts from {}",
					runtimeOutputDirectory.string());
			}
		}

		report(0.55f, "Copying runtime binaries");

		auto copyFile = [&](const std::filesystem::path& src, const std::filesystem::path& dest,
			const std::string& name, bool required)
		{
			try {
				if (!std::filesystem::exists(src)) {
					if (required) FNK_ERROR_TAG("Build", "Required {} not found at {}", name, src.string());
					else FNK_WARN_TAG("Build", "{} not found at {}", name, src.string());
					return false;
				}
				auto canonical = std::filesystem::canonical(src);
				if (NeedsCopy(canonical, dest)) {
					std::filesystem::create_directories(dest.parent_path());
					std::filesystem::copy_file(canonical, dest, std::filesystem::copy_options::overwrite_existing);
					FNK_INFO_TAG("Build", "Copied {}", name);
				}
				return true;
			}
			catch (const std::exception& e) {
				if (required) FNK_ERROR_TAG("Build", "Failed to copy {}: {}", name, e.what());
				else FNK_WARN_TAG("Build", "Failed to copy {}: {}", name, e.what());
				return false;
			}
		};
		auto copyRequiredFile = [&](const std::filesystem::path& src,
			const std::filesystem::path& dest, const std::string& name)
		{
			if (copyFile(src, dest, name, true)) return true;
			failBuild("Required artifact staging failed");
			return false;
		};

		const std::string runtimeExecutableFilename = GetRuntimeExecutableFilename();
		const std::string outputExecutableStem = project->ExecutableName.empty()
			? project->Name : project->ExecutableName;
		if (!BuildOutputSafety::IsSafePathSegment(outputExecutableStem)) {
			FNK_ERROR_TAG("Build", "Unsafe executable name '{}'.", outputExecutableStem);
			failBuild("Invalid executable name");
			return result;
		}
		if (!platformPackageBuild) {
		result.ExecutablePath = (finalOutDir /
			(outputExecutableStem + std::filesystem::path(runtimeExecutableFilename).extension().string())).string();
		if (!copyRequiredFile(runtimeOutputDirectory / runtimeExecutableFilename,
			outDir / (outputExecutableStem + std::filesystem::path(runtimeExecutableFilename).extension().string()),
			"runtime executable")) return result;
#if !defined(FNK_PLATFORM_WINDOWS)
		{
			std::error_code permissionError;
			std::filesystem::permissions(outDir / outputExecutableStem,
				std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
				std::filesystem::perm_options::add, permissionError);
		}
#endif
		if (!copyRequiredFile(runtimeOutputDirectory / GetEngineRuntimeFilename(),
			outDir / GetEngineRuntimeFilename(),
			GetEngineRuntimeFilename())) return result;

		{
			const std::string sdlFilename = SDL3SharedLibraryFilename();
			if (!copyRequiredFile(runtimeOutputDirectory / sdlFilename,
				outDir / sdlFilename, sdlFilename)) return result;
		}
		if (buildConfiguration != "Dist") {
			const std::string tracyFilename = SharedLibraryFilename("Tracy");
			const std::filesystem::path tracySource = runtimeOutputDirectory / tracyFilename;
#if FUNKE_PROFILER_ENABLED
			if (!copyRequiredFile(tracySource, outDir / tracyFilename, tracyFilename)) return result;
#else
			if (std::filesystem::exists(tracySource)) {
				copyFile(tracySource, outDir / tracyFilename, tracyFilename, false);
			}
#endif
		}

#if FUNKE_WITH_RENDER && !defined(FNK_PLATFORM_WINDOWS)
		{
			const std::string dawnFilename = SharedLibraryFilename("webgpu_dawn");
			if (!copyRequiredFile(runtimeOutputDirectory / dawnFilename,
				outDir / dawnFilename, dawnFilename)) return result;
		}
#endif
#if defined(FNK_PLATFORM_WINDOWS)
		// Debuggers look up the PDB by its link-time name next to the module, so the renamed exe keeps Funke-Runtime.pdb.
		if (buildConfiguration == "Debug") {
			copyFile(runtimeOutputDirectory / "Funke-Runtime.pdb", outDir / "Funke-Runtime.pdb", "Funke-Runtime.pdb", false);
			// The runtime's post-build copies only the engine DLL; its PDB stays in the engine's own output folder.
			const std::filesystem::path enginePdb = std::filesystem::exists(runtimeOutputDirectory / "Funke-Engine.pdb")
				? runtimeOutputDirectory / "Funke-Engine.pdb"
				: runtimeOutputDirectory.parent_path() / "Funke-Engine" / "Funke-Engine.pdb";
			copyFile(enginePdb, outDir / "Funke-Engine.pdb", "Funke-Engine.pdb", false);
		}
#endif
		}

#if FUNKE_WITH_SCRIPTING
		// Package builds do not rebuild Funke-Runtime, so its ScriptCore copy can predate the one this build just
		// compiled; staging that copy breaks the interop layout check against the engine compiled from source.
		const std::filesystem::path freshScriptCoreDir = runtimeOutputDirectory.parent_path() / "Funke-ScriptCore";
		const std::filesystem::path scriptCoreDir =
			BuildOutputSafety::FindFirstCompleteArtifactDirectory({
				platformPackageBuild ? freshScriptCoreDir : runtimeOutputDirectory,
				platformPackageBuild ? runtimeOutputDirectory : freshScriptCoreDir,
				engineRoot.empty() ? std::filesystem::path{} : engineRoot / "Funke-ScriptCore",
			}, {
				"Funke-ScriptCore.dll",
				"Funke-ScriptCore.runtimeconfig.json",
				"Funke-ScriptCore.deps.json",
			});
		if (scriptCoreDir.empty()) {
			FNK_ERROR_TAG("Build", "No complete Funke-ScriptCore artifact set is available.");
			failBuild("Scripting runtime artifacts unavailable");
			return result;
		}
		if (!copyRequiredFile(scriptCoreDir / "Funke-ScriptCore.dll",
			contentDir / "Funke-ScriptCore.dll", "Funke-ScriptCore.dll")) return result;
		if (!copyRequiredFile(scriptCoreDir / "Funke-ScriptCore.runtimeconfig.json",
			contentDir / "Funke-ScriptCore.runtimeconfig.json", "ScriptCore config")) return result;
		if (!copyRequiredFile(scriptCoreDir / "Funke-ScriptCore.deps.json",
			contentDir / "Funke-ScriptCore.deps.json", "ScriptCore deps")) return result;
		if (buildConfiguration == "Debug") {
			copyFile(scriptCoreDir / "Funke-ScriptCore.pdb", contentDir / "Funke-ScriptCore.pdb", "Funke-ScriptCore.pdb", false);
		}
#if defined(FNK_PLATFORM_WINDOWS)
		if (!platformPackageBuild) {
			const std::string netHostFilename = GetNetHostRuntimeFilename();
			const std::filesystem::path netHostSource = runtimeOutputDirectory / netHostFilename;
			if (!copyRequiredFile(netHostSource, outDir / netHostFilename, netHostFilename)) return result;
		}
#endif
#endif

		const std::filesystem::path licenseSource =
			BuildOutputSafety::FindFirstCompleteArtifactDirectory({
				runtimeOutputDirectory / "Licenses",
				engineRoot.empty() ? std::filesystem::path{} : engineRoot / "Licenses",
				exeDir / "EngineSDK" / "Licenses",
				exeDir.parent_path() / "Funke-Launcher" / "EngineSDK" / "Licenses",
			}, {
				std::filesystem::path("Funke") / "LICENSE.txt",
				std::filesystem::path("Funke") / "THIRD_PARTY_LICENSES.md",
				std::filesystem::path("DotNet") / "ThirdPartyNotices.txt",
			});
		if (licenseSource.empty()) {
			FNK_ERROR_TAG("Build", "A complete redistribution license payload is unavailable.");
			failBuild("Redistribution licenses unavailable");
			return result;
		}
		const DirectoryCopyResult licenseCopy = CopyDirIncremental(licenseSource, contentDir / "Licenses");
		if (!licenseCopy.Succeeded()) {
			FNK_ERROR_TAG("Build", "Failed to stage redistribution licenses: {}", licenseCopy.Error);
			failBuild("License staging failed");
			return result;
		}

		report(0.75f, "Optimizing project assets");
		{
			FNK_INFO_TAG("Build", "Assets: {} file(s) snapshotted, {} unused skipped ({:.1f} MB)",
				initialAssetsCopy.Copied, initialAssetsCopy.Skipped,
				static_cast<double>(initialAssetsCopy.SkippedBytes) / (1024.0 * 1024.0));
			report(0.78f, "Optimizing scene assets");
			std::string conversionError;
			const int convertedFiles = ConvertSerializedAssets(
				contentDir / "Assets", SceneSerializationFormat::Binary, {}, &conversionError);
			if (!conversionError.empty()) {
				FNK_ERROR_TAG("Build", "Failed to optimize serialized assets: {}", conversionError);
				failBuild("Scene asset optimization failed");
				return result;
			}
			FNK_INFO_TAG("Build", "Serialized assets optimized to binary: {} file(s)", convertedFiles);
		}

		// Ship the asset-registry manifest (<project>/.funke/asset-registry.cache) so the runtime
		// trusts it and skips the cold ~9k-.meta directory scan on launch. Copy-only — no registry
		// calls from this build worker thread; a stale/absent manifest self-heals on first launch.
		{
			const std::filesystem::path manifestSrc =
				std::filesystem::path(project->AssetsDirectory).parent_path() / ".funke" / "asset-registry.cache";
			if (std::filesystem::exists(manifestSrc)) {
				try {
					const std::filesystem::path manifestDest = contentDir / ".funke" / "asset-registry.cache";
					std::filesystem::create_directories(manifestDest.parent_path());
					std::ifstream manifestIn(manifestSrc, std::ios::binary);
					const std::string manifest((std::istreambuf_iterator<char>(manifestIn)), std::istreambuf_iterator<char>());
					std::ofstream manifestOut(manifestDest, std::ios::binary | std::ios::trunc);
					manifestOut << BuildAssetPruning::FilterRegistryManifest(manifest, assetPlan);
					if (!manifestIn.is_open() || !manifestOut) throw std::runtime_error("could not copy the manifest");
					FNK_INFO_TAG("Build", "Shipped asset-registry manifest");
				}
				catch (const std::exception& e) {
					FNK_WARN_TAG("Build", "Failed to ship asset-registry manifest: {}", e.what());
				}
			}
			else {
				FNK_WARN_TAG("Build", "Asset-registry manifest not found at {}; runtime cold-scans on first launch", manifestSrc.string());
			}
		}

		{
			if (!funkeAssetsSrc.empty() && std::filesystem::exists(funkeAssetsSrc)) {
				const auto shipsBuiltIn = [&](const std::filesystem::path& rel) {
					if (assetPlan.Valid) return assetPlan.KeepsBuiltInFile(rel);
					std::string key = Path::ToUtf8(rel);
					std::replace(key.begin(), key.end(), '\\', '/');
					std::transform(key.begin(), key.end(), key.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					return !key.starts_with("textures/editor/");
				};
				const FilteredCopyResult funkeAssetsCopy = CopyDirIncrementalFiltered(funkeAssetsSrc, contentDir / "FunkeAssets", shipsBuiltIn);
				if (!funkeAssetsCopy.Succeeded()) {
					FNK_ERROR_TAG("Build", "Failed to copy FunkeAssets: {}", funkeAssetsCopy.Error);
					failBuild("FunkeAssets staging failed");
					return result;
				}
				FNK_INFO_TAG("Build", "FunkeAssets: {} file(s) copied, {} unused skipped ({:.1f} MB)",
					funkeAssetsCopy.Copied, funkeAssetsCopy.Skipped,
					static_cast<double>(funkeAssetsCopy.SkippedBytes) / (1024.0 * 1024.0));
				// Incremental copy: a project file replaces the staged engine file only when it is newer (edited).
				if (!funkeAssetsOverrides.empty()) {
					const FilteredCopyResult overrideCopy = CopyDirIncrementalFiltered(funkeAssetsOverrides, contentDir / "FunkeAssets", shipsBuiltIn);
					if (!overrideCopy.Succeeded()) {
						FNK_ERROR_TAG("Build", "Failed to copy the project's FunkeAssets overrides: {}", overrideCopy.Error);
						failBuild("FunkeAssets staging failed");
						return result;
					}
					if (overrideCopy.Copied > 0) FNK_INFO_TAG("Build", "FunkeAssets: {} project override(s)", overrideCopy.Copied);
				}
			}
			else {
				FNK_ERROR_TAG("Build", "FunkeAssets not found; build cannot be completed");
				failBuild("FunkeAssets missing");
				return result;
			}
		}

		if (hasManagedProject) {
			auto userBinDir = std::filesystem::path(
				project->GetUserAssemblyOutputPath(buildConfiguration)).parent_path();
			const std::filesystem::path userAssemblyPath =
				project->GetUserAssemblyOutputPath(buildConfiguration);
			if (!std::filesystem::exists(userAssemblyPath)) {
				FNK_ERROR_TAG("Build", "Compiled user assembly not found at {}", userAssemblyPath.string());
				failBuild("User assembly missing");
				return result;
			}
			auto destBinDir = contentDir / "bin" / buildConfiguration;
			int copied = 0;
			try {
				for (const auto& entry : std::filesystem::directory_iterator(userBinDir)) {
					if (!entry.is_regular_file()) continue;
					auto ext = entry.path().extension().string();
					if (ext == ".dll" || ext == ".json" || (ext == ".pdb" && buildConfiguration == "Debug")) {
						if (!copyRequiredFile(entry.path(), destBinDir / entry.path().filename(),
							entry.path().filename().string())) return result;
						copied++;
					}
				}
			}
			catch (const std::exception& e) {
				FNK_ERROR_TAG("Build", "Failed to enumerate user assemblies: {}", e.what());
				failBuild("User assembly staging failed");
				return result;
			}
			FNK_INFO_TAG("Build", "User assemblies: {} file(s) copied", copied);
		}

		// Native siblings must be staged explicitly for managed P/Invoke package bridges.
		if (!platformPackageBuild) {
			int copiedPackages = 0;
			for (const std::string& packageName : project->Packages) {
				if (packageName.empty()) continue;
				if (!BuildOutputSafety::IsSafePathSegment(packageName)) {
					FNK_ERROR_TAG("Build", "Unsafe package identifier '{}'.", packageName);
					failBuild("Invalid package identifier");
					return result;
				}
				const auto manifest = std::find_if(packageManifests.begin(), packageManifests.end(),
					[&](const FunkePackageManifest& candidate) { return candidate.Name == packageName; });
				if (manifest == packageManifests.end()) {
					FNK_ERROR_TAG("Build", "Package '{}' has no readable funke-package.lua.", packageName);
					failBuild("Package manifest missing");
					return result;
				}
				if (!manifest->HasNativeLayer && !manifest->HasNativeStandaloneLayer) {
					FNK_INFO_TAG("Build", "Package '{}' is managed-only.", packageName);
					continue;
				}
				const std::string nativeFolderName = "Pkg." + packageName + ".Native";
				const std::string nativeFileName = SharedLibraryFilename(nativeFolderName);
				const std::string nativeAbiFileName = nativeFileName + ".funkeabi";
				std::vector<std::filesystem::path> candidateDirectories;
				if (!manifest->IsEngine) {
					candidateDirectories.emplace_back(
						FunkeProject::GetProjectPackageOutputDirectory(
							project->RootDirectory, nativeFolderName, buildConfiguration));
				}
				else {
					candidateDirectories.emplace_back(
						runtimeOutputDirectory.parent_path() / nativeFolderName);
				}
				candidateDirectories.emplace_back(
					std::filesystem::path(manifest->PackageDir) / "Bin" / GetPackageBinaryFolderName());
				const std::filesystem::path sourceDirectory =
					BuildOutputSafety::FindFirstCompleteArtifactDirectory(
						candidateDirectories, { nativeFileName, nativeAbiFileName });
				const std::filesystem::path src = sourceDirectory / nativeFileName;
				const std::filesystem::path dst = outDir / "Packages" / nativeFolderName / nativeFileName;
				if (!copyRequiredFile(src, dst, nativeFileName)) return result;
				if (!copyRequiredFile(sourceDirectory / nativeAbiFileName,
					dst.parent_path() / nativeAbiFileName, nativeAbiFileName)) return result;
				copiedPackages++;

#if defined(FNK_PLATFORM_WINDOWS)
				const std::filesystem::path pdbSrc = sourceDirectory / (nativeFolderName + ".pdb");
				if (std::filesystem::exists(pdbSrc)) {
					copyFile(pdbSrc, dst.parent_path() / pdbSrc.filename(), pdbSrc.filename().string(), false);
				}
#endif
			}
			if (copiedPackages > 0) {
				FNK_INFO_TAG("Build", "Native packages: {} library file(s) staged under Packages/.", copiedPackages);
			}
		}

#ifdef FNK_PLATFORM_WINDOWS
		if (!platformPackageBuild && appIcon) {
			std::string iconExtLower = appIconSource.extension().string();
			std::transform(iconExtLower.begin(), iconExtLower.end(),
				iconExtLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			const std::filesystem::path embedTargetExe = outDir
				/ (outputExecutableStem + std::filesystem::path(runtimeExecutableFilename).extension().string());
			if (!std::filesystem::exists(embedTargetExe)) {
				FNK_WARN_TAG("Build", "Cannot embed icon — built executable not found at {}",
					embedTargetExe.string());
			}
			else {
				std::vector<std::uint8_t> icoBytes;
				std::string buildErr;
				// An authored .ico already carries hand-tuned sizes; embed it verbatim.
				if (iconExtLower == ".ico") {
					buildErr = ReadIcoFileBytes(appIconSource, icoBytes);
				}
				else {
					buildErr = BuildIcoBytesFromImage(*appIcon, icoBytes);
				}
				if (!buildErr.empty()) {
					FNK_WARN_TAG("Build", "Failed to prepare icon ({}): {}",
						appIconSource.filename().string(), buildErr);
				}
				else {
					const std::string err = EmbedIconIntoExecutable(
						embedTargetExe.string(), icoBytes);
					if (err.empty()) {
						FNK_INFO_TAG("Build", "Embedded icon: {}", appIconSource.filename().string());
					}
					else {
						FNK_WARN_TAG("Build", "Failed to embed icon: {}", err);
					}
				}
			}
		}
#endif

#if defined(FNK_PLATFORM_MACOS)
		if (!platformPackageBuild) {
			report(0.92f, "Creating app bundle");
			std::filesystem::path bundleExecutable;
			std::string bundleError;
			if (!CreateMacAppBundle(outDir, outputExecutableStem, *project, appIcon.get(), bundleExecutable, bundleError)) {
				FNK_ERROR_TAG("Build", "Failed to create the app bundle: {}", bundleError);
				failBuild("App bundle creation failed");
				return result;
			}
			result.ExecutablePath = Path::ToUtf8(finalOutDir / bundleExecutable.lexically_relative(outDir));
		}
#endif

		const BuildOutputSafety::DirectoryFingerprint finalAssetFingerprint =
			BuildOutputSafety::CaptureDirectoryFingerprint(project->AssetsDirectory);
		if (!(assetInputFingerprint == finalAssetFingerprint)) {
			FNK_ERROR_TAG("Build", "Project Assets changed during the build; discarding mixed-version output.");
			failBuild("Project assets changed during build");
			return result;
		}

		if (platformPackageBuild) {
			report(0.80f, "Building with " + platformPackage->Name);
			PlatformBuilder::BuildRequest platformRequest;
			platformRequest.EngineRoot = engineRoot;
			platformRequest.ProjectRoot = Path::FromUtf8(project->RootDirectory);
			platformRequest.ProjectName = project->Name;
			platformRequest.ContentDir = contentDir;
			platformRequest.OutputDir = outDir;
			platformRequest.Configuration = project->ActiveBuildProfile == FunkeProject::BuildProfile::Release ? "Release" : "Development";
			if (project->ActiveBuildProfile == FunkeProject::BuildProfile::Debug) {
				// Platform runtimes have no debug engine build; scripts still compile as Debug.
				FNK_INFO_TAG("Build", "{} has no Debug runtime; packaging the Development runtime with Debug scripts.",
					FunkeBuildProfile::PlatformDisplayName(request.Platform));
			}
			platformRequest.AppName = project->Name;
			platformRequest.Version = project->BuildVersion.empty() ? std::string("1.0.0") : project->BuildVersion;
			platformRequest.Company = project->BuildCompany;
			platformRequest.EntityBits = project->EntityBits;
			platformRequest.Modules = project->Modules;
			platformRequest.Options = std::move(request.PlatformOptions);
			platformRequest.AppId = project->GetApplicationIdentifier();
			if (hasStagedAppIcon) platformRequest.IconPath = stagedAppIcon;
			const PlatformBuilder::BuildResult platformResult = PlatformBuilder::Build(*platformPackage, platformRequest,
				[&](float progress, std::string_view stage) {
					report(0.80f + 0.15f * progress, stage);
				});
			if (!platformResult.Succeeded) {
				FNK_ERROR_TAG("Build", "{} build failed: {}", FunkeBuildProfile::PlatformDisplayName(targetPlatform),
					platformResult.Error);
				failBuild(std::string(FunkeBuildProfile::PlatformDisplayName(targetPlatform)) + " build failed");
				return result;
			}
			std::error_code contentError;
			std::filesystem::remove_all(contentDir, contentError);
			std::filesystem::path artifactRelative = platformResult.Artifact.lexically_relative(outDir);
			if (artifactRelative.empty() || *artifactRelative.begin() == "..") artifactRelative = platformResult.Artifact.filename();
			FNK_INFO_TAG("Build", "{} artifact: {}", FunkeBuildProfile::PlatformDisplayName(targetPlatform),
				Path::ToUtf8(finalOutDir / artifactRelative));
		}

		report(0.95f, "Finalizing");
		std::string publishError;
		if (!stagedOutput.Publish(publishError)) {
			FNK_ERROR_TAG("Build", "Failed to publish staged output: {}", publishError);
			failBuild("Build publish failed");
			return result;
		}
		float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - buildStart).count();
		FNK_INFO_TAG("Build", "Build completed in {:.2f}s -> {}", elapsed, result.OutputDirectory);
		report(1.0f, "Build complete");

		result.Succeeded = true;
		result.Stage = "Build complete";
		return result;
	}

} // namespace Funke::GameBuilder
