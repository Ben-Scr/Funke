#pragma once
#define _CRT_SECURE_NO_WARNINGS

#ifdef FNK_PLATFORM_WINDOWS
#define NOMINMAX
#include <Windows.h>
#endif

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Core/Version.hpp>
#include <Core/Assert.hpp>
#include <Core/Base.hpp>
#include <Core/Log.hpp>
#include <Core/Exceptions.hpp>

// Deliberately stops here, at the STL + Core/ boundary.
//
// Adding Core/Application.hpp + Scene/SceneManager.hpp was measured: it cuts
// the transitive parse volume of the 127 engine TUs roughly in half
// (~2,554k -> ~1,278k lines) but moved a full engine rebuild only 52s -> 50s.
// After /Z7 and the CL_MPCount cap (see Directory.Build.props), this build is
// dominated by template instantiation, codegen and linking rather than header
// parsing, so parse-volume reductions no longer translate into wall-clock.
//
// It is not worth the incremental cost: Application.hpp has 11 engine
// includers, so precompiling it turns an 11-TU edit into a 127-TU rebuild plus
// a PCH regeneration. Header edits are constant; full rebuilds are rare.
//
// Also deliberately absent: <imgui.h> (the engine DLL must not pull ImGui into
// every TU — Funke-Editor layers its own PCH on top of this one for that, see
// Funke-Editor/src/EditorPch.hpp) and <magic_enum/magic_enum.hpp> (removed
// from Core/Base.hpp; only four files reflect enums, and they include it
// themselves).