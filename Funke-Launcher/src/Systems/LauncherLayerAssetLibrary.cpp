#include "Systems/LauncherLayer.hpp"
#include "Gui/LauncherUI.hpp"
#include "Gui/ImGuiDialog.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Localization/Localization.hpp"
#include "Core/Log.hpp"
#include "Serialization/Path.hpp"
#include "Core/Version.hpp"
#include "Serialization/Json.hpp"
#include "Utils/Hash.hpp"
#include "Utils/PackageToolPath.hpp"
#include "Utils/Process.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <format>
#include <sstream>
#include <stdexcept>

namespace Funke {
	using namespace LauncherUI;

	namespace {

		constexpr const char* k_DefaultAssetLibraryUrl =
			"https://raw.githubusercontent.com/Ben-Scr/Funke-AssetLibrary/main/index.json";

		std::string ResolveAssetLibraryUrl() {
			if (const char* env = std::getenv("FUNKE_ASSET_LIBRARY_URL"); env && *env) {
				return env;
			}
			return k_DefaultAssetLibraryUrl;
		}

		// Whitelist limits to GitHub: prevents a compromised mirror redirecting downloads.
		bool IsTrustedArchiveHost(std::string_view url) {
			constexpr std::string_view k_Trusted[] = {
				"https://github.com/",
				"https://objects.githubusercontent.com/",
				"https://raw.githubusercontent.com/",
				"https://codeload.github.com/",
			};
			for (auto prefix : k_Trusted) {
				if (url.size() >= prefix.size()
					&& std::string_view(url.data(), prefix.size()) == prefix) {
					return true;
				}
			}
			return false;
		}

		std::vector<std::string> JsonStringArray(const Json::Value* v) {
			std::vector<std::string> out;
			if (!v || !v->IsArray()) return out;
			for (const auto& item : v->GetArray()) {
				if (item.IsString()) out.push_back(item.AsStringOr(""));
			}
			return out;
		}

		LauncherLayer::AssetLibraryEntry ParseLibraryEntry(const Json::Value& obj) {
			LauncherLayer::AssetLibraryEntry e;
			auto str = [&](const char* k) -> std::string {
				const Json::Value* v = obj.FindMember(k);
				return v ? v->AsStringOr("") : "";
			};
			e.Id = str("id");
			e.Name = str("name");
			e.Author = str("author");
			e.ShortDescription = str("shortDescription");
			e.Description = str("description");
			e.Tags = JsonStringArray(obj.FindMember("tags"));
			e.Category = str("category");
			e.Version = str("version");
			e.License = str("license");
			e.ThumbnailUrl = str("thumbnail");
			e.ScreenshotUrls = JsonStringArray(obj.FindMember("screenshots"));
			if (const Json::Value* archive = obj.FindMember("archive"); archive && archive->IsObject()) {
				if (auto* v = archive->FindMember("url"); v) e.ArchiveUrl = v->AsStringOr("");
				if (auto* v = archive->FindMember("sha256"); v) e.ArchiveSha256 = Hash::ToLowerHex(v->AsStringOr(""));
				if (auto* v = archive->FindMember("sizeBytes"); v) e.ArchiveSizeBytes = v->AsUInt64Or(0);
				if (auto* v = archive->FindMember("rootInZip"); v) e.ArchiveRootInZip = v->AsStringOr("");
			}
			if (const Json::Value* engine = obj.FindMember("engine"); engine && engine->IsObject()) {
				if (auto* v = engine->FindMember("minVersion"); v) e.EngineMinVersion = v->AsStringOr("");
				if (auto* v = engine->FindMember("maxVersion"); v) e.EngineMaxVersion = v->AsStringOr("");
			}
			e.RequiredPackages = JsonStringArray(obj.FindMember("requiredPackages"));
			e.Homepage = str("homepage");
			e.SourceUrl = str("sourceUrl");
			return e;
		}

		// Accepts flat archives (project.json at root) and single-subdir archives; rejects anything else.
		std::filesystem::path PickExtractedProjectRoot(const std::filesystem::path& stagingDir) {
			std::error_code ec;
			if (std::filesystem::exists(stagingDir / "project.json", ec)) {
				return stagingDir;
			}
			std::filesystem::path onlySubdir;
			int dirCount = 0;
			int topFiles = 0;
			for (auto& entry : std::filesystem::directory_iterator(stagingDir, ec)) {
				if (ec) break;
				if (entry.is_directory(ec)) {
					onlySubdir = entry.path();
					++dirCount;
				}
				else if (entry.is_regular_file(ec)) {
					++topFiles;
				}
			}
			if (dirCount == 1 && topFiles == 0) {
				if (std::filesystem::exists(onlySubdir / "project.json", ec)) {
					return onlySubdir;
				}
			}
			return {};
		}

	} // anonymous namespace

	std::string LauncherLayer::AssetLibraryStageText(AssetLibraryStage stage,
		std::string_view displayName) const
	{
		switch (stage) {
		case AssetLibraryStage::Idle:
			return "";
		case AssetLibraryStage::FetchingManifest:
			return FNK_TR("launcher.asset_library.progress.fetching_manifest");
		case AssetLibraryStage::Downloading:
			return std::vformat(
				FNK_TR("launcher.asset_library.progress.downloading"),
				std::make_format_args(displayName));
		case AssetLibraryStage::Verifying:
			return FNK_TR("launcher.asset_library.progress.verifying");
		case AssetLibraryStage::Extracting:
			return FNK_TR("launcher.asset_library.progress.extracting");
		case AssetLibraryStage::Importing:
			return FNK_TR("launcher.asset_library.progress.importing");
		case AssetLibraryStage::Done:
			return FNK_TR("launcher.asset_library.progress.done");
		case AssetLibraryStage::Error:
			return FNK_TR("launcher.asset_library.progress.done"); // overlay handles error
		}
		return "";
	}

	const LauncherLayer::AssetLibraryEntry* LauncherLayer::FindAssetLibraryEntry(std::string_view id) const {
		for (const auto& entry : m_AssetLibrary.Entries) {
			if (entry.Id == id) return &entry;
		}
		return nullptr;
	}

	void LauncherLayer::StartFetchAssetLibraryIndex() {
		m_LibraryThumbnails.Refresh();
		{
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			if (m_AssetLibrary.FetchInFlight) return;
			m_AssetLibrary.FetchInFlight = true;
			m_AssetLibrary.FetchError.clear();
		}
		m_AssetLibraryFetchErrorReported = false; // re-arm one-shot error surfacing for this attempt

		// If a previous worker is still alive (download or fetch), join it
		// first. Single-slot pipeline by design.
		if (m_AssetLibraryTask.Worker.joinable()) m_AssetLibraryTask.Worker.join();

		// Boundary guard: a throw escaping a std::thread body calls std::terminate and kills the
		// launcher. The fetch shells out, parses JSON, and allocates — catch and publish into task state.
		m_AssetLibraryTask.Worker = std::thread([this]() {
			try {
			std::string error;
			std::vector<AssetLibraryEntry> parsed;
			int schemaVersion = 0;
			std::string generatedAt;

			const std::filesystem::path tool = PackageTool::ResolveExecutable();
			if (tool.empty()) {
				error = "Funke-PackageTool not found";
			}
			else {
				std::vector<std::string> cmd;
				if (tool.extension() == ".dll") {
					cmd.push_back("dotnet");
					cmd.push_back(tool.string());
				}
				else {
					cmd.push_back(tool.string());
				}
				cmd.push_back("github-index");
				cmd.push_back(ResolveAssetLibraryUrl());

				// github-index has the 15s top-level timeout still, but for a
				// small JSON manifest that's fine.
				const Process::Result result = Process::Run(cmd, {}, std::chrono::seconds(30));
				if (!result.Succeeded()) {
					error = "Couldn't fetch library index (exit code "
						+ std::to_string(result.ExitCode) + ")";
				}
				else {
					std::string parseError;
					Json::Value root;
					if (!Json::TryParse(result.Output, root, &parseError) || !root.IsObject()) {
						error = "Funke is not valid JSON: " + parseError;
					}
					else {
						if (auto* v = root.FindMember("schemaVersion"); v) schemaVersion = v->AsIntOr(0);
						if (auto* v = root.FindMember("generatedAt"); v) generatedAt = v->AsStringOr("");

						if (schemaVersion > 1) {
							error = "Funke schema is newer than this launcher supports";
						}
						else if (auto* entries = root.FindMember("entries"); entries && entries->IsArray()) {
							for (const auto& entryVal : entries->GetArray()) {
								if (!entryVal.IsObject()) continue;
								AssetLibraryEntry e = ParseLibraryEntry(entryVal);
								if (e.Id.empty() || e.Name.empty() || e.ArchiveUrl.empty()) continue;
								// The id becomes a path component for the download zip and staging dir; a remote
								// index must not be able to steer those outside .asset-library-staging.
								const bool safeId = e.Id != "." && e.Id != ".."
									&& std::all_of(e.Id.begin(), e.Id.end(), [](char c) {
										return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.';
									});
								if (!safeId) continue;
								parsed.push_back(std::move(e));
							}
						}
					}
				}
			}

			{
				std::scoped_lock lock(m_AssetLibraryTask.Mutex);
				m_AssetLibrary.SchemaVersion = schemaVersion;
				m_AssetLibrary.GeneratedAt = std::move(generatedAt);
				m_AssetLibrary.Entries = std::move(parsed);
				m_AssetLibrary.Loaded = error.empty();
				m_AssetLibrary.FetchInFlight = false;
				m_AssetLibrary.FetchError = error;
				m_AssetLibrary.LastRefreshedAt = std::chrono::steady_clock::now();
			}
			if (!error.empty()) {
				FNK_CORE_WARN_TAG("AssetLibrary", "Fetch failed: {}", error);
			}
			else {
				FNK_CORE_INFO_TAG("AssetLibrary", "Loaded {} entries", m_AssetLibrary.Entries.size());
			}
			}
			catch (const std::exception& e) {
				FNK_CORE_ERROR_TAG("AssetLibrary", "Fetch worker crashed: {}", e.what());
				std::scoped_lock lock(m_AssetLibraryTask.Mutex);
				m_AssetLibrary.Loaded = false;
				m_AssetLibrary.FetchInFlight = false;
				m_AssetLibrary.FetchError = std::string("Unexpected error: ") + e.what();
				m_AssetLibrary.LastRefreshedAt = std::chrono::steady_clock::now();
			}
			catch (...) {
				FNK_CORE_ERROR_TAG("AssetLibrary", "Fetch worker crashed: unknown exception");
				std::scoped_lock lock(m_AssetLibraryTask.Mutex);
				m_AssetLibrary.Loaded = false;
				m_AssetLibrary.FetchInFlight = false;
				m_AssetLibrary.FetchError = "Unexpected error";
				m_AssetLibrary.LastRefreshedAt = std::chrono::steady_clock::now();
			}
		});
	}

	void LauncherLayer::StartAssetLibraryDownload(const AssetLibraryEntry& entry) {
		// Bypass the GitHub host whitelist when an explicit override URL is
		// set — that's a dev/QA escape hatch (e.g. python -m http.server on
		// localhost). Default-config installs still get the strict check.
		const bool urlOverride = std::getenv("FUNKE_ASSET_LIBRARY_URL") != nullptr;
		if (!urlOverride && !IsTrustedArchiveHost(entry.ArchiveUrl)) {
			ShowError(FNK_TR("launcher.asset_library.error.untrusted_host"));
			return;
		}

		// Trust gate: show the one-shot disclosure modal the first time the
		// user downloads from this source URL on this machine. The download
		// kicks off only after acknowledgement.
		const std::string sourceKey = ResolveAssetLibraryUrl();
		auto it = m_Settings.AssetLibraryTrustAck.find(sourceKey);
		if (it == m_Settings.AssetLibraryTrustAck.end() || !it->second) {
			m_PendingTrustedDownload = entry;
			m_OpenAssetLibraryTrustPopup = true;
			return;
		}

		{
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			if (m_AssetLibraryTask.Running) return; // already downloading something
			m_AssetLibraryTask.EntryId = entry.Id;
			m_AssetLibraryTask.DisplayName = entry.Name;
			m_AssetLibraryTask.Stage = AssetLibraryStage::FetchingManifest;
			m_AssetLibraryTask.Progress = 0.0f;
			m_AssetLibraryTask.Error.clear();
			m_AssetLibraryTask.TargetProjectPath.clear();
			m_AssetLibraryTask.Running = true;
			m_AssetLibraryTask.Finished = false;
			m_AssetLibraryTask.Success = false;
		}

		// Join previous worker first (could be a finished fetch).
		if (m_AssetLibraryTask.Worker.joinable()) m_AssetLibraryTask.Worker.join();

		AssetLibraryEntry entryCopy = entry; // capture by value
		// Resolve the projects base dir HERE (render thread): m_Settings.DefaultProjectsLocation
		// is mutated by the settings UI on this thread, so the worker must not read it.
		std::string baseDir = m_Settings.DefaultProjectsLocation.empty()
			? FunkeProject::GetDefaultProjectsDir()
			: m_Settings.DefaultProjectsLocation;
		// Boundary guard: a throw escaping a std::thread body calls std::terminate and kills the
		// launcher. The worker touches the filesystem, parses JSON, and allocates — any of which can
		// throw. Catch here and publish the failure into the task state the poll loop already reads.
		m_AssetLibraryTask.Worker = std::thread(
			[this, entry = std::move(entryCopy), dir = std::move(baseDir)]() mutable {
				try {
					AssetLibraryDownloadWorkerBody(std::move(entry), std::move(dir));
				}
				catch (const std::exception& e) {
					FNK_CORE_ERROR_TAG("AssetLibrary", "Download worker crashed: {}", e.what());
					std::scoped_lock lock(m_AssetLibraryTask.Mutex);
					m_AssetLibraryTask.Stage = AssetLibraryStage::Error;
					m_AssetLibraryTask.Error = std::string("Unexpected error: ") + e.what();
					m_AssetLibraryTask.Running = false;
					m_AssetLibraryTask.Finished = true;
					m_AssetLibraryTask.Success = false;
				}
				catch (...) {
					FNK_CORE_ERROR_TAG("AssetLibrary", "Download worker crashed: unknown exception");
					std::scoped_lock lock(m_AssetLibraryTask.Mutex);
					m_AssetLibraryTask.Stage = AssetLibraryStage::Error;
					m_AssetLibraryTask.Error = "Unexpected error during import";
					m_AssetLibraryTask.Running = false;
					m_AssetLibraryTask.Finished = true;
					m_AssetLibraryTask.Success = false;
				}
			});
	}

	void LauncherLayer::AssetLibraryDownloadWorkerBody(AssetLibraryEntry entry, std::string baseDir) {
		auto setStage = [&](AssetLibraryStage stage, float progress) {
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			m_AssetLibraryTask.Stage = stage;
			m_AssetLibraryTask.Progress = progress;
		};
		auto fail = [&](std::string error) {
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			m_AssetLibraryTask.Stage = AssetLibraryStage::Error;
			m_AssetLibraryTask.Error = std::move(error);
			m_AssetLibraryTask.Running = false;
			m_AssetLibraryTask.Finished = true;
			m_AssetLibraryTask.Success = false;
		};

		const std::filesystem::path tool = PackageTool::ResolveExecutable();
		if (tool.empty()) { fail("Funke-PackageTool not found"); return; }

		// Engine version compatibility — refuse if we don't meet minVersion.
		if (!entry.EngineMinVersion.empty()
			&& !Version::IsAtLeast(FNK_VERSION, entry.EngineMinVersion))
		{
			fail("Requires Funke " + entry.EngineMinVersion);
			return;
		}

		setStage(AssetLibraryStage::Downloading, 0.0f);

		// Stage download to <projects>/.asset-library-staging/<id>-<rand>.zip.tmp.
		// baseDir was resolved on the render thread (passed in) — do NOT read
		// m_Settings.DefaultProjectsLocation here.
		std::error_code ec;
		const std::filesystem::path stagingRoot = std::filesystem::path(baseDir) / ".asset-library-staging";
		std::filesystem::create_directories(stagingRoot, ec);

		// Unique suffix lets two concurrent downloads of the same id coexist
		// even though the UI prevents it today — costs nothing to be defensive.
		const auto now = std::chrono::steady_clock::now().time_since_epoch();
		const std::string token = std::to_string(now.count());
		const std::filesystem::path tempZip = stagingRoot / (entry.Id + "-" + token + ".zip.tmp");
		const std::filesystem::path stagingDir = stagingRoot / (entry.Id + "-" + token);

		auto cleanup = [&]() {
			std::error_code _ec;
			std::filesystem::remove(tempZip, _ec);
			std::filesystem::remove_all(stagingDir, _ec);
		};

		std::vector<std::string> dlCmd;
		if (tool.extension() == ".dll") { dlCmd.push_back("dotnet"); dlCmd.push_back(tool.string()); }
		else { dlCmd.push_back(tool.string()); }
		dlCmd.push_back("github-download");
		dlCmd.push_back(entry.ArchiveUrl);
		dlCmd.push_back(tempZip.string());

		// Bounded timeout (not 0 = wait-forever): a stalled mirror would otherwise pin
		// this worker, and joining it on shutdown would hang the launcher on exit.
		const Process::Result dlResult = Process::Run(dlCmd, {}, std::chrono::milliseconds(180000));
		if (!dlResult.Succeeded()) {
			cleanup();
			fail("Download failed (exit code " + std::to_string(dlResult.ExitCode) + ")");
			return;
		}
		setStage(AssetLibraryStage::Verifying, 0.0f);

		if (!entry.ArchiveSha256.empty()) {
			std::string actual, hashError;
			if (!Hash::Sha256OfFile(tempZip, actual, hashError)) {
				cleanup();
				fail(hashError);
				return;
			}
			if (actual != entry.ArchiveSha256) {
				cleanup();
				fail(FNK_TR("launcher.asset_library.error.verification"));
				return;
			}
		}

		setStage(AssetLibraryStage::Extracting, 0.0f);
		std::filesystem::create_directories(stagingDir, ec);
		std::vector<std::string> unzipCmd;
		if (tool.extension() == ".dll") { unzipCmd.push_back("dotnet"); unzipCmd.push_back(tool.string()); }
		else { unzipCmd.push_back(tool.string()); }
		unzipCmd.push_back("unzip");
		unzipCmd.push_back(tempZip.string());
		unzipCmd.push_back(stagingDir.string());
		const Process::Result unzipResult = Process::Run(unzipCmd, {}, std::chrono::milliseconds(180000));
		if (!unzipResult.Succeeded()) {
			cleanup();
			fail("Extraction failed (exit code " + std::to_string(unzipResult.ExitCode) + ")");
			return;
		}

		// Locate the project root inside the extracted dir.
		std::filesystem::path projectRoot = PickExtractedProjectRoot(stagingDir);
		if (projectRoot.empty()) {
			cleanup();
			fail(FNK_TR("launcher.asset_library.error.no_project_in_archive"));
			return;
		}

		setStage(AssetLibraryStage::Importing, 0.0f);

		// Move into final <projects>/<sanitized-name>/ with collision suffix.
		auto sanitize = [](std::string_view s) {
			std::string out;
			out.reserve(s.size());
			for (char c : s) {
				if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?'
					|| c == '"' || c == '<' || c == '>' || c == '|') {
					out.push_back('-');
				}
				else { out.push_back(c); }
			}
			// "." / ".." (or a name of only dots) would resolve outside the projects directory.
			if (out.find_first_not_of('.') == std::string::npos) out = "Project";
			return out;
		};
		std::filesystem::path finalDir = std::filesystem::path(baseDir) / sanitize(entry.Name);
		for (int suffix = 1; std::filesystem::exists(finalDir, ec); ++suffix) {
			finalDir = std::filesystem::path(baseDir) / (sanitize(entry.Name) + "-" + std::to_string(suffix));
			if (suffix > 999) { cleanup(); fail("Couldn't find a free project name"); return; }
		}

		std::filesystem::rename(projectRoot, finalDir, ec);
		if (ec) {
			cleanup();
			fail("Couldn't move project into place: " + ec.message());
			return;
		}
		// Drop the staging dir if anything left over (only the picked subdir
		// gets renamed; siblings — there shouldn't be any — are removed).
		std::filesystem::remove_all(stagingDir, ec);
		std::filesystem::remove(tempZip, ec);

		FunkeProject loaded;
		std::string loadError;
		if (!FunkeProject::TryLoad(finalDir.string(), loaded, &loadError)) {
			// Not registered anywhere, so remove it; otherwise each retry leaves a Name-1, Name-2, ... orphan.
			std::filesystem::remove_all(finalDir, ec);
			fail("Project failed to load: " + loadError);
			return;
		}

		const std::string finalName = loaded.Name.empty() ? entry.Name : loaded.Name;
		// NOTE: registry mutation (AddProject/Save) is deferred to the render thread
		// in PollAssetLibraryTask — LauncherRegistry is unsynchronized and the render
		// thread iterates it every frame. The worker only publishes the result.
		{
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			m_AssetLibraryTask.Stage = AssetLibraryStage::Done;
			m_AssetLibraryTask.Progress = 1.0f;
			m_AssetLibraryTask.Running = false;
			m_AssetLibraryTask.Finished = true;
			m_AssetLibraryTask.Success = true;
			m_AssetLibraryTask.TargetProjectPath = finalDir.string();
			m_AssetLibraryTask.FinalName = finalName;
		}
		FNK_CORE_INFO_TAG("AssetLibrary", "Imported '{}' to '{}'",
			finalName, finalDir.string());
	}

	void LauncherLayer::PollAssetLibraryTask() {
		bool finishedThisFrame = false;
		std::string targetPath;
		std::string finalName;
		std::string downloadError;   // non-empty => download/import failed
		std::string fetchError;      // non-empty => index fetch failed
		{
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			if (m_AssetLibraryTask.Finished && !m_AssetLibraryTask.Running
				&& m_AssetLibraryTask.Stage != AssetLibraryStage::Idle)
			{
				if (m_AssetLibraryTask.Success) {
					finishedThisFrame = true;
					targetPath = m_AssetLibraryTask.TargetProjectPath;
					finalName = m_AssetLibraryTask.FinalName;
				}
				else if (m_AssetLibraryTask.Stage == AssetLibraryStage::Error) {
					downloadError = m_AssetLibraryTask.Error;
					m_AssetLibraryTask.Stage = AssetLibraryStage::Idle; // one-shot
				}
			}

			// Funke fetch failure: surface once from sticky state. An auto-fetch
			// can start and fail between two polls, so an in-flight edge would miss it.
			if (!m_AssetLibrary.FetchInFlight && !m_AssetLibrary.FetchError.empty()
				&& !m_AssetLibraryFetchErrorReported) {
				fetchError = m_AssetLibrary.FetchError;
				m_AssetLibraryFetchErrorReported = true;
			}
		}
		if (finishedThisFrame && !targetPath.empty()) {
			// Register the imported project HERE (render thread): the worker is done,
			// and LauncherRegistry must only be mutated on the thread that iterates it.
			if (!finalName.empty()) {
				m_Registry.AddProject(finalName, targetPath);
				m_Registry.Save();
			}
			// Refresh the registry view so the new project shows up under
			// My Projects without restarting. Don't auto-switch tabs. Completion
			// is surfaced via a modal (RenderAssetLibraryDoneModal), so this
			// one-shot also raises its open flag.
			RefreshProjectsList();
			m_SelectedProjectPath = targetPath;
			m_OpenAssetLibraryDonePopup = true;
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			m_AssetLibraryTask.TargetProjectPath.clear(); // one-shot
			m_AssetLibraryTask.FinalName.clear();
		}
		// Errors surface in the shared secondary error window, never inline in the panel.
		if (!downloadError.empty()) {
			ShowError(downloadError);
		}
		if (!fetchError.empty()) {
			ShowError(FNK_TR("launcher.asset_library.fetch_failed") + " (" + fetchError + ")");
		}
	}

	void LauncherLayer::RenderAssetLibraryTab() {
		bool fetchInFlight = false;
		bool indexLoaded = false;
		std::vector<AssetLibraryEntry> snapshot;
		{
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			fetchInFlight = m_AssetLibrary.FetchInFlight;
			indexLoaded = m_AssetLibrary.Loaded;
			snapshot = m_AssetLibrary.Entries;
		}

		const LauncherPalette pal = GetLauncherPalette();
		const float em = ImGui::GetFontSize();
		const float startX = ImGui::GetCursorPosX();
		const float availW = ImGui::GetContentRegionAvail().x;

		// Header: title + subtitle, Refresh right-aligned. Fetch failures surface in the error window, not inline.
		const float headerTop = ImGui::GetCursorPosY();
		const float headerH = PageHeader(FNK_TR("launcher.asset_library.title"),
			fetchInFlight ? FNK_TR("launcher.asset_library.progress.fetching_manifest")
				: FNK_TR("launcher.asset_library.subtitle"));
		const float headerBottom = ImGui::GetCursorPosY();

		const std::string refreshLabel = FNK_TR("launcher.asset_library.refresh");
		const float buttonH = std::round(ImGui::GetFrameHeight() + em * 0.35f);
		const float refreshW = IconButtonWidth(refreshLabel);
		ImGui::SetCursorPos(ImVec2(startX + availW - refreshW,
			headerTop + std::max(0.0f, (headerH - buttonH) * 0.5f)));
		if (fetchInFlight) ImGui::BeginDisabled();
		if (Icons::ButtonWithIcon(Icons::Type::Refresh, refreshLabel.c_str(), ImVec2(refreshW, buttonH), true)) {
			StartFetchAssetLibraryIndex();
		}
		if (fetchInFlight) ImGui::EndDisabled();

		ImGui::SetCursorPos(ImVec2(startX, headerBottom + em * 0.6f));

		ImGui::BeginChild("##AssetLibraryList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);

		if (snapshot.empty()) {
			if (indexLoaded && !fetchInFlight) {
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y * 0.3f);
				CenteredText(FNK_TR("launcher.asset_library.coming_soon").c_str(), pal.TextDim);
			}
			ImGui::EndChild();
			return;
		}

		const float gap = std::round(em * 0.8f);
		const float listW = ImGui::GetContentRegionAvail().x;
		const int columns = std::max(1, static_cast<int>((listW + gap) / (em * 15.0f + gap)));
		const float tileW = std::floor((listW - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns));
		const float previewH = std::round(tileW * 0.45f);
		const float tileH = previewH + std::round(em * 5.6f);
		const float pad = std::round(em * 0.8f);
		const float rounding = std::round(em * 0.6f);
		ImDrawList* dl = ImGui::GetWindowDrawList();

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
		for (size_t i = 0; i < snapshot.size(); ++i) {
			const AssetLibraryEntry& entry = snapshot[i];
			if (i % static_cast<size_t>(columns) != 0) {
				ImGui::SameLine(0.0f, gap);
			}

			ImGui::PushID(entry.Id.c_str());
			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton("##tile", ImVec2(tileW, tileH))) {
				m_AssetLibraryDetailEntryId = entry.Id;
				m_AssetLibraryDetailScreenshot = 0;
				m_OpenAssetLibraryDetailPopup = true;
			}
			const bool hovered = ImGui::IsItemHovered();
			const ImVec2 p1(p0.x + tileW, p0.y + tileH);

			dl->AddRectFilled(p0, p1, hovered ? pal.CardHover : pal.Card, rounding);

			// Keep the fallback visible until a thumbnail is uploaded.
			dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + previewH), ProjectThumbColor(entry.Name),
				rounding, ImDrawFlags_RoundCornersTop);
			const Texture2D* thumbnail = ImGui::IsItemVisible() && !fetchInFlight ? m_LibraryThumbnails.Request(entry.ThumbnailUrl, entry.Version) : nullptr;
			if (thumbnail) {
				dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + previewH), pal.Card, rounding, ImDrawFlags_RoundCornersTop);
				const float scale = std::min(tileW / thumbnail->GetWidth(), previewH / thumbnail->GetHeight());
				const ImVec2 size(thumbnail->GetWidth() * scale, thumbnail->GetHeight() * scale);
				const ImVec2 start(p0.x + (tileW - size.x) * 0.5f, p0.y + (previewH - size.y) * 0.5f);
				dl->AddImage(static_cast<ImTextureID>(thumbnail->GetHandle()), start, ImVec2(start.x + size.x, start.y + size.y));
			} else
			{
				const std::string initials = ProjectInitials(entry.Name);
				ImGui::PushFont(nullptr, BaseFontSize() * 2.0f);
				const ImVec2 isz = ImGui::CalcTextSize(initials.c_str());
				dl->AddText(ImVec2(p0.x + (tileW - isz.x) * 0.5f, p0.y + (previewH - isz.y) * 0.5f),
					IM_COL32(255, 255, 255, 230), initials.c_str());
				ImGui::PopFont();
			}
			dl->AddRect(p0, p1, hovered ? pal.Accent : pal.CardBorder, rounding, 0, hovered ? 1.5f : 1.0f);

			dl->PushClipRect(ImVec2(p0.x + pad, p0.y + previewH), ImVec2(p1.x - pad, p1.y - pad * 0.5f), true);
			float y = p0.y + previewH + pad * 0.8f;
			ImGui::PushFont(nullptr, BaseFontSize() * 1.1f);
			dl->AddText(ImVec2(p0.x + pad, y), ImGui::GetColorU32(ImGuiCol_Text), entry.Name.c_str());
			y += ImGui::GetTextLineHeight();
			ImGui::PopFont();

			std::string meta;
			if (!entry.Author.empty()) meta = Localization::Format("launcher.asset_library.author_prefix", entry.Author);
			if (!entry.Version.empty()) {
				if (!meta.empty()) meta += "  ·  ";
				meta += "v" + entry.Version;
			}
			if (!meta.empty()) {
				dl->AddText(ImVec2(p0.x + pad, y), pal.TextDim, meta.c_str());
				y += ImGui::GetTextLineHeight();
			}

			if (!entry.ShortDescription.empty()) {
				y += em * 0.35f;
				dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p0.x + pad, y),
					ImGui::GetColorU32(ImGuiCol_Text, 0.85f), entry.ShortDescription.c_str(), nullptr,
					tileW - pad * 2.0f);
			}
			dl->PopClipRect();

			ImGui::PopID();
		}
		ImGui::PopStyleVar();

		ImGui::EndChild();
	}

	void LauncherLayer::RenderAssetLibraryDetailModal() {
		constexpr const char* k_DetailId = "###AssetLibraryDetail";
		if (m_OpenAssetLibraryDetailPopup) {
			ImGui::OpenPopup(k_DetailId);
			m_OpenAssetLibraryDetailPopup = false;
		}

		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(640, 0), ImGuiCond_Appearing);

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string title = FNK_TR("launcher.asset_library.title") + std::string(k_DetailId);
		if (!ImGuiDialog::BeginModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			return;
		}

		AssetLibraryEntry entry;
		bool found = false;
		{
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			if (auto* e = FindAssetLibraryEntry(m_AssetLibraryDetailEntryId)) {
				entry = *e;
				found = true;
			}
		}
		if (!found) {
			ImGui::TextDisabled("%s", FNK_TR("launcher.asset_library.empty").c_str());
			if (ImGui::Button(FNK_TR("launcher.asset_library.close").c_str())) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
			return;
		}

		if (ImGuiDialog::BeginBody("##LibraryDetails")) {
			ImGui::TextUnformatted(entry.Name.c_str());
			if (!entry.Author.empty()) {
				ImGui::SameLine();
				ImGui::TextDisabled("by %s", entry.Author.c_str());
			}
			ImGui::Separator();

			// Metadata column.
			if (!entry.Version.empty()) ImGui::Text("v%s", entry.Version.c_str());
			if (!entry.License.empty()) ImGui::TextDisabled("License: %s", entry.License.c_str());
			if (!entry.EngineMinVersion.empty()) {
				const bool engineOK = Version::IsAtLeast(FNK_VERSION, entry.EngineMinVersion);
				if (!engineOK) {
					ImGui::TextColored(GetLauncherPalette().Error,
						"Requires Funke %s (you have %s)",
						entry.EngineMinVersion.c_str(), FNK_VERSION);
				}
				else {
					ImGui::TextDisabled("Engine: %s+", entry.EngineMinVersion.c_str());
				}
			}
			if (!entry.RequiredPackages.empty()) {
				std::string joined;
				for (size_t i = 0; i < entry.RequiredPackages.size(); ++i) {
					if (i) joined += ", ";
					joined += entry.RequiredPackages[i];
				}
				ImGui::TextDisabled("Required packages: %s", joined.c_str());
			}

			ImGui::Spacing();
			ImGui::Separator();

			if (!entry.Description.empty()) {
				ImGui::TextWrapped("%s", entry.Description.c_str());
			}

			ImGui::Spacing();
			ImGui::Separator();
		}
		ImGuiDialog::EndBody();

		// Bottom action row.
		const bool engineOK = entry.EngineMinVersion.empty()
			|| Version::IsAtLeast(FNK_VERSION, entry.EngineMinVersion);

		bool downloadInFlight = false;
		{
			std::scoped_lock lock(m_AssetLibraryTask.Mutex);
			downloadInFlight = m_AssetLibraryTask.Running;
		}

		const int action = DialogFooter({ FNK_TR("launcher.asset_library.download"), FNK_TR("launcher.asset_library.close") }, 0, engineOK && !downloadInFlight);
		if (action == 0) {
			StartAssetLibraryDownload(entry);
			ImGui::CloseCurrentPopup();
		}

		if (action == 1) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void LauncherLayer::RenderAssetLibraryDoneModal() {
		constexpr const char* k_DoneId = "###AssetLibraryDone";

		if (m_OpenAssetLibraryDonePopup) {
			ImGui::OpenPopup(k_DoneId);
			m_OpenAssetLibraryDonePopup = false;
		}

		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string title = FNK_TR("launcher.asset_library.done.title") + k_DoneId;
		if (!ImGuiDialog::BeginModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			return;
		}

		ImGuiDialog::Message("##Result", FNK_TR("launcher.asset_library.done.body").c_str());
		if (DialogFooter({ FNK_TR("launcher.asset_library.done.ok") }, 0) == 0) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void LauncherLayer::RenderAssetLibraryTrustModal() {
		constexpr const char* k_TrustId = "###AssetLibraryTrust";
		if (m_OpenAssetLibraryTrustPopup) {
			ImGui::OpenPopup(k_TrustId);
			m_OpenAssetLibraryTrustPopup = false;
		}

		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string title = FNK_TR("launcher.asset_library.trust.title") + std::string(k_TrustId);
		if (!ImGuiDialog::BeginModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			return;
		}

		ImGuiDialog::Message("##TrustDetails", FNK_TR("launcher.asset_library.trust.body").c_str());
		const int action = DialogFooter({ FNK_TR("launcher.asset_library.trust.acknowledge"),
			FNK_TR("launcher.asset_library.trust.cancel") }, 0);
		if (action == 0) {
			m_Settings.AssetLibraryTrustAck[ResolveAssetLibraryUrl()] = true;
			m_Settings.Save();

			if (m_PendingTrustedDownload.has_value()) {
				AssetLibraryEntry entry = std::move(*m_PendingTrustedDownload);
				m_PendingTrustedDownload.reset();
				ImGui::CloseCurrentPopup();
				StartAssetLibraryDownload(entry);
				ImGui::EndPopup();
				return;
			}
			ImGui::CloseCurrentPopup();
		}
		if (action == 1) {
			m_PendingTrustedDownload.reset();
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}
