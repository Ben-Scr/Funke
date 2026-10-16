#include "pch.hpp"
#include "Base.hpp"

#include "Core/CrashReporter.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include "Core/Platform.hpp"

namespace Funke {

	void InitializeCore()
	{
		Platform::ExtendToolSearchPath();
		Allocator::Init();
		Log::Initialize();
		// CrashReporter installed after Log so the handler can flush
		// spdlog before the dump fires.
		CrashReporter::Initialize();

		FNK_CORE_TRACE_TAG("Core", "Funke Engine {}", FNK_VERSION);
		FNK_CORE_TRACE_TAG("Core", "Initializing...");
	}

	void ShutdownCore()
	{
		FNK_CORE_TRACE_TAG("Core", "Shutting down...");
		CrashReporter::Shutdown();
		Log::Shutdown();
		Allocator::Shutdown();
	}
}
