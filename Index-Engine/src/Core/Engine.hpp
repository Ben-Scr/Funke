#pragma once

#include "Core/EngineTypes.hpp"
#include "Core/Export.hpp"

#include <cstdint>
#include <string_view>

namespace Index {

	class INDEX_API Engine final {
	public:
		Engine() = delete;

		static std::string_view GetVersion() noexcept;
		static std::string_view GetVersionLong() noexcept;

		static GraphicsApi GetGraphicsApi() noexcept;
		static std::string_view GetGraphicsApiName() noexcept;

		static GpuVendor GetGpuVendor() noexcept;
		// Raw adapter string views are empty before renderer init and expire on shutdown or reinitialization.
		static std::string_view GetGpuVendorName() noexcept;
		static std::string_view GetGpuName() noexcept;

		static CpuVendor GetCpuVendor() noexcept;
		static std::string_view GetCpuVendorName() noexcept;
		static std::string_view GetCpuName() noexcept;
		static uint32_t GetProcessorCount() noexcept;
	};

} // namespace Index
