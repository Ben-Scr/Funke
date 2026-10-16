#include <pch.hpp>
#include "Path.hpp"

#include "Core/Platform.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"

#include <optional>

#ifdef FNK_PLATFORM_WINDOWS
#include <windows.h>
#include <shlobj.h>
#endif

namespace Funke {
    std::string Path::GetSpecialFolderPath(SpecialFolder folder)
    {
#ifdef FNK_PLATFORM_WINDOWS
        PWSTR path = nullptr;
        HRESULT hr = E_FAIL;

        switch (folder) {
        case SpecialFolder::User: hr = SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &path); break;
        case SpecialFolder::Desktop: hr = SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path); break;
        case SpecialFolder::Documents: hr = SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &path); break;
        case SpecialFolder::Downloads: hr = SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path); break;
        case SpecialFolder::Music: hr = SHGetKnownFolderPath(FOLDERID_Music, 0, nullptr, &path); break;
        case SpecialFolder::Pictures: hr = SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &path); break;
        case SpecialFolder::Videos: hr = SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &path); break;
        case SpecialFolder::AppDataRoaming: hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path); break;
        case SpecialFolder::LocalAppData: hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path); break;
        case SpecialFolder::ProgramData: hr = SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &path); break;
        default: FNK_THROW(FunkeErrorCode::InvalidArgument, "Unknown SpecialFolder");
        }

        FNK_ASSERT(!FAILED(hr), FunkeErrorCode::Undefined, "Failed to get folder path");

        const int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
        // size==0 means WideCharToMultiByte failed; without a guard, std::string(size-1,...)
        // becomes std::string(SIZE_MAX, 0) and crashes the process.
        if (size <= 0) {
            CoTaskMemFree(path);
            return {};
        }
        std::string result(static_cast<size_t>(size), '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, path, -1, result.data(), size, nullptr, nullptr) != size) {
            CoTaskMemFree(path);
            return {};
        }
        result.pop_back();

        CoTaskMemFree(path);
        return result;
#else
        auto userFolder = [](Platform::UserFolder userFolder) {
            std::filesystem::path path = Platform::GetUserFolder(userFolder);
            // Sandboxed platforms (mobile, Web) have no user folders; their data root is the only writable place.
            if (path.empty()) path = Platform::GetUserDataRoot();
            return ToUtf8(path.lexically_normal());
        };

        switch (folder) {
        case SpecialFolder::User: return userFolder(Platform::UserFolder::Home);
        case SpecialFolder::Desktop: return userFolder(Platform::UserFolder::Desktop);
        case SpecialFolder::Documents: return userFolder(Platform::UserFolder::Documents);
        case SpecialFolder::Downloads: return userFolder(Platform::UserFolder::Downloads);
        case SpecialFolder::Music: return userFolder(Platform::UserFolder::Music);
        case SpecialFolder::Pictures: return userFolder(Platform::UserFolder::Pictures);
        case SpecialFolder::Videos: return userFolder(Platform::UserFolder::Videos);
        case SpecialFolder::AppDataRoaming:
        case SpecialFolder::LocalAppData:
            return ToUtf8(Platform::GetUserDataRoot());
        case SpecialFolder::ProgramData:
#if defined(FNK_PLATFORM_MACOS)
            return "/Library/Application Support";
#elif defined(FNK_PLATFORM_LINUX)
            return "/usr/share";
#else
            return ToUtf8(Platform::GetUserDataRoot());
#endif
        default:
            FNK_THROW(FunkeErrorCode::InvalidArgument, "Unknown SpecialFolder");
        }
#endif
    }

    namespace {
        std::string& ExecutableDirOverride() {
            static std::string s_Override;
            return s_Override;
        }
    }

    void Path::SetExecutableDirOverride(const std::string& directory)
    {
        ExecutableDirOverride() = directory;
    }

    std::string Path::ExecutableDir()
    {
        if (!ExecutableDirOverride().empty()) return ExecutableDirOverride();
#ifdef FNK_PLATFORM_WINDOWS
        // Grow the buffer until the path fits — silent truncation at MAX_PATH would
        // produce a wrong directory on long-installation paths (the registry-key value
        // is `LongPathsEnabled = 1` in modern Windows, paths of 1000+ chars are real).
        std::vector<wchar_t> buf(MAX_PATH);
        for (;;) {
            DWORD copied = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (copied == 0) {
                FNK_THROW(FunkeErrorCode::Undefined, "GetModuleFileNameW failed");
            }
            if (copied < buf.size()) break; // fit
            // Buffer was too small — Windows null-terminates and returns size on truncation.
            buf.resize(buf.size() * 2);
            if (buf.size() > 65536) {
                FNK_THROW(FunkeErrorCode::Undefined, "Executable path > 64KB; refusing");
            }
        }
        const auto utf8 = std::filesystem::path(buf.data()).parent_path().u8string();
        return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
#else
        // SDL_GetBasePath covers /proc/self/exe, macOS bundles, Android assets and the Web VFS root.
        const std::filesystem::path dir = Platform::GetExecutableDirectory();
        if (dir.empty())
            FNK_THROW(FunkeErrorCode::Undefined, "Failed to resolve executable path");
        std::string result = ToUtf8(dir);
        while (result.size() > 1 && (result.back() == '/' || result.back() == '\\')) result.pop_back();
        return result;
#endif
    }

    std::string Path::ResolveFunkeAssets(const std::string& subdirectory) {
        std::string exeDir = ExecutableDir();

        // 1. Packaged build: FunkeAssets next to the executable
        std::string packed = Combine(exeDir, "FunkeAssets", subdirectory);
        if (std::filesystem::exists(FromUtf8(packed)))
            return packed;

        // 2. Dev layout: shared FunkeAssets one level up (bin/<config>/FunkeAssets/)
        std::string dev = Combine(exeDir, "..", "FunkeAssets", subdirectory);
        if (std::filesystem::exists(FromUtf8(dev)))
            return ToUtf8(std::filesystem::canonical(FromUtf8(dev)));

        return {};
    }

    std::string Path::ToProjectRelativeDisplay(const std::string& absolutePath) {
        if (absolutePath.empty()) return {};

        std::error_code ec;
        std::filesystem::path input = FromUtf8(absolutePath);
        std::filesystem::path canonInput = std::filesystem::weakly_canonical(input, ec);
        if (ec || canonInput.empty()) canonInput = input;

        auto tryRelative = [&canonInput](const std::string& rootStr,
                                         const std::string& displayPrefix) -> std::optional<std::string> {
            if (rootStr.empty()) return std::nullopt;
            std::error_code rec;
            std::filesystem::path canonRoot = std::filesystem::weakly_canonical(
                FromUtf8(rootStr), rec);
            if (rec || canonRoot.empty()) canonRoot = FromUtf8(rootStr);

            std::filesystem::path rel = canonInput.lexically_relative(canonRoot);
			const std::string relString = ToGenericUtf8(rel);
            // lexically_relative returns "." for equality, "" when the roots
            // differ (e.g. C: vs D:), and a "..-prefix" when the input is
            // outside the root. None of those are valid display strings.
            if (relString.empty() || relString == ".") return std::nullopt;
            if (relString.rfind("..", 0) == 0) return std::nullopt;
            if (displayPrefix.empty()) return relString;
            return displayPrefix + "/" + relString;
        };

        if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
            if (auto rel = tryRelative(project->RootDirectory, {}); rel) {
                return *rel;
            }
        }

        // Engine-shipped FunkeAssets (e.g. the default font when no project is
        // loaded, or the editor itself walking the engine assets directory).
        const std::string engineFunkeAssets = ResolveFunkeAssets("");
        if (!engineFunkeAssets.empty()) {
            if (auto rel = tryRelative(engineFunkeAssets, "FunkeAssets"); rel) {
                return *rel;
            }
        }

        // Last-resort fallback: surface just the filename so we never leak the
        // full machine-local path even for unexpected inputs.
        return ToUtf8(FromUtf8(absolutePath).filename());
    }
}
