#pragma once
#include "Core/Export.hpp"
#include <cstdint>
#include <unordered_map>

namespace Funke::Detail {
	// Only typed entity references use this mapping; asset GUIDs and arbitrary numbers never do.
	class FUNKE_API EntityReferenceWriteScope {
	public:
		explicit EntityReferenceWriteScope(const std::unordered_map<uint64_t, uint64_t>& mapping);
		~EntityReferenceWriteScope();
		EntityReferenceWriteScope(const EntityReferenceWriteScope&) = delete;
		EntityReferenceWriteScope& operator=(const EntityReferenceWriteScope&) = delete;
	private:
		const std::unordered_map<uint64_t, uint64_t>* m_Previous;
	};
	FUNKE_API uint64_t MapWrittenEntityReference(uint64_t persistentId);
}
