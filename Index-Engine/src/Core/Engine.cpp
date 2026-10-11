#include "pch.hpp"
#include "Core/Engine.hpp"
#include "Core/BuildIdentity.hpp"

#if INDEX_WITH_RENDER
#include "Graphics/RenderApi.hpp"
#endif

#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>

#if (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)) \
	&& (defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__))
#define IDX_ENGINE_HAS_CPUID 1
#else
#define IDX_ENGINE_HAS_CPUID 0
#endif

#if IDX_ENGINE_HAS_CPUID
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

#if defined(IDX_PLATFORM_WINDOWS)
#include <processthreadsapi.h>
#elif defined(IDX_PLATFORM_LINUX)
#include <unistd.h>
#endif

namespace Index {

	namespace {
		struct CpuDetails {
			CpuVendor Vendor = CpuVendor::Unknown;
			std::array<char, 64> VendorName{};
			size_t VendorNameLength = 0;
			std::array<char, 128> Name{};
			size_t NameLength = 0;
			uint32_t ProcessorCount = 1;
		};

		constexpr bool IsAsciiPadding(char value) noexcept {
			return value == '\0'
				|| value == ' '
				|| value == '\t'
				|| value == '\r'
				|| value == '\n';
		}

		std::string_view TrimAscii(std::string_view value) noexcept {
			while (!value.empty() && IsAsciiPadding(value.front())) value.remove_prefix(1);
			while (!value.empty() && IsAsciiPadding(value.back())) value.remove_suffix(1);
			return value;
		}

		template<size_t Capacity>
		size_t StoreText(std::array<char, Capacity>& destination, std::string_view value) noexcept {
			static_assert(Capacity > 0);
			value = TrimAscii(value);
			const size_t length = value.size() < Capacity - 1 ? value.size() : Capacity - 1;
			if (length > 0) std::memcpy(destination.data(), value.data(), length);
			destination[length] = '\0';
			return length;
		}

		constexpr char ToAsciiLower(char value) noexcept {
			return value >= 'A' && value <= 'Z'
				? static_cast<char>(value + ('a' - 'A'))
				: value;
		}

		bool EqualsAsciiCaseInsensitive(std::string_view left, std::string_view right) noexcept {
			if (left.size() != right.size()) return false;
			for (size_t index = 0; index < left.size(); ++index) {
				if (ToAsciiLower(left[index]) != ToAsciiLower(right[index])) return false;
			}
			return true;
		}

		bool ContainsAsciiCaseInsensitive(std::string_view value, std::string_view token) noexcept {
			if (token.empty()) return true;
			if (token.size() > value.size()) return false;

			for (size_t start = 0; start <= value.size() - token.size(); ++start) {
				bool matches = true;
				for (size_t offset = 0; offset < token.size(); ++offset) {
					if (ToAsciiLower(value[start + offset]) != ToAsciiLower(token[offset])) {
						matches = false;
						break;
					}
				}
				if (matches) return true;
			}

			return false;
		}

		CpuVendor ParseCpuVendor(std::string_view value) noexcept {
			if (EqualsAsciiCaseInsensitive(value, "GenuineIntel") || ContainsAsciiCaseInsensitive(value, "Intel")) return CpuVendor::Intel;
			if (EqualsAsciiCaseInsensitive(value, "AuthenticAMD") || ContainsAsciiCaseInsensitive(value, "AMD")) return CpuVendor::AMD;
			if (EqualsAsciiCaseInsensitive(value, "0x61") || ContainsAsciiCaseInsensitive(value, "Apple")) return CpuVendor::Apple;
			if (EqualsAsciiCaseInsensitive(value, "0x51") || ContainsAsciiCaseInsensitive(value, "Qualcomm")) return CpuVendor::Qualcomm;
			if (EqualsAsciiCaseInsensitive(value, "0x41")
				|| ContainsAsciiCaseInsensitive(value, "ARM")
				|| ContainsAsciiCaseInsensitive(value, "AArch")) return CpuVendor::ARM;
			return CpuVendor::Unknown;
		}

#if IDX_ENGINE_HAS_CPUID
		struct CpuIdRegisters {
			uint32_t Eax = 0;
			uint32_t Ebx = 0;
			uint32_t Ecx = 0;
			uint32_t Edx = 0;
		};

		bool HasCpuId() noexcept {
#if defined(_MSC_VER)
			return true;
#else
			return __get_cpuid_max(0u, nullptr) != 0u;
#endif
		}

		CpuIdRegisters ReadCpuId(uint32_t leaf, uint32_t subleaf = 0) noexcept {
#if defined(_MSC_VER)
			static_assert(sizeof(int) == sizeof(uint32_t));
			int values[4]{};
			__cpuidex(values, std::bit_cast<int>(leaf), std::bit_cast<int>(subleaf));
			return {
				std::bit_cast<uint32_t>(values[0]),
				std::bit_cast<uint32_t>(values[1]),
				std::bit_cast<uint32_t>(values[2]),
				std::bit_cast<uint32_t>(values[3])
			};
#else
			unsigned int eax = 0;
			unsigned int ebx = 0;
			unsigned int ecx = 0;
			unsigned int edx = 0;
			__cpuid_count(leaf, subleaf, eax, ebx, ecx, edx);
			return {
				static_cast<uint32_t>(eax),
				static_cast<uint32_t>(ebx),
				static_cast<uint32_t>(ecx),
				static_cast<uint32_t>(edx)
			};
#endif
		}

		void DetectCpuId(CpuDetails& details) noexcept {
			if (!HasCpuId()) return;

			const CpuIdRegisters basicInfo = ReadCpuId(0u);
			const std::array<uint32_t, 3> vendorWords{
				basicInfo.Ebx,
				basicInfo.Edx,
				basicInfo.Ecx
			};
			std::array<char, 13> vendor{};
			std::memcpy(vendor.data(), vendorWords.data(), vendorWords.size() * sizeof(vendorWords[0]));
			details.VendorNameLength = StoreText(
				details.VendorName,
				std::string_view(vendor.data(), vendor.size() - 1));

			const CpuIdRegisters extendedInfo = ReadCpuId(0x80000000u);
			if (extendedInfo.Eax < 0x80000004u) return;

			std::array<char, 49> brand{};
			for (uint32_t index = 0; index < 3; ++index) {
				const CpuIdRegisters registers = ReadCpuId(0x80000002u + index);
				const std::array<uint32_t, 4> words{
					registers.Eax,
					registers.Ebx,
					registers.Ecx,
					registers.Edx
				};
				std::memcpy(brand.data() + (static_cast<size_t>(index) * 16), words.data(), 16);
			}

			details.NameLength = StoreText(
				details.Name,
				std::string_view(brand.data(), brand.size() - 1));
		}
#endif

#if defined(IDX_PLATFORM_LINUX)
		void DetectLinuxCpuInfo(CpuDetails& details) noexcept {
			std::FILE* file = std::fopen("/proc/cpuinfo", "r");
			if (!file) return;

			std::array<char, 512> line{};
			while (std::fgets(line.data(), static_cast<int>(line.size()), file) != nullptr) {
				const std::string_view fullLine(line.data());
				const size_t separator = fullLine.find(':');
				if (separator == std::string_view::npos) continue;

				const std::string_view key = TrimAscii(fullLine.substr(0, separator));
				const std::string_view value = TrimAscii(fullLine.substr(separator + 1));
				if (details.VendorNameLength == 0 && (key == "vendor_id" || key == "CPU implementer")) {
					details.VendorNameLength = StoreText(details.VendorName, value);
				}
				if (details.NameLength == 0 && (key == "model name" || key == "Processor")) {
					details.NameLength = StoreText(details.Name, value);
				}
				if (details.VendorNameLength != 0 && details.NameLength != 0) break;
			}

			std::fclose(file);
		}
#endif

		uint32_t ClampProcessorCount(uint64_t count) noexcept {
			constexpr uint32_t maximum = std::numeric_limits<uint32_t>::max();
			return count > static_cast<uint64_t>(maximum)
				? maximum
				: static_cast<uint32_t>(count);
		}

		uint32_t DetectProcessorCount() noexcept {
#if defined(IDX_PLATFORM_WINDOWS)
			const DWORD count = ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
			if (count != 0) return ClampProcessorCount(static_cast<uint64_t>(count));
#elif defined(IDX_PLATFORM_LINUX)
			const long count = ::sysconf(_SC_NPROCESSORS_ONLN);
			if (count > 0) return ClampProcessorCount(static_cast<uint64_t>(count));
#endif

			const unsigned int fallback = std::thread::hardware_concurrency();
			return fallback != 0 ? ClampProcessorCount(static_cast<uint64_t>(fallback)) : 1u;
		}

		CpuDetails DetectCpuDetails() noexcept {
			CpuDetails details{};
#if IDX_ENGINE_HAS_CPUID
			DetectCpuId(details);
#endif
#if defined(IDX_PLATFORM_LINUX)
			if (details.VendorNameLength == 0 || details.NameLength == 0) DetectLinuxCpuInfo(details);
#endif
			if (details.VendorNameLength == 0) {
				details.VendorNameLength = StoreText(details.VendorName, "Unknown");
			}
			if (details.NameLength == 0) {
				details.NameLength = StoreText(details.Name, "Unknown");
			}
			details.Vendor = ParseCpuVendor(
				std::string_view(details.VendorName.data(), details.VendorNameLength));
			details.ProcessorCount = DetectProcessorCount();
			return details;
		}

		const CpuDetails& GetCpuDetails() noexcept {
			static const CpuDetails details = DetectCpuDetails();
			return details;
		}

		std::string_view GraphicsApiName(GraphicsApi api) noexcept {
			switch (api) {
				case GraphicsApi::Direct3D11: return "Direct3D11";
				case GraphicsApi::Direct3D12: return "Direct3D12";
				case GraphicsApi::Vulkan:     return "Vulkan";
				case GraphicsApi::Metal:      return "Metal";
				case GraphicsApi::OpenGL:     return "OpenGL";
				case GraphicsApi::OpenGLES:   return "OpenGLES";
				case GraphicsApi::WebGPU:     return "WebGPU";
				case GraphicsApi::Unknown:    return {};
			}
			return {};
		}
	}

	std::string_view Engine::GetVersion() noexcept {
		return IDX_VERSION;
	}

	std::string_view Engine::GetVersionLong() noexcept {
		return IDX_VERSION_LONG;
	}

	GraphicsApi Engine::GetGraphicsApi() noexcept {
#if INDEX_WITH_RENDER
		return RenderApi::GetGraphicsApi();
#else
		return GraphicsApi::Unknown;
#endif
	}

	std::string_view Engine::GetGraphicsApiName() noexcept {
		return GraphicsApiName(GetGraphicsApi());
	}

	GpuVendor Engine::GetGpuVendor() noexcept {
#if INDEX_WITH_RENDER
		return RenderApi::GetGpuVendor();
#else
		return GpuVendor::Unknown;
#endif
	}

	std::string_view Engine::GetGpuVendorName() noexcept {
#if INDEX_WITH_RENDER
		const std::string& name = RenderApi::GetVendorString();
		return std::string_view(name.data(), name.size());
#else
		return {};
#endif
	}

	std::string_view Engine::GetGpuName() noexcept {
#if INDEX_WITH_RENDER
		const std::string& name = RenderApi::GetRendererString();
		return std::string_view(name.data(), name.size());
#else
		return {};
#endif
	}

	CpuVendor Engine::GetCpuVendor() noexcept {
		return GetCpuDetails().Vendor;
	}

	std::string_view Engine::GetCpuVendorName() noexcept {
		const CpuDetails& details = GetCpuDetails();
		return std::string_view(details.VendorName.data(), details.VendorNameLength);
	}

	std::string_view Engine::GetCpuName() noexcept {
		const CpuDetails& details = GetCpuDetails();
		return std::string_view(details.Name.data(), details.NameLength);
	}

	uint32_t Engine::GetProcessorCount() noexcept {
		return GetCpuDetails().ProcessorCount;
	}

} // namespace Index

#undef IDX_ENGINE_HAS_CPUID
