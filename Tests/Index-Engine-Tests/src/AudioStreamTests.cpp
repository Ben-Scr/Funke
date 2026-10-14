#include <doctest/doctest.h>

#include "Audio/AudioStreams.hpp"
#include "Core/Platform.hpp"

using namespace Index;

TEST_CASE("Audio streams fail closed without an audio device")
{
	CHECK(AudioStreams::Open("missing.mp3") == 0);
	CHECK(AudioStreams::Open("") == 0);

	// Every call on an unknown id is a harmless no-op, so scripts holding a stale handle can't crash the player.
	constexpr uint32_t unknown = 12345;
	CHECK_FALSE(AudioStreams::IsValid(unknown));
	CHECK_FALSE(AudioStreams::Play(unknown));
	CHECK_FALSE(AudioStreams::IsPlaying(unknown));
	CHECK_FALSE(AudioStreams::IsAtEnd(unknown));
	CHECK(AudioStreams::GetLengthSeconds(unknown) == 0.0f);
	CHECK(AudioStreams::GetCursorSeconds(unknown) == 0.0f);
	AudioStreams::Seek(unknown, 10.0f);
	AudioStreams::SetVolume(unknown, 0.5f);
	AudioStreams::Pause(unknown);
	AudioStreams::Stop(unknown);
	AudioStreams::Close(unknown);
	AudioStreams::CloseAll();
}

#if !defined(IDX_PLATFORM_ANDROID)
TEST_CASE("Runtime permissions are granted up front outside Android")
{
	const std::string permission = "android.permission.READ_MEDIA_AUDIO";
	CHECK(Platform::GetPermissionState(permission) == Platform::PermissionState::Granted);
	Platform::RequestPermission(permission);
	CHECK(Platform::GetPermissionState(permission) == Platform::PermissionState::Granted);
}
#endif
