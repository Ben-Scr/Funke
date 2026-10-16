#pragma once
#include "Core/Export.hpp"

#include <cstdint>
#include <string_view>

namespace Funke {

	// Sounds streamed from any file on disk (music players, user files) rather than decoded asset audio.
	// Main-thread only. 0 is never a valid stream; ids are not reused within a process.
	class FUNKE_API AudioStreams {
	public:
		// UTF-8 path to an MP3, WAV or FLAC file. Returns 0 when audio is off or the file can't be opened.
		static uint32_t Open(std::string_view path);
		static void Close(uint32_t stream);
		static void CloseAll();
		static bool IsValid(uint32_t stream);

		static bool Play(uint32_t stream);
		// Keeps the cursor, so Play continues where it paused.
		static void Pause(uint32_t stream);
		static void Stop(uint32_t stream);
		static bool IsPlaying(uint32_t stream);
		// True once a non-looping stream has played to its last frame.
		static bool IsAtEnd(uint32_t stream);

		static float GetCursorSeconds(uint32_t stream);
		static float GetLengthSeconds(uint32_t stream);
		static void Seek(uint32_t stream, float seconds);

		static void SetVolume(uint32_t stream, float volume);
		static float GetVolume(uint32_t stream);
		static void SetLooping(uint32_t stream, bool looping);
		static bool IsLooping(uint32_t stream);
	};

}
