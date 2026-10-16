// Platform layer: the portable SHA-256, SDL -> Funke input translation and the
// shared-library / platform naming helpers.

#include <doctest/doctest.h>

#include "Core/Input.hpp"
#include "Core/KeyCode.hpp"
#include "Core/MouseButton.hpp"
#include "Core/Platform.hpp"
#include "Core/Platform/SDLInputMapping.hpp"
#include "Core/Platform/TitlebarRegions.hpp"
#include "Utils/Hash.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace Funke;

TEST_CASE("SHA-256 matches the FIPS 180-4 test vectors") {
	CHECK(Hash::Sha256OfBytes("", 0) ==
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(Hash::Sha256OfBytes("abc", 3) ==
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	const std::string twoBlocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	CHECK(Hash::Sha256OfBytes(twoBlocks.data(), twoBlocks.size()) ==
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	const std::string million(1000000, 'a');
	CHECK(Hash::Sha256OfBytes(million.data(), million.size()) ==
		"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("SHA-256 of a file matches the in-memory digest") {
	const std::filesystem::path file = std::filesystem::temp_directory_path() / "funke-sha256-test.bin";
	std::string payload;
	for (int i = 0; i < 200000; ++i) payload.push_back(static_cast<char>(i * 31));
	{
		std::ofstream out(file, std::ios::binary);
		out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
	}
	std::string hex, error;
	REQUIRE(Hash::Sha256OfFile(file, hex, error));
	CHECK(hex == Hash::Sha256OfBytes(payload.data(), payload.size()));
	std::filesystem::remove(file);

	CHECK_FALSE(Hash::Sha256OfFile(file, hex, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("SDL scancodes map onto the stable KeyCode values") {
	using SDLInputMapping::KeyCodeFromScancode;
	CHECK(KeyCodeFromScancode(SDL_SCANCODE_A) == static_cast<int>(KeyCode::A));
	CHECK(KeyCodeFromScancode(SDL_SCANCODE_Z) == static_cast<int>(KeyCode::Z));
	CHECK(KeyCodeFromScancode(SDL_SCANCODE_0) == static_cast<int>(KeyCode::Alpha0));
	CHECK(KeyCodeFromScancode(SDL_SCANCODE_9) == static_cast<int>(KeyCode::Alpha9));
	CHECK(KeyCodeFromScancode(SDL_SCANCODE_ESCAPE) == static_cast<int>(KeyCode::Esc));
	CHECK(KeyCodeFromScancode(SDL_SCANCODE_DOWN) == static_cast<int>(KeyCode::Bottom));
	CHECK(KeyCodeFromScancode(SDL_SCANCODE_F24) == static_cast<int>(KeyCode::F24));
	CHECK(KeyCodeFromScancode(SDL_SCANCODE_KP_ENTER) == static_cast<int>(KeyCode::KpEnter));
	CHECK(KeyCodeFromScancode(SDL_SCANCODE_RGUI) == static_cast<int>(KeyCode::RightSuper));
	CHECK(KeyCodeFromScancode(SDL_SCANCODE_APPLICATION) == static_cast<int>(KeyCode::Menu));
	CHECK(KeyCodeFromScancode(SDL_SCANCODE_AC_BACK) == static_cast<int>(KeyCode::Esc));
	CHECK(KeyCodeFromScancode(SDL_SCANCODE_UNKNOWN) == -1);
	CHECK(KeyCodeFromScancode(-5) == -1);
	CHECK(KeyCodeFromScancode(SDL_SCANCODE_COUNT) == -1);

	// Reverse lookup prefers the primary key (Escape), not the Android back alias.
	CHECK(SDLInputMapping::ScancodeFromKeyCode(static_cast<int>(KeyCode::Esc)) == SDL_SCANCODE_ESCAPE);
	CHECK(SDLInputMapping::ScancodeFromKeyCode(static_cast<int>(KeyCode::Space)) == SDL_SCANCODE_SPACE);
}

TEST_CASE("SDL mouse buttons map onto MouseButton") {
	using SDLInputMapping::MouseButtonFromSDL;
	CHECK(MouseButtonFromSDL(SDL_BUTTON_LEFT) == static_cast<int>(MouseButton::Left));
	CHECK(MouseButtonFromSDL(SDL_BUTTON_RIGHT) == static_cast<int>(MouseButton::Right));
	CHECK(MouseButtonFromSDL(SDL_BUTTON_MIDDLE) == static_cast<int>(MouseButton::Middle));
	CHECK(MouseButtonFromSDL(SDL_BUTTON_X1) == static_cast<int>(MouseButton::Back));
	CHECK(MouseButtonFromSDL(SDL_BUTTON_X2) == static_cast<int>(MouseButton::Revert));
	CHECK(MouseButtonFromSDL(0) == -1);
	CHECK(MouseButtonFromSDL(9) == -1);
}

TEST_CASE("Gamepad buttons keep the Xbox-positional layout") {
	using SDLInputMapping::SDLGamepadButtonFromIndex;
	CHECK(SDLGamepadButtonFromIndex(static_cast<int>(GamepadButton::A)) == SDL_GAMEPAD_BUTTON_SOUTH);
	CHECK(SDLGamepadButtonFromIndex(static_cast<int>(GamepadButton::Y)) == SDL_GAMEPAD_BUTTON_NORTH);
	CHECK(SDLGamepadButtonFromIndex(static_cast<int>(GamepadButton::LeftBumper)) == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
	CHECK(SDLGamepadButtonFromIndex(static_cast<int>(GamepadButton::Guide)) == SDL_GAMEPAD_BUTTON_GUIDE);
	CHECK(SDLGamepadButtonFromIndex(static_cast<int>(GamepadButton::DPadRight)) == SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
	CHECK(SDLGamepadButtonFromIndex(static_cast<int>(GamepadButton::DPadLeft)) == SDL_GAMEPAD_BUTTON_DPAD_LEFT);
	CHECK(SDLGamepadButtonFromIndex(15) == -1);
	// GamepadAxis is read by index straight into SDL_GetGamepadAxis.
	CHECK(static_cast<int>(GamepadAxis::LeftTrigger) == SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
	CHECK(static_cast<int>(GamepadAxis::RightY) == SDL_GAMEPAD_AXIS_RIGHTY);
}

TEST_CASE("Custom titlebar hit test") {
	using TitlebarRegions::Hit;
	TitlebarRegions::SetCaptionRect(0, 0, 800, 32);
	TitlebarRegions::ResetNonClientRects();
	TitlebarRegions::AddNonClientRect(700, 0, 100, 32);

	CHECK(TitlebarRegions::HitTest(400, 16, 800, 600, 8, true, false) == Hit::Caption);
	CHECK(TitlebarRegions::HitTest(750, 16, 800, 600, 8, true, false) == Hit::Client);
	CHECK(TitlebarRegions::HitTest(400, 300, 800, 600, 8, true, false) == Hit::Client);
	CHECK(TitlebarRegions::HitTest(2, 2, 800, 600, 8, true, false) == Hit::ResizeTopLeft);
	CHECK(TitlebarRegions::HitTest(799, 300, 800, 600, 8, true, false) == Hit::ResizeRight);
	CHECK(TitlebarRegions::HitTest(400, 599, 800, 600, 8, true, false) == Hit::ResizeBottom);
	// Maximized and fixed-size windows have no resize border; the caption reaches the edge.
	CHECK(TitlebarRegions::HitTest(2, 2, 800, 600, 8, true, true) == Hit::Caption);
	CHECK(TitlebarRegions::HitTest(2, 2, 800, 600, 8, false, false) == Hit::Caption);

	TitlebarRegions::SetCaptionRect(0, 0, 0, 0);
	TitlebarRegions::ResetNonClientRects();
}

TEST_CASE("Shared library and platform naming") {
#if defined(FNK_PLATFORM_WINDOWS)
	CHECK(Platform::SharedLibraryFileName("Pkg.Foo.Native") == "Pkg.Foo.Native.dll");
#elif defined(FNK_PLATFORM_MACOS)
	CHECK(Platform::SharedLibraryFileName("Pkg.Foo.Native") == "libPkg.Foo.Native.dylib");
#else
	CHECK(Platform::SharedLibraryFileName("Pkg.Foo.Native") == "libPkg.Foo.Native.so");
#endif
	CHECK_FALSE(std::string(FNK_PLATFORM_NAME).empty());
	CHECK(Platform::GetLogicalCpuCount() >= 1);
	CHECK(std::filesystem::exists(Platform::GetExecutablePath()));
	CHECK(std::filesystem::is_directory(Platform::GetExecutableDirectory()));
	// SDL_GetBasePath ends with a separator, so its parent_path() is the directory itself.
	CHECK(std::filesystem::equivalent(Platform::GetExecutablePath().parent_path(),
		Platform::GetExecutableDirectory().parent_path()));
	CHECK_FALSE(Platform::GetUserDataRoot().empty());
}

TEST_CASE("Loading a missing shared library reports an error") {
	std::string error;
	CHECK(Platform::LoadSharedLibrary("this-library-does-not-exist-12345", &error) == nullptr);
	CHECK_FALSE(error.empty());
	CHECK(Platform::GetSharedLibrarySymbol(nullptr, "x") == nullptr);
	Platform::UnloadSharedLibrary(nullptr);
}

TEST_CASE("Java bridge queues plugin messages in order and fails calls off Android") {
	std::string channel, message;
	while (Platform::PollJavaMessage(channel, message)) {}

	Platform::PushJavaMessage("ads", "{\"event\":\"loaded\"}");
	Platform::PushJavaMessage("billing", std::string("bytes\0kept", 10));
	REQUIRE(Platform::PollJavaMessage(channel, message));
	CHECK(channel == "ads");
	CHECK(message == "{\"event\":\"loaded\"}");
	REQUIRE(Platform::PollJavaMessage(channel, message));
	CHECK(channel == "billing");
	CHECK(message.size() == 10);
	CHECK_FALSE(Platform::PollJavaMessage(channel, message));

	// The queue is bounded; the oldest messages give way.
	for (int i = 0; i < 1100; ++i) Platform::PushJavaMessage("flood", std::to_string(i));
	int count = 0;
	std::string first;
	while (Platform::PollJavaMessage(channel, message)) {
		if (count++ == 0) first = message;
	}
	CHECK(count == 1024);
	CHECK(first == "76");

#if !defined(FNK_PLATFORM_ANDROID)
	std::string result = "stale";
	CHECK_FALSE(Platform::CallJava("dev.funke.ads.FunkeAds", "initialize", "{}", result));
	CHECK(result.empty());
#endif
}
