#include "pch.hpp"
#include "Serialization/SceneSerializerStorage.hpp"

#if !(INDEX_WITH_RENDER && INDEX_WITH_AUDIO && INDEX_WITH_PHYSICS && INDEX_WITH_SCRIPTING)

#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

namespace Index::SceneSerializerStorage {
	using Json::Value;

	namespace {
		constexpr std::uint8_t k_BinaryMagic[] = {
			'I', 'D', 'X', 'S', 'C', 'N', 'B', 0x1A
		};
		constexpr std::uint32_t k_BinaryStorageVersion = 1;

		enum class BinaryValueTag : std::uint8_t {
			Null = 0,
			Bool,
			Double,
			Int64,
			UInt64,
			String,
			Object,
			Array
		};

		void WriteU8(std::vector<std::uint8_t>& out, std::uint8_t value) {
			out.push_back(value);
		}

		void WriteU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
			for (int i = 0; i < 4; ++i) {
				out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
			}
		}

		void WriteU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
			for (int i = 0; i < 8; ++i) {
				out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
			}
		}

		bool WriteString(std::vector<std::uint8_t>& out, const std::string& value) {
			if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
				return false;
			}
			WriteU32(out, static_cast<std::uint32_t>(value.size()));
			out.insert(out.end(), value.begin(), value.end());
			return true;
		}

		bool WriteValue(std::vector<std::uint8_t>& out, const Json::Value& value) {
			switch (value.GetType()) {
			case Json::Value::Type::Null:
				WriteU8(out, static_cast<std::uint8_t>(BinaryValueTag::Null));
				return true;
			case Json::Value::Type::Bool:
				WriteU8(out, static_cast<std::uint8_t>(BinaryValueTag::Bool));
				WriteU8(out, value.AsBoolOr(false) ? 1u : 0u);
				return true;
			case Json::Value::Type::Number:
				switch (value.GetNumberKind()) {
				case Json::Value::NumberKind::Double: {
					WriteU8(out, static_cast<std::uint8_t>(BinaryValueTag::Double));
					std::uint64_t bits = 0;
					const double number = value.AsDoubleOr(0.0);
					std::memcpy(&bits, &number, sizeof(bits));
					WriteU64(out, bits);
					return true;
				}
				case Json::Value::NumberKind::Int64:
					WriteU8(out, static_cast<std::uint8_t>(BinaryValueTag::Int64));
					WriteU64(out, static_cast<std::uint64_t>(value.AsInt64Or(0)));
					return true;
				case Json::Value::NumberKind::UInt64:
					WriteU8(out, static_cast<std::uint8_t>(BinaryValueTag::UInt64));
					WriteU64(out, value.AsUInt64Or(0));
					return true;
				}
				return false;
			case Json::Value::Type::String:
				WriteU8(out, static_cast<std::uint8_t>(BinaryValueTag::String));
				return WriteString(out, value.AsStringOr());
			case Json::Value::Type::Object: {
				const auto& object = value.GetObject();
				if (object.size() > std::numeric_limits<std::uint32_t>::max()) {
					return false;
				}
				WriteU8(out, static_cast<std::uint8_t>(BinaryValueTag::Object));
				WriteU32(out, static_cast<std::uint32_t>(object.size()));
				for (const auto& [key, memberValue] : object) {
					if (!WriteString(out, key) || !WriteValue(out, memberValue)) {
						return false;
					}
				}
				return true;
			}
			case Json::Value::Type::Array: {
				const auto& array = value.GetArray();
				if (array.size() > std::numeric_limits<std::uint32_t>::max()) {
					return false;
				}
				WriteU8(out, static_cast<std::uint8_t>(BinaryValueTag::Array));
				WriteU32(out, static_cast<std::uint32_t>(array.size()));
				for (const Json::Value& item : array) {
					if (!WriteValue(out, item)) {
						return false;
					}
				}
				return true;
			}
			}
			return false;
		}

		class BinaryReader {
		public:
			explicit BinaryReader(const std::vector<std::uint8_t>& bytes)
				: m_Bytes(bytes) {}

			bool ReadU8(std::uint8_t& out) {
				if (Remaining() < 1) return false;
				out = m_Bytes[m_Offset++];
				return true;
			}

			bool ReadU32(std::uint32_t& out) {
				if (Remaining() < 4) return false;
				out = 0;
				for (int i = 0; i < 4; ++i) {
					out |= static_cast<std::uint32_t>(m_Bytes[m_Offset++]) << (i * 8);
				}
				return true;
			}

			bool ReadU64(std::uint64_t& out) {
				if (Remaining() < 8) return false;
				out = 0;
				for (int i = 0; i < 8; ++i) {
					out |= static_cast<std::uint64_t>(m_Bytes[m_Offset++]) << (i * 8);
				}
				return true;
			}

			bool ReadString(std::string& out) {
				std::uint32_t size = 0;
				if (!ReadU32(size) || Remaining() < size) return false;
				out.assign(reinterpret_cast<const char*>(m_Bytes.data() + m_Offset), size);
				m_Offset += size;
				return true;
			}

			bool ReadValue(Value& out, int depth = 0) {
				constexpr int k_MaxDepth = 256;
				if (depth > k_MaxDepth) return false;

				std::uint8_t rawTag = 0;
				if (!ReadU8(rawTag)) return false;

				switch (static_cast<BinaryValueTag>(rawTag)) {
				case BinaryValueTag::Null:
					out = Value();
					return true;
				case BinaryValueTag::Bool: {
					std::uint8_t value = 0;
					if (!ReadU8(value)) return false;
					out = Value(value != 0);
					return true;
				}
				case BinaryValueTag::Double: {
					std::uint64_t bits = 0;
					if (!ReadU64(bits)) return false;
					double number = 0.0;
					std::memcpy(&number, &bits, sizeof(number));
					out = Value(number);
					return true;
				}
				case BinaryValueTag::Int64: {
					std::uint64_t bits = 0;
					if (!ReadU64(bits)) return false;
					out = Value(static_cast<std::int64_t>(bits));
					return true;
				}
				case BinaryValueTag::UInt64: {
					std::uint64_t value = 0;
					if (!ReadU64(value)) return false;
					out = Value(value);
					return true;
				}
				case BinaryValueTag::String: {
					std::string value;
					if (!ReadString(value)) return false;
					out = Value(std::move(value));
					return true;
				}
				case BinaryValueTag::Object: {
					std::uint32_t count = 0;
					if (!ReadU32(count)) return false;
					out = Value::MakeObject();
					// Every element takes at least one byte, so cap untrusted counts by what is left in the buffer.
					out.ReserveMembers(std::min<std::size_t>(count, Remaining()));
					for (std::uint32_t i = 0; i < count; ++i) {
						std::string key;
						Value memberValue;
						if (!ReadString(key) || !ReadValue(memberValue, depth + 1)) return false;
						out.AddMemberUnchecked(std::move(key), std::move(memberValue));
					}
					return true;
				}
				case BinaryValueTag::Array: {
					std::uint32_t count = 0;
					if (!ReadU32(count)) return false;
					out = Value::MakeArray();
					out.EnsureArray().reserve(std::min<std::size_t>(count, Remaining()));
					for (std::uint32_t i = 0; i < count; ++i) {
						Value item;
						if (!ReadValue(item, depth + 1)) return false;
						out.Append(std::move(item));
					}
					return true;
				}
				}
				return false;
			}

			bool AtEnd() const { return m_Offset == m_Bytes.size(); }
			std::size_t Tell() const { return m_Offset; }
			void Seek(std::size_t offset) { m_Offset = offset; }

			bool SkipBytes(std::size_t count) {
				if (Remaining() < count) return false;
				m_Offset += count;
				return true;
			}

			bool SkipString() {
				std::uint32_t size = 0;
				return ReadU32(size) && SkipBytes(size);
			}

			bool SkipValue(int depth = 0) {
				constexpr int k_MaxDepth = 256;
				if (depth > k_MaxDepth) return false;

				std::uint8_t rawTag = 0;
				if (!ReadU8(rawTag)) return false;

				switch (static_cast<BinaryValueTag>(rawTag)) {
				case BinaryValueTag::Null: return true;
				case BinaryValueTag::Bool: return SkipBytes(1);
				case BinaryValueTag::Double:
				case BinaryValueTag::Int64:
				case BinaryValueTag::UInt64: return SkipBytes(8);
				case BinaryValueTag::String: return SkipString();
				case BinaryValueTag::Object: {
					std::uint32_t count = 0;
					if (!ReadU32(count)) return false;
					for (std::uint32_t i = 0; i < count; ++i) {
						if (!SkipString() || !SkipValue(depth + 1)) return false;
					}
					return true;
				}
				case BinaryValueTag::Array: {
					std::uint32_t count = 0;
					if (!ReadU32(count)) return false;
					for (std::uint32_t i = 0; i < count; ++i) {
						if (!SkipValue(depth + 1)) return false;
					}
					return true;
				}
				}
				return false;
			}

		private:
			std::size_t Remaining() const { return m_Bytes.size() - m_Offset; }

			const std::vector<std::uint8_t>& m_Bytes;
			std::size_t m_Offset = 0;
		};

		std::vector<std::uint8_t> EncodeBinaryRoot(const Json::Value& root) {
			std::vector<std::uint8_t> bytes;
			bytes.reserve(256);
			bytes.insert(bytes.end(), std::begin(k_BinaryMagic), std::end(k_BinaryMagic));
			WriteU32(bytes, k_BinaryStorageVersion);
			if (!WriteValue(bytes, root)) {
				return {};
			}
			return bytes;
		}

		bool DecodeBinaryRoot(const std::vector<std::uint8_t>& bytes, Value& outRoot, std::string* outError) {
			if (!IsBinaryData(bytes)) {
				if (outError) *outError = "Missing Index binary scene magic";
				return false;
			}

			BinaryReader reader(bytes);
			for (std::size_t i = 0; i < sizeof(k_BinaryMagic); ++i) {
				std::uint8_t ignored = 0;
				if (!reader.ReadU8(ignored)) {
					if (outError) *outError = "Truncated binary scene header";
					return false;
				}
			}

			std::uint32_t version = 0;
			if (!reader.ReadU32(version)) {
				if (outError) *outError = "Truncated binary scene version";
				return false;
			}
			if (version > k_BinaryStorageVersion) {
				if (outError) *outError = "Binary scene version is newer than this engine";
				return false;
			}
			if (!reader.ReadValue(outRoot)) {
				if (outError) *outError = "Failed to decode binary scene payload";
				return false;
			}
			if (!reader.AtEnd()) {
				if (outError) *outError = "Binary scene has trailing bytes";
				return false;
			}
			return true;
		}
	}

	bool IsBinaryData(const std::vector<std::uint8_t>& bytes) {
		return bytes.size() >= sizeof(k_BinaryMagic)
			&& std::equal(std::begin(k_BinaryMagic), std::end(k_BinaryMagic), bytes.begin());
	}

	bool BeginBinaryScene(
		const std::vector<std::uint8_t>& bytes,
		Value& outHeader,
		BinarySceneEntityCursor& outCursor,
		std::string* outError) {
		outCursor = BinarySceneEntityCursor{};

		if (!IsBinaryData(bytes)) {
			if (outError) *outError = "Missing Index binary scene magic";
			return false;
		}

		BinaryReader reader(bytes);
		for (std::size_t i = 0; i < sizeof(k_BinaryMagic); ++i) {
			std::uint8_t ignored = 0;
			if (!reader.ReadU8(ignored)) {
				if (outError) *outError = "Truncated binary scene header";
				return false;
			}
		}

		std::uint32_t version = 0;
		if (!reader.ReadU32(version)) {
			if (outError) *outError = "Truncated binary scene version";
			return false;
		}
		if (version > k_BinaryStorageVersion) {
			if (outError) *outError = "Binary scene version is newer than this engine";
			return false;
		}

		std::uint8_t rootTag = 0;
		if (!reader.ReadU8(rootTag)
			|| static_cast<BinaryValueTag>(rootTag) != BinaryValueTag::Object) {
			if (outError) *outError = "Binary scene root is not an object";
			return false;
		}

		std::uint32_t memberCount = 0;
		if (!reader.ReadU32(memberCount)) {
			if (outError) *outError = "Truncated binary scene root";
			return false;
		}

		outHeader = Value::MakeObject();
		// Untrusted count: cap the up-front reservation; real members still append past it.
		outHeader.ReserveMembers(std::min<std::uint32_t>(memberCount, 256u));
		for (std::uint32_t i = 0; i < memberCount; ++i) {
			std::string key;
			if (!reader.ReadString(key)) {
				if (outError) *outError = "Truncated binary scene member key";
				return false;
			}

			if (key == "entities") {
				std::uint8_t arrayTag = 0;
				if (!reader.ReadU8(arrayTag)
					|| static_cast<BinaryValueTag>(arrayTag) != BinaryValueTag::Array) {
					if (outError) *outError = "Binary scene 'entities' is not an array";
					return false;
				}
				std::uint32_t entityCount = 0;
				if (!reader.ReadU32(entityCount)) {
					if (outError) *outError = "Truncated binary scene 'entities' count";
					return false;
				}
				outCursor.bytes = &bytes;
				outCursor.offset = reader.Tell();
				outCursor.remaining = entityCount;
				for (std::uint32_t entityIndex = 0; entityIndex < entityCount; ++entityIndex) {
					if (!reader.SkipValue()) {
						if (outError) *outError = "Truncated binary scene entity";
						outCursor = BinarySceneEntityCursor{};
						return false;
					}
				}
			}
			else {
				Value memberValue;
				if (!reader.ReadValue(memberValue)) {
					if (outError) *outError = "Failed to decode binary scene header member";
					return false;
				}
				outHeader.AddMemberUnchecked(std::move(key), std::move(memberValue));
			}
		}

		if (!reader.AtEnd()) {
			if (outError) *outError = "Binary scene has trailing bytes";
			return false;
		}
		return true;
	}

	bool ReadNextBinaryEntity(
		BinarySceneEntityCursor& cursor,
		Value& outEntity,
		std::string* outError) {
		if (cursor.bytes == nullptr || cursor.remaining == 0) return false;

		BinaryReader reader(*cursor.bytes);
		reader.Seek(cursor.offset);
		if (!reader.ReadValue(outEntity)) {
			if (outError) *outError = "Failed to decode binary scene entity";
			cursor.remaining = 0;
			return false;
		}
		cursor.offset = reader.Tell();
		--cursor.remaining;
		return true;
	}

	bool ReadRootFromFile(const std::string& path, Value& outRoot, std::string* outError) {
		const std::vector<std::uint8_t> bytes = File::ReadAllBytes(path);
		if (bytes.empty()) {
			if (outError) *outError = "File is empty";
			return false;
		}

		if (IsBinaryData(bytes)) return DecodeBinaryRoot(bytes, outRoot, outError);

		const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		return Json::TryParse(text, outRoot, outError);
	}

	bool WriteRootToFile(const std::string& path, const Json::Value& root, SceneSerializationFormat format) {
		if (format == SceneSerializationFormat::Binary) {
			const std::vector<std::uint8_t> bytes = EncodeBinaryRoot(root);
			return !bytes.empty() && File::WriteAllBytes(path, bytes);
		}
		return File::WriteAllText(path, Json::Stringify(root, true));
	}
}

#endif
