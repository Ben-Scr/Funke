#pragma once

#include "Scripting/Bolt/NativeApi.hpp"
#include <atomic>
#include <vector>

namespace Funke::BoltScriptHost {
	// A Bolt value can escape the callback that created it. End its native borrow
	// at callback exit and reject later attempts to read the saved query rows.
	class SceneQueryBorrow {
	public:
		SceneQueryBorrow() = default;
		SceneQueryBorrow(SceneQueryBorrow&&) noexcept = default;
		SceneQueryBorrow(const SceneQueryBorrow&) = delete;
		SceneQueryBorrow& operator=(const SceneQueryBorrow&) = delete;
		~SceneQueryBorrow() { CloseAll(); }

		void CloseAll() {
			for (const auto& entry : m_Borrows)
				ScriptApi().Scene_EndQueryBorrow(entry.Token);
			m_Borrows.clear();
		}

		uint64_t Call(uint64_t op, uint64_t handle, const std::string& scene) {
			if (op == 0) {
				const auto token = ScriptApi().Scene_BeginQueryBorrow(scene.c_str());
				if (!token) return 0;
				const auto id = s_Next.fetch_add(1, std::memory_order_relaxed);
				try { m_Borrows.push_back({id, token}); }
				catch (...) {
					ScriptApi().Scene_EndQueryBorrow(token);
					return 0;
				}
				return id;
			}
			for (auto it = m_Borrows.begin(); it != m_Borrows.end(); ++it) {
				if (it->Id != handle) continue;
				if (op == 1) return 1;
				if (op != 2) return 0;
				ScriptApi().Scene_EndQueryBorrow(it->Token);
				m_Borrows.erase(it);
				return 1;
			}
			return 0;
		}

	private:
		struct Borrow { uint64_t Id, Token; };
		inline static std::atomic<uint64_t> s_Next{1};
		std::vector<Borrow> m_Borrows;
	};
}
