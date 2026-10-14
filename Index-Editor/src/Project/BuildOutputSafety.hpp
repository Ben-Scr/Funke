#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace Index::BuildOutputSafety {

	enum class OutputPathError {
		None,
		Empty,
		ResolutionFailed,
		FilesystemRoot,
		ProjectRoot,
		ContainsProject,
		ProjectDirectory,
		AssetsDirectory,
		ContainsAssetsDirectory,
		ExistingFile,
		UnownedExistingDirectory,
	};

	inline constexpr std::string_view OwnershipMarkerFilename = ".index-build-output";

	struct OutputPathValidation {
		std::filesystem::path CanonicalOutput;
		OutputPathError Error = OutputPathError::None;

		bool IsValid() const { return Error == OutputPathError::None; }
	};

	struct DirectoryEntryFingerprint {
		std::string RelativePath;
		std::uintmax_t Size = 0;
		std::filesystem::file_time_type LastWriteTime{};

		bool operator==(const DirectoryEntryFingerprint&) const = default;
	};

	struct DirectoryFingerprint {
		std::vector<DirectoryEntryFingerprint> Entries;
		std::string Error;

		bool Succeeded() const { return Error.empty(); }
		bool operator==(const DirectoryFingerprint& other) const {
			return Succeeded() && other.Succeeded() && Entries == other.Entries;
		}
	};

	inline std::filesystem::path NormalizePath(const std::filesystem::path& path,
		std::error_code& error)
	{
		error.clear();
		std::filesystem::path absolute = std::filesystem::absolute(path, error);
		if (error) return {};
		std::filesystem::path normalized = std::filesystem::weakly_canonical(absolute, error);
		// libstdc++ keeps the separator left by a resolved "..": "Project/Builds/.." becomes "Project/".
		if (!error && normalized.has_relative_path() && normalized.filename().empty()) {
			normalized = normalized.parent_path();
		}
		return normalized;
	}

	inline bool ComponentsEqual(const std::filesystem::path& lhs,
		const std::filesystem::path& rhs)
	{
		std::string left = lhs.generic_string();
		std::string right = rhs.generic_string();
#ifdef IDX_PLATFORM_WINDOWS
		std::transform(left.begin(), left.end(), left.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		std::transform(right.begin(), right.end(), right.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
		return left == right;
	}

	inline bool IsSameOrDescendant(const std::filesystem::path& candidate,
		const std::filesystem::path& directory)
	{
		auto candidateIt = candidate.begin();
		for (auto directoryIt = directory.begin(); directoryIt != directory.end();
			++directoryIt, ++candidateIt)
		{
			if (candidateIt == candidate.end() || !ComponentsEqual(*candidateIt, *directoryIt)) {
				return false;
			}
		}
		return true;
	}

	inline bool IsSafePathSegment(std::string_view value) {
		if (value.empty() || value == "." || value == "..") return false;
		if (value.back() == ' ' || value.back() == '.') return false;
		for (const unsigned char character : value) {
			if (character < 32
				|| character == '<' || character == '>' || character == ':' || character == '"'
				|| character == '/' || character == '\\' || character == '|' || character == '?'
				|| character == '*') {
				return false;
			}
		}
#ifdef IDX_PLATFORM_WINDOWS
		std::string deviceName(value.substr(0, value.find('.')));
		std::transform(deviceName.begin(), deviceName.end(), deviceName.begin(),
			[](unsigned char character) { return static_cast<char>(std::toupper(character)); });
		if (deviceName == "CON" || deviceName == "PRN" || deviceName == "AUX" || deviceName == "NUL") {
			return false;
		}
		if (deviceName.size() == 4
			&& (deviceName.starts_with("COM") || deviceName.starts_with("LPT"))
			&& deviceName[3] >= '1' && deviceName[3] <= '9') {
			return false;
		}
#endif
		return true;
	}

	inline bool IsSafeRelativePath(const std::filesystem::path& path) {
		if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
			return false;
		}
		for (const auto& component : path) {
			if (component.empty() || component == "." || component == "..") return false;
		}
		return true;
	}

	inline bool ContainsRequiredFiles(const std::filesystem::path& directory,
		std::initializer_list<std::filesystem::path> relativePaths)
	{
		if (directory.empty()) return false;
		std::error_code error;
		for (const auto& relativePath : relativePaths) {
			error.clear();
			if (!IsSafeRelativePath(relativePath)
				|| !std::filesystem::is_regular_file(directory / relativePath, error)
				|| error) {
				return false;
			}
		}
		return true;
	}

	inline std::filesystem::path FindFirstCompleteArtifactDirectory(
		const std::vector<std::filesystem::path>& candidates,
		std::initializer_list<std::filesystem::path> requiredFiles)
	{
		for (const auto& candidate : candidates) {
			if (ContainsRequiredFiles(candidate, requiredFiles)) return candidate;
		}
		return {};
	}

	enum class RuntimePayloadKind {
		Workspace,
		BundledDevelopment,
		BundledDist,
	};

	inline bool IsProfilerRuntimeLibraryRequired(bool profilerEnabled,
		RuntimePayloadKind payloadKind)
	{
		return profilerEnabled && payloadKind != RuntimePayloadKind::BundledDist;
	}

	inline DirectoryFingerprint CaptureDirectoryFingerprint(const std::filesystem::path& root) {
		DirectoryFingerprint result;
		std::error_code error;
		if (!std::filesystem::is_directory(root, error) || error) {
			result.Error = error ? error.message() : "directory does not exist";
			return result;
		}

		std::filesystem::recursive_directory_iterator iterator(root, error);
		const std::filesystem::recursive_directory_iterator end;
		if (error) {
			result.Error = error.message();
			return result;
		}
		while (iterator != end) {
			const auto status = iterator->symlink_status(error);
			if (error) {
				result.Error = error.message();
				return result;
			}
			if (std::filesystem::is_symlink(status)) {
				result.Error = "symbolic links are not supported: " + iterator->path().string();
				return result;
			}
			if (std::filesystem::is_regular_file(status)) {
				const auto relativePath = iterator->path().lexically_relative(root);
				if (!IsSafeRelativePath(relativePath)) {
					result.Error = "unsafe relative path: " + iterator->path().string();
					return result;
				}
				const std::uintmax_t size = iterator->file_size(error);
				if (error) {
					result.Error = error.message();
					return result;
				}
				const auto writeTime = iterator->last_write_time(error);
				if (error) {
					result.Error = error.message();
					return result;
				}
				result.Entries.push_back({ relativePath.generic_string(), size, writeTime });
			}
			else if (!std::filesystem::is_directory(status)) {
				result.Error = "unsupported directory entry: " + iterator->path().string();
				return result;
			}

			iterator.increment(error);
			if (error) {
				result.Error = error.message();
				return result;
			}
		}
		std::sort(result.Entries.begin(), result.Entries.end(),
			[](const auto& lhs, const auto& rhs) { return lhs.RelativePath < rhs.RelativePath; });
		return result;
	}

	inline OutputPathError ValidateExistingOutput(const std::filesystem::path& outputDirectory) {
		std::error_code error;
		if (!std::filesystem::exists(outputDirectory, error)) {
			return error ? OutputPathError::ResolutionFailed : OutputPathError::None;
		}
		if (!std::filesystem::is_directory(outputDirectory, error) || error) {
			return OutputPathError::ExistingFile;
		}

		const bool isEmpty = std::filesystem::is_empty(outputDirectory, error);
		if (error) return OutputPathError::ResolutionFailed;
		if (isEmpty) return OutputPathError::None;

		const std::filesystem::path marker = outputDirectory / std::string(OwnershipMarkerFilename);
		if (!std::filesystem::is_regular_file(marker, error) || error) {
			return OutputPathError::UnownedExistingDirectory;
		}
		return OutputPathError::None;
	}

	inline bool LooksLikeLegacyIndexBuildOutput(const std::filesystem::path& outputDirectory) {
		std::error_code error;
		if (!std::filesystem::is_regular_file(outputDirectory / "project.json", error) || error) return false;
		if (!std::filesystem::is_directory(outputDirectory / "Assets", error) || error) return false;
		for (const char* engineLibrary : {
			"Index-Engine.dll", "libIndex-Engine.so", "libIndex-Engine.dylib" }) {
			error.clear();
			if (std::filesystem::is_regular_file(outputDirectory / engineLibrary, error) && !error) {
				return true;
			}
		}
		return false;
	}

	inline OutputPathValidation ValidateOutputPath(const std::filesystem::path& projectRoot,
		const std::filesystem::path& assetsDirectory,
		const std::filesystem::path& outputDirectory)
	{
		if (projectRoot.empty() || assetsDirectory.empty() || outputDirectory.empty()) {
			return { {}, OutputPathError::Empty };
		}

		std::error_code error;
		const std::filesystem::path canonicalProject = NormalizePath(projectRoot, error);
		if (error) return { {}, OutputPathError::ResolutionFailed };
		const std::filesystem::path canonicalAssets = NormalizePath(assetsDirectory, error);
		if (error) return { {}, OutputPathError::ResolutionFailed };
		const std::filesystem::path canonicalOutput = NormalizePath(outputDirectory, error);
		if (error) return { {}, OutputPathError::ResolutionFailed };
		const std::filesystem::path canonicalBuilds = NormalizePath(canonicalProject / "Builds", error);
		if (error) return { {}, OutputPathError::ResolutionFailed };

		if (canonicalOutput == canonicalOutput.root_path() || canonicalOutput.filename().empty()) {
			return { canonicalOutput, OutputPathError::FilesystemRoot };
		}
		if (ComponentsEqual(canonicalOutput, canonicalProject)) {
			return { canonicalOutput, OutputPathError::ProjectRoot };
		}
		if (IsSameOrDescendant(canonicalProject, canonicalOutput)) {
			return { canonicalOutput, OutputPathError::ContainsProject };
		}
		if (IsSameOrDescendant(canonicalOutput, canonicalAssets)) {
			return { canonicalOutput, OutputPathError::AssetsDirectory };
		}
		if (IsSameOrDescendant(canonicalAssets, canonicalOutput)) {
			return { canonicalOutput, OutputPathError::ContainsAssetsDirectory };
		}
		if (IsSameOrDescendant(canonicalOutput, canonicalProject)
			&& !IsSameOrDescendant(canonicalOutput, canonicalBuilds)) {
			return { canonicalOutput, OutputPathError::ProjectDirectory };
		}
		if (const OutputPathError existingError = ValidateExistingOutput(canonicalOutput);
			existingError != OutputPathError::None) {
			if (existingError == OutputPathError::UnownedExistingDirectory
				&& LooksLikeLegacyIndexBuildOutput(canonicalOutput)) {
				return { canonicalOutput, OutputPathError::None };
			}
			return { canonicalOutput, existingError };
		}
		return { canonicalOutput, OutputPathError::None };
	}

	inline const char* GetErrorMessage(OutputPathError error) {
		switch (error) {
			case OutputPathError::Empty:
				return "Project, asset, and output paths must not be empty.";
			case OutputPathError::ResolutionFailed:
				return "The output path could not be resolved safely.";
			case OutputPathError::FilesystemRoot:
				return "The output directory cannot be a filesystem root.";
			case OutputPathError::ProjectRoot:
				return "The output directory cannot be the project root.";
			case OutputPathError::ContainsProject:
				return "The output directory cannot contain the project directory.";
			case OutputPathError::ProjectDirectory:
				return "Output inside the project must be placed under its Builds directory.";
			case OutputPathError::AssetsDirectory:
				return "The output directory cannot be the Assets directory or one of its descendants.";
			case OutputPathError::ContainsAssetsDirectory:
				return "The output directory cannot contain the Assets directory.";
			case OutputPathError::ExistingFile:
				return "The output path already exists and is not a directory.";
			case OutputPathError::UnownedExistingDirectory:
				return "The output directory contains unrelated files and is not owned by Index.";
			case OutputPathError::None:
				break;
		}
		return "";
	}

}
