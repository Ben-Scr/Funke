#pragma once

#include <cstdint>

namespace Index {

	enum class GraphicsApi : uint8_t {
		Unknown = 0,
		Direct3D11 = 1,
		Direct3D12 = 2,
		Vulkan = 3,
		Metal = 4,
		OpenGL = 5,
		OpenGLES = 6,
		WebGPU = 7,
	};

	enum class GpuVendor : uint8_t {
		Unknown = 0,
		NVIDIA = 1,
		AMD = 2,
		Intel = 3,
		Apple = 4,
		Qualcomm = 5,
		ARM = 6,
		Microsoft = 7,
	};

	enum class CpuVendor : uint8_t {
		Unknown = 0,
		Intel = 1,
		AMD = 2,
		Apple = 3,
		ARM = 4,
		Qualcomm = 5,
	};

} // namespace Index
