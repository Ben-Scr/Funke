#include <doctest/doctest.h>

#include "Serialization/File.hpp"
#include "Serialization/FileWatcher.hpp"
#include "Serialization/Path.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

using namespace Funke;
using namespace std::chrono_literals;

namespace {
	struct WatchedDirectory {
		std::filesystem::path Root = std::filesystem::temp_directory_path()
			/ ("FunkeFileWatcher_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		FileWatcher Watcher;
		std::vector<std::vector<std::string>> Batches;
		std::thread::id CallbackThread;

		WatchedDirectory() { std::filesystem::create_directories(Root); }
		~WatchedDirectory() {
			Watcher.Stop();
			std::error_code ec;
			std::filesystem::remove_all(Root, ec);
		}

		std::string PathTo(const char* name) const { return Path::ToUtf8(Path::CanonicalIdentity(Root / name)); }
		bool Write(const char* name, const std::string& contents) const {
			// Windows metadata probes can briefly prevent atomic file replacement.
			const auto deadline = std::chrono::steady_clock::now() + 1s;
			do {
				if (File::WriteAllText(PathTo(name), contents)) return true;
				std::this_thread::sleep_for(5ms);
			} while (std::chrono::steady_clock::now() < deadline);
			return false;
		}
		bool Saw(const std::string& path) const {
			for (const auto& batch : Batches) if (std::find(batch.begin(), batch.end(), path) != batch.end()) return true;
			return false;
		}

		template<class Predicate> bool Wait(Predicate ready) {
			const auto deadline = std::chrono::steady_clock::now() + 5s;
			while (std::chrono::steady_clock::now() < deadline) {
				Watcher.Poll(0.05f);
				if (ready()) return true;
				std::this_thread::sleep_for(5ms);
			}
			return false;
		}

		void Drain() {
			const auto deadline = std::chrono::steady_clock::now() + 150ms;
			while (std::chrono::steady_clock::now() < deadline) {
				Watcher.Poll(0.05f);
				std::this_thread::sleep_for(5ms);
			}
		}

		bool Start() {
			Watcher.Watch(std::vector<std::string>{ Path::ToUtf8(Root) }, std::vector<std::string>{ ".png", ".meta" },
				[this](const std::vector<std::string>& paths) {
					CallbackThread = std::this_thread::get_id();
					Batches.push_back(paths);
				});
			int attempt = 0;
			// Establish worker readiness without assuming a filesystem or thread-start latency.
			const bool ready = Wait([&] {
				if (Saw(PathTo("ready.meta"))) return true;
				Write("ready.meta", std::string(++attempt, 'x'));
				return false;
			});
			Drain();
			Batches.clear();
			return ready;
		}
	};
}

TEST_CASE("FileWatcher reports only changed paths including metadata renames and deletions") {
	WatchedDirectory fixture;
	REQUIRE(fixture.Write("untouched.png", "unchanged"));
	REQUIRE(fixture.Write("changed.png", "before"));
	REQUIRE(fixture.Start());

	SUBCASE("Modification retains the timestamp but changes the size") {
		const auto path = fixture.Root / "changed.png";
		const auto stamp = std::filesystem::last_write_time(path);
		REQUIRE(fixture.Write("changed.png", "different size after editing"));
		std::filesystem::last_write_time(path, stamp);
		REQUIRE(fixture.Wait([&] { return fixture.Saw(fixture.PathTo("changed.png")); }));
	}
	SUBCASE("Added image and metadata are both delivered") {
		REQUIRE(fixture.Write("new.png", "new pixels"));
		REQUIRE(fixture.Write("new.png.meta", "new metadata"));
		REQUIRE(fixture.Wait([&] { return fixture.Saw(fixture.PathTo("new.png")) && fixture.Saw(fixture.PathTo("new.png.meta")); }));
	}
	SUBCASE("A rename reports both paths even when the file count stays constant") {
		std::filesystem::rename(fixture.Root / "changed.png", fixture.Root / "renamed.png");
		REQUIRE(fixture.Wait([&] { return fixture.Saw(fixture.PathTo("changed.png")) && fixture.Saw(fixture.PathTo("renamed.png")); }));
	}
	SUBCASE("Deleted paths are retained in the notification") {
		REQUIRE(std::filesystem::remove(fixture.Root / "changed.png"));
		REQUIRE(fixture.Wait([&] { return fixture.Saw(fixture.PathTo("changed.png")); }));
	}
	fixture.Drain();
	CHECK_FALSE(fixture.Saw(fixture.PathTo("untouched.png")));
	CHECK(fixture.CallbackThread == std::this_thread::get_id());
	for (const auto& batch : fixture.Batches) {
		CHECK(std::unordered_set<std::string>(batch.begin(), batch.end()).size() == batch.size());
		for (const auto& path : batch) CHECK(Path::FromUtf8(path).is_absolute());
	}
}

TEST_CASE("FileWatcher retains separate worker changes until Poll and supports legacy callbacks") {
	WatchedDirectory fixture;
	REQUIRE(fixture.Start());
	REQUIRE(fixture.Write("first.png", "first"));
	std::this_thread::sleep_for(100ms);
	REQUIRE(fixture.Write("second.png", "second"));
	REQUIRE(fixture.Write("ignored.txt", "ignored"));
	std::this_thread::sleep_for(100ms);
	CHECK(fixture.Batches.empty());
	REQUIRE(fixture.Wait([&] { return fixture.Saw(fixture.PathTo("first.png")) && fixture.Saw(fixture.PathTo("second.png")); }));
	fixture.Drain();
	CHECK_FALSE(fixture.Saw(fixture.PathTo("ignored.txt")));
	fixture.Watcher.Stop();
	CHECK_FALSE(fixture.Watcher.IsWatching());
	fixture.Batches.clear();
	fixture.Watcher.Poll();
	CHECK(fixture.Batches.empty());

	int callbacks = 0;
	fixture.Watcher.Watch(Path::ToUtf8(fixture.Root), ".png", [&] { ++callbacks; });
	int attempt = 0;
	REQUIRE(fixture.Wait([&] {
		if (callbacks > 0) return true;
		fixture.Write("legacy.png", std::string(++attempt, 'x'));
		return false;
	}));
	fixture.Watcher.Stop();
}
