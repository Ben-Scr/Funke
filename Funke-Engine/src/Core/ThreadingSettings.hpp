#pragma once

#include <algorithm>
#include <cstdint>

namespace Funke {
	enum class ParallelWorkload : uint32_t {
		Particles = 1u << 0,
		SkeletalPoses = 1u << 1,
		Rendering2D = 1u << 2,
		Rendering3D = 1u << 3,
		Physics2D = 1u << 4,
	};
	inline constexpr uint32_t k_AllParallelWorkloads = (1u << 5) - 1;

	struct ThreadingSettings {
		static constexpr int MaxWorkers = 32;
		// -1 selects automatically; 0 runs without workers.
		int WorkerCount = -1;
		int Physics3DWorkerCount = -1;
		bool ParallelParticles = true;
		bool ParallelSkeletalPoses = true;
		bool ParallelRendering2D = true;
		bool ParallelRendering3D = true;
		bool ParallelPhysics2D = true;

		void Clamp() {
			WorkerCount = std::clamp(WorkerCount, -1, MaxWorkers);
			Physics3DWorkerCount = std::clamp(Physics3DWorkerCount, -1, MaxWorkers);
		}

		uint32_t GetParallelWorkloads() const {
			return (ParallelParticles ? uint32_t(ParallelWorkload::Particles) : 0)
				| (ParallelSkeletalPoses ? uint32_t(ParallelWorkload::SkeletalPoses) : 0)
				| (ParallelRendering2D ? uint32_t(ParallelWorkload::Rendering2D) : 0)
				| (ParallelRendering3D ? uint32_t(ParallelWorkload::Rendering3D) : 0)
				| (ParallelPhysics2D ? uint32_t(ParallelWorkload::Physics2D) : 0);
		}

		int ResolvePhysics3DWorkers(unsigned hardwareThreads) const {
			return Physics3DWorkerCount >= 0 ? std::min(Physics3DWorkerCount, MaxWorkers)
				: static_cast<int>(std::clamp(std::max(2u, hardwareThreads) - 1u, 1u, 8u));
		}

		bool operator==(const ThreadingSettings&) const = default;
	};
}
