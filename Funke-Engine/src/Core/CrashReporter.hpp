#pragma once

#include "Core/Export.hpp"

#include <string>

namespace Funke {

	class FUNKE_API CrashReporter {
	public:
		static void Initialize();
		static void Shutdown();

		static const std::string& GetDumpDirectory();
	};

} // namespace Funke
