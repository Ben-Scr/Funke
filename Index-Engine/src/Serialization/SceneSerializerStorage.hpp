#pragma once

#include "Core/Export.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Index::Json {
	class Value;
}

namespace Index::SceneSerializerStorage {
	INDEX_API bool ReadRootFromFile(const std::string& path, Json::Value& outRoot, std::string* outError = nullptr);
	INDEX_API bool WriteRootToFile(const std::string& path, const Json::Value& root, SceneSerializationFormat format);
	INDEX_API bool IsBinaryData(const std::vector<std::uint8_t>& bytes);

	struct BinarySceneEntityCursor {
		const std::vector<std::uint8_t>* bytes = nullptr;
		std::size_t offset = 0;
		std::uint32_t remaining = 0;
	};

	INDEX_API bool BeginBinaryScene(const std::vector<std::uint8_t>& bytes, Json::Value& outHeader,
		BinarySceneEntityCursor& outCursor, std::string* outError = nullptr);
	INDEX_API bool ReadNextBinaryEntity(BinarySceneEntityCursor& cursor, Json::Value& outEntity,
		std::string* outError = nullptr);
}
