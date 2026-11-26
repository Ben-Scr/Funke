#include "pch.hpp"
#include "Serialization/EntityReferenceSerialization.hpp"

namespace Funke::Detail {
	namespace { thread_local const std::unordered_map<uint64_t, uint64_t>* s_WriteMapping = nullptr; }
	EntityReferenceWriteScope::EntityReferenceWriteScope(const std::unordered_map<uint64_t, uint64_t>& mapping)
		: m_Previous(s_WriteMapping) { s_WriteMapping = &mapping; }
	EntityReferenceWriteScope::~EntityReferenceWriteScope() { s_WriteMapping = m_Previous; }
	uint64_t MapWrittenEntityReference(uint64_t persistentId) {
		if (s_WriteMapping) if (auto it = s_WriteMapping->find(persistentId); it != s_WriteMapping->end()) return it->second;
		return persistentId;
	}
}
