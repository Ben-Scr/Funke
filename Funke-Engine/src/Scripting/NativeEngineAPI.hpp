#pragma once

#include <cstdint>

namespace Funke {

	struct NativeEngineAPI {
		void (*LogInfo)(const char* msg, const char* file, int line);
		void (*LogWarn)(const char* msg, const char* file, int line);
		void (*LogError)(const char* msg, const char* file, int line);
		float (*GetDeltaTime)();
		void (*GetPosition)(uint32_t entity, float* x, float* y);
		void (*SetPosition)(uint32_t entity, float x, float y);
		float (*GetRotation)(uint32_t entity);
		void (*SetRotation)(uint32_t entity, float rot);
	};

	inline NativeEngineAPI* g_EngineAPI = nullptr;

} // namespace Funke
