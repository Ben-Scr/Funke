#include "pch.hpp"
#include "Commands.hpp"

#include "Serialization/Path.hpp"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

#ifdef IDX_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdlib>
#include <fstream>
#endif

namespace Index::Cli {

#ifdef IDX_PLATFORM_WINDOWS
	namespace {
		std::wstring Normalize(std::wstring entry) {
			while (!entry.empty() && (entry.back() == L'\\' || entry.back() == L'/')) entry.pop_back();
			std::transform(entry.begin(), entry.end(), entry.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
			return entry;
		}

		std::vector<std::wstring> Split(const std::wstring& value) {
			std::vector<std::wstring> entries;
			std::size_t start = 0;
			while (start <= value.size()) {
				const std::size_t end = value.find(L';', start);
				std::wstring entry = value.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
				if (!entry.empty()) entries.push_back(std::move(entry));
				if (end == std::wstring::npos) break;
				start = end + 1;
			}
			return entries;
		}
	}
#endif

	int RunSetupPath(Args& args) {
		const bool remove = args.Flag("remove");
		std::string error;
		if (!args.Finish(error)) return Fail(error, k_ExitUsage);
#ifdef IDX_PLATFORM_WINDOWS
		const std::wstring directory = Path::FromUtf8(Path::ExecutableDir()).lexically_normal().wstring();

		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_READ | KEY_WRITE, &key) != ERROR_SUCCESS) {
			return Fail("cannot open HKEY_CURRENT_USER\\Environment");
		}
		std::wstring current;
		DWORD type = REG_EXPAND_SZ;
		DWORD bytes = 0;
		if (RegQueryValueExW(key, L"Path", nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS && bytes > 0) {
			current.resize(bytes / sizeof(wchar_t));
			RegQueryValueExW(key, L"Path", nullptr, &type, reinterpret_cast<BYTE*>(current.data()), &bytes);
			while (!current.empty() && current.back() == L'\0') current.pop_back();
		}
		// Keep REG_SZ paths REG_SZ; anything else (missing, expand) becomes REG_EXPAND_SZ like the system dialog writes.
		if (type != REG_SZ) type = REG_EXPAND_SZ;

		std::vector<std::wstring> entries = Split(current);
		const std::wstring target = Normalize(directory);
		const auto matches = [&](const std::wstring& entry) { return Normalize(entry) == target; };
		const bool present = std::any_of(entries.begin(), entries.end(), matches);

		if (remove == !present) {
			RegCloseKey(key);
			Out(remove ? "Not on your PATH; nothing to remove." : "Already on your PATH: " + Path::ToUtf8(directory));
			return k_ExitOk;
		}
		if (remove) entries.erase(std::remove_if(entries.begin(), entries.end(), matches), entries.end());
		else entries.push_back(directory);

		std::wstring updated;
		for (const std::wstring& entry : entries) {
			if (!updated.empty()) updated += L';';
			updated += entry;
		}
		const LSTATUS status = RegSetValueExW(key, L"Path", 0, type, reinterpret_cast<const BYTE*>(updated.c_str()),
			static_cast<DWORD>((updated.size() + 1) * sizeof(wchar_t)));
		RegCloseKey(key);
		if (status != ERROR_SUCCESS) return Fail("could not write the user PATH");

		// Lets Explorer (and terminals started from it) pick up the change without a sign-out.
		DWORD_PTR ignored = 0;
		SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"),
			SMTO_ABORTIFHUNG, 5000, &ignored);
		Out(remove ? "Removed " + Path::ToUtf8(directory) + " from your PATH."
			: "Added " + Path::ToUtf8(directory) + " to your PATH. Open a new terminal to use `index`.");
		return k_ExitOk;
#else
		// A launcher script in ~/.local/bin (the XDG user bin folder) instead of editing shell profiles. Not a symlink:
		// the tool resolves its engine files and libraries next to its real location.
		const char* home = std::getenv("HOME");
		if (!home || !*home) return Fail("HOME is not set");
		const std::filesystem::path binDirectory = std::filesystem::path(home) / ".local" / "bin";
		const std::filesystem::path launcher = binDirectory / "index";
		const std::string marker = "# Added by index setup-path";
		std::error_code ec;

		const auto isOurs = [&]() {
			std::ifstream existing(launcher);
			std::string line;
			while (std::getline(existing, line)) if (line == marker) return true;
			return false;
		};
		if (remove) {
			if (!std::filesystem::exists(launcher, ec)) {
				Out("Not on your PATH; nothing to remove.");
				return k_ExitOk;
			}
			if (!isOurs()) return Fail(Path::ToUtf8(launcher) + " was not created by index setup-path; leaving it alone");
			std::filesystem::remove(launcher, ec);
			if (ec) return Fail("could not remove " + Path::ToUtf8(launcher) + ": " + ec.message());
			Out("Removed " + Path::ToUtf8(launcher) + ".");
			return k_ExitOk;
		}

		if (std::filesystem::exists(launcher, ec) && !isOurs()) {
			return Fail(Path::ToUtf8(launcher) + " already exists and was not created by index setup-path");
		}
		std::filesystem::create_directories(binDirectory, ec);
		std::string executable = Path::ToUtf8(Path::FromUtf8(Path::ExecutableDir()) / "index");
		std::string quoted = "'";
		for (char c : executable) quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
		quoted += "'";
		{
			std::ofstream out(launcher, std::ios::trunc);
			out << "#!/bin/sh\n" << marker << "\nexec " << quoted << " \"$@\"\n";
			if (!out) return Fail("could not write " + Path::ToUtf8(launcher));
		}
		std::filesystem::permissions(launcher, std::filesystem::perms::owner_all | std::filesystem::perms::group_read
			| std::filesystem::perms::group_exec | std::filesystem::perms::others_read | std::filesystem::perms::others_exec,
			std::filesystem::perm_options::replace, ec);

		const char* pathVariable = std::getenv("PATH");
		const std::string searchPath = std::string(":") + (pathVariable ? pathVariable : "") + ":";
		Out("Installed " + Path::ToUtf8(launcher) + ".");
		if (searchPath.find(":" + Path::ToUtf8(binDirectory) + ":") == std::string::npos) {
			Out("~/.local/bin is not on your PATH yet. Add this line to ~/.profile (bash) or ~/.zprofile (zsh), "
				"then open a new terminal:\n  export PATH=\"$HOME/.local/bin:$PATH\"");
		}
		return k_ExitOk;
#endif
	}

} // namespace Index::Cli
