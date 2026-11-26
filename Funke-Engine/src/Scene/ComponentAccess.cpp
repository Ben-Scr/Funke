#include "pch.hpp"
#include "Scene/ComponentAccess.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/ComponentRegistry.hpp"
#include <atomic>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <algorithm>

namespace Funke::ComponentAccess {
	namespace {
		std::atomic<uint64_t> s_NextIdentity{2};
		struct State {
			uint64_t Instance = s_NextIdentity.fetch_add(1, std::memory_order_relaxed);
			std::atomic<uint64_t> Revision{1};
			uint32_t Borrows = 0;
			std::atomic<uint32_t> Jobs{0};
			std::thread::id Owner = std::this_thread::get_id();
		};
		using StatePtr = std::shared_ptr<State>;
		thread_local std::unordered_map<uint64_t, StatePtr> t_Borrows;
		std::atomic<uint32_t> s_ActiveBorrows{0};
		struct JobBorrow { StatePtr State; std::vector<uint32_t> Writes, Reads; };
		std::mutex s_JobMutex;
		std::unordered_map<uint64_t, JobBorrow> s_JobBorrows;
		std::atomic<uint32_t> s_ActiveJobs{0};
		bool Overlaps(std::span<const uint32_t> left, std::span<const uint32_t> right) {
			return std::any_of(left.begin(), left.end(), [&](uint32_t id) { return std::find(right.begin(), right.end(), id) != right.end(); });
		}
		StatePtr& GetState(entt::registry& registry) {
			if (auto* state = registry.ctx().find<StatePtr>()) return *state;
			return registry.ctx().emplace<StatePtr>(std::make_shared<State>());
		}
	}

	void BeforeMutation(const entt::registry& registry) {
		const auto* state = registry.ctx().find<StatePtr>();
		if (!state) return;
		if ((*state)->Jobs.load(std::memory_order_acquire) != 0)
			throw std::logic_error("Structural change blocked by an active query job. Complete its JobHandle first.");
#if defined(FNK_DEBUG) || defined(FNK_RELEASE)
		if ((*state)->Borrows != 0)
			throw std::logic_error("Structural change blocked by an active QueryRef. Dispose the query before Create/Destroy/Add/Remove, scene unload or ECB playback.");
#endif
		++(*state)->Revision;
	}

	void RequireNoBorrows() {
		if (s_ActiveJobs.load(std::memory_order_acquire) != 0)
			throw std::logic_error("Scene/schema change blocked by an active query job. Complete its JobHandle first.");
#if defined(FNK_DEBUG) || defined(FNK_RELEASE)
		if (s_ActiveBorrows.load(std::memory_order_relaxed) != 0)
			throw std::logic_error("Scene/schema change blocked by an active QueryRef. Dispose all queries first.");
#endif
	}

	uint64_t BeginBorrow(entt::registry& registry) {
		if (auto* state = registry.ctx().find<StatePtr>(); state && (*state)->Jobs.load(std::memory_order_acquire) != 0)
			throw std::logic_error("QueryRef conflicts with an active query job. Complete its JobHandle first.");
#if defined(FNK_DEBUG) || defined(FNK_RELEASE)
		auto state = GetState(registry);
		if (state->Owner != std::this_thread::get_id()) return 0;
		const uint64_t token = s_NextIdentity.fetch_add(1, std::memory_order_relaxed);
		t_Borrows.emplace(token, state);
		++state->Borrows;
		s_ActiveBorrows.fetch_add(1, std::memory_order_relaxed);
		return token;
#else
		return 1;
#endif
	}

	void EndBorrow(uint64_t token) noexcept {
#if defined(FNK_DEBUG) || defined(FNK_RELEASE)
		auto it = t_Borrows.find(token);
		if (it == t_Borrows.end()) return;
		--it->second->Borrows;
		s_ActiveBorrows.fetch_sub(1, std::memory_order_relaxed);
		t_Borrows.erase(it);
#endif
	}

	uint64_t BeginJobBorrow(entt::registry& registry, std::span<const uint32_t> writes, std::span<const uint32_t> reads) {
		auto state = GetState(registry);
		if (state->Owner != std::this_thread::get_id() || state->Borrows != 0) return 0;
		std::lock_guard lock(s_JobMutex);
		for (const auto& [token, active] : s_JobBorrows) {
			if (active.State != state) continue;
			if (Overlaps(writes, active.Writes) || Overlaps(writes, active.Reads) || Overlaps(reads, active.Writes)) return 0;
		}
		const uint64_t token = s_NextIdentity.fetch_add(1, std::memory_order_relaxed);
		s_JobBorrows.emplace(token, JobBorrow{state, {writes.begin(), writes.end()}, {reads.begin(), reads.end()}});
		state->Jobs.fetch_add(1, std::memory_order_release);
		s_ActiveJobs.fetch_add(1, std::memory_order_release);
		return token;
	}

	void EndJobBorrow(uint64_t token) noexcept {
		std::lock_guard lock(s_JobMutex);
		const auto it = s_JobBorrows.find(token);
		if (it == s_JobBorrows.end()) return;
		it->second.State->Jobs.fetch_sub(1, std::memory_order_release);
		s_ActiveJobs.fetch_sub(1, std::memory_order_release);
		s_JobBorrows.erase(it);
	}

	bool Capture(Entity entity, const ComponentInfo& info, uint64_t schema, Identity& identity) {
		identity = {};
		if (!entity.IsValid() || !info.getRaw) return false;
		if (auto* state = entity.GetRegistry()->ctx().find<StatePtr>(); state && (*state)->Jobs.load(std::memory_order_acquire) != 0) return false;
		if (!info.getRaw(entity)) return false;
		auto& state = GetState(*entity.GetRegistry());
		if (state->Owner != std::this_thread::get_id()) return false;
		identity = {state->Instance, static_cast<uint64_t>(entt::to_integral(entity.GetHandle())), state->Revision.load(std::memory_order_relaxed), schema};
		return true;
	}

	bool Transfer(Entity entity, const ComponentInfo& info, uint64_t schema,
		const Identity& identity, void* value, int size, bool write, size_t payloadSize) {
		if (!value || size <= 0 || static_cast<size_t>(size) != payloadSize || payloadSize > info.rawSize) return false;
		Identity current;
		if (!Capture(entity, info, schema, current) || identity != current) return false;
		void* component = info.getRaw(entity);
		if (!component) return false;
		if (write) {
			std::memcpy(component, value, payloadSize);
			entity.GetScene()->MarkTransformDirty(entity.GetHandle());
			entity.GetScene()->MarkAllDirtyOnce();
		}
		else std::memcpy(value, component, payloadSize);
		return true;
	}
}
