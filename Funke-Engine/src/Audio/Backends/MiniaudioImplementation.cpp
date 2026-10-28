#include "pch.hpp"

// Vorbis declarations must precede miniaudio to enable its Ogg decoder.
#define STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
// Match OleInitialize's apartment so audio initialization does not break drag and drop.
#define MA_COINIT_VALUE 0x2
#include <miniaudio.h>

// stb_vorbis defines short macros that must not leak into subsequent headers.
#undef STB_VORBIS_HEADER_ONLY
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <extras/stb_vorbis.c>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
