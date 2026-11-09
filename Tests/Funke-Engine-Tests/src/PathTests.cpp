#include <doctest/doctest.h>

#include "Serialization/Path.hpp"

#include <filesystem>
#include <chrono>
#include <string>

using namespace Funke;

namespace {
	namespace fs = std::filesystem;

	// The separator Combine() will emit, as a single-char string.
	const std::string kSep(1, static_cast<char>(fs::path::preferred_separator));

	// Reference implementation mirroring Path::Combine for two/three fragments,
	// using the exact same std::filesystem operations the production code uses.
	std::string ExpectedCombine(const std::string& a, const std::string& b) {
		fs::path p;
		p /= Path::FromUtf8(a);
		p /= Path::FromUtf8(b);
		return Path::ToUtf8(p.make_preferred());
	}

	std::string ExpectedCombine(const std::string& a, const std::string& b, const std::string& c) {
		fs::path p;
		p /= Path::FromUtf8(a);
		p /= Path::FromUtf8(b);
		p /= Path::FromUtf8(c);
		return Path::ToUtf8(p.make_preferred());
	}
} // namespace

TEST_CASE("Path::Combine joins fragments with the platform-preferred separator") {
	const std::string result = Path::Combine("foo", "bar");
	CHECK(result == ExpectedCombine("foo", "bar"));
	// The single embedded separator must be the OS-preferred one.
	CHECK(result == "foo" + kSep + "bar");
}

TEST_CASE("Path::Combine chains an arbitrary number of fragments") {
	const std::string result = Path::Combine("a", "b", "c");
	CHECK(result == ExpectedCombine("a", "b", "c"));
	CHECK(result == "a" + kSep + "b" + kSep + "c");
}

TEST_CASE("Path::Combine with a single fragment returns that fragment unchanged") {
	CHECK(Path::Combine("solo") == "solo");
	CHECK(Path::Combine("name.ext") == "name.ext");
}

TEST_CASE("Path::Combine normalizes forward-slash separators to the preferred separator") {
	// A forward-slash input fragment is parsed as a relative path with two
	// components, so make_preferred() rewrites the separator on Windows and
	// leaves it untouched on Linux.
	const std::string result = Path::Combine("foo/bar");
	CHECK(result == ExpectedCombine("foo", "bar"));
	CHECK(result == "foo" + kSep + "bar");
}

TEST_CASE("Path::Combine treats an absolute-looking second fragment as filesystem operator/= does") {
	// operator/= replaces the path when the right-hand side is absolute. Mirror
	// whatever std::filesystem decides so the test is identical on both OSes.
	const std::string lhsThenRooted = Path::Combine("foo", "/bar");
	fs::path expected;
	expected /= fs::path("foo");
	expected /= fs::path("/bar");
	CHECK(lhsThenRooted == expected.make_preferred().string());
}

TEST_CASE("Path::Combine preserves file extensions in the final fragment") {
	const std::string result = Path::Combine("dir", "asset.dataasset");
	CHECK(result == ExpectedCombine("dir", "asset.dataasset"));
	CHECK(result == "dir" + kSep + "asset.dataasset");
	// The trailing extension fragment survives intact.
	CHECK(fs::path(result).extension().string() == ".dataasset");
	CHECK(fs::path(result).filename().string() == "asset.dataasset");
	CHECK(fs::path(result).stem().string() == "asset");
}

TEST_CASE("Path::Combine is deterministic and repeatable for equal inputs") {
	CHECK(Path::Combine("x", "y", "z") == Path::Combine("x", "y", "z"));
	// Order matters: swapping fragments yields a different join.
	CHECK(Path::Combine("x", "y") != Path::Combine("y", "x"));
}

TEST_CASE("Path::Combine accepts std::string fragments as well as string literals") {
	const std::string a = "Textures";
	const std::string b = "icon.png";
	const std::string result = Path::Combine(a, b);
	CHECK(result == ExpectedCombine("Textures", "icon.png"));
	CHECK(result == "Textures" + kSep + "icon.png");
}

TEST_CASE("Path::Combine with an empty leading fragment yields the second fragment") {
	// p /= path("") is a no-op; appending "leaf" then gives just "leaf".
	CHECK(Path::Combine(std::string(""), std::string("leaf")) == ExpectedCombine("", "leaf"));
	CHECK(Path::Combine(std::string(""), std::string("leaf")) == "leaf");
}

TEST_CASE("Path filesystem conversion preserves Unicode UTF-8") {
	const std::string unicode = "Pr\xC3\xBC" "fung";
	CHECK(Path::ToUtf8(Path::FromUtf8(unicode)) == unicode);
	CHECK(Path::Combine(unicode, "Assets") == unicode + kSep + "Assets");
}

TEST_CASE("Path resolves relocated bundle resources from the content base independently of cwd") {
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkeAssetPaths_" + std::to_string(suffix));
	struct Restore {
		fs::path Root;
		fs::path Cwd;
		std::string ContentBase;
		~Restore() {
			Path::SetExecutableDirOverride(ContentBase);
			std::error_code error;
			fs::current_path(Cwd, error);
			fs::remove_all(Root, error);
		}
	} restore{ root, fs::current_path(), Path::ExecutableDir() };
	const fs::path foreign = root / "Foreign working directory";
	fs::create_directories(foreign / "FunkeAssets/Textures");
	fs::current_path(foreign);
	const fs::path staged = root / "Staged/Funke.app";
	fs::create_directories(staged / "Contents/MacOS");
	fs::create_directories(staged / "Contents/Resources/FunkeAssets/Textures");
	const fs::path moved = root / Path::FromUtf8("Relocated Applications Pr\xC3\xBC" "fung") / "Funke.app";
	fs::create_directories(moved.parent_path());
	fs::rename(staged, moved);
	const fs::path resources = moved / "Contents/Resources";
	Path::SetExecutableDirOverride(Path::ToUtf8(resources));
	CHECK(fs::equivalent(Path::FromUtf8(Path::ResolveFunkeAssets("")), resources / "FunkeAssets"));
	CHECK(fs::equivalent(Path::FromUtf8(Path::ResolveFunkeAssets("Textures")), resources / "FunkeAssets/Textures"));
	CHECK(Path::ResolveFunkeAssets("Missing").empty());
	fs::remove_all(resources / "FunkeAssets");
	CHECK(Path::ResolveFunkeAssets("").empty());
	CHECK(Path::ResolveFunkeAssets("Textures").empty());

	const fs::path executable = root / "Release/Funke-Editor";
	fs::create_directories(executable);
	fs::create_directories(executable.parent_path() / "FunkeAssets/Textures");
	Path::SetExecutableDirOverride(Path::ToUtf8(executable));
	CHECK(fs::equivalent(Path::FromUtf8(Path::ResolveFunkeAssets("Textures")), executable.parent_path() / "FunkeAssets/Textures"));
	fs::create_directories(executable / "FunkeAssets/Textures");
	CHECK(fs::equivalent(Path::FromUtf8(Path::ResolveFunkeAssets("Textures")), executable / "FunkeAssets/Textures"));
}
