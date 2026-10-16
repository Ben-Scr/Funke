#pragma once
#include "Core/Export.hpp"
#include "Inspector/PropertyValue.hpp"
#include "Scene/Entity.hpp"
#include "Scene/EntityHandle.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Index {
	class Scene;
	struct AnimationTrack;
	struct PropertyDescriptor;
	struct ComponentInfo;

	// "Position.x" -> { "Position", 0 }; "Color.a" -> { "Color", 3 }; "SpriteName" -> { "SpriteName", -1 }.
	struct AnimationPropertyPath {
		std::string Base;
		int Channel = -1;
	};

	INDEX_API AnimationPropertyPath ParseAnimationProperty(std::string_view property);
	INDEX_API const char* AnimationChannelSuffix(PropertyType type, int channel);
	// Number of float channels of a vector/color type; 1 for scalars, 0 for types that can only be stepped.
	INDEX_API int AnimationChannelCount(PropertyType type);
	// True for values a curve can drive (numbers, bools, vectors, colors); everything else needs discrete keys.
	INDEX_API bool IsCurveAnimatable(PropertyType type);

	INDEX_API float ReadAnimationChannel(const PropertyValue& value, int channel);
	INDEX_API void WriteAnimationChannel(PropertyValue& value, int channel, float channelValue);

	// Child of root reached by '/'-separated entity names; root itself for an empty path; invalid if missing.
	INDEX_API Entity ResolveAnimationPath(Entity root, std::string_view path);
	// Path from root to target for ResolveAnimationPath; empty for root, and also empty when target is not below root
	// (check with IsAncestorOf first).
	INDEX_API std::string MakeAnimationPath(Entity root, Entity target);

	// Serialized name or C# script class plus property, resolved to something readable and writable.
	struct INDEX_API AnimationTarget {
		enum class Kind : uint8_t { Property, ScriptField };

		Kind TargetKind = Kind::Property;
		EntityHandle EntityId = entt::null;
		std::string Component;
		std::string Property;

		const ComponentInfo* Info = nullptr;
		const PropertyDescriptor* Descriptor = nullptr;
		bool IsTransform = false;
		bool IsUI = false;

		std::string ScriptClass;
		uint32_t ScriptHandle = 0;
		PropertyType ScriptType = PropertyType::None;
		// Script fields are only read back through JSON, so the last value written is kept here.
		PropertyValue Cached;
		bool HasCached = false;
	};

	// The targets one Animator writes to. Tracks of all its clips share targets, so a crossfade between clips that
	// animate the same property blends one value.
	class INDEX_API AnimationTargetSet {
	public:
		// Index into GetTargets(), or -1 with outError set. outChannel receives the parsed channel.
		int Resolve(Scene& scene, Entity root, const AnimationTrack& track, int& outChannel, std::string& outError);

		bool Read(Scene& scene, int target, PropertyValue& outValue);
		// False when the target went away (entity destroyed, script reloaded); the owner should rebind.
		bool Write(Scene& scene, int target, const PropertyValue& value);

		// Adopts an already resolved target (the editor keeps authored values this way); returns its index.
		int Add(const AnimationTarget& target);
		PropertyType GetType(int target) const;
		const std::vector<AnimationTarget>& GetTargets() const { return m_Targets; }
		void Clear() { m_Targets.clear(); }

		// Looks up the property descriptor of a registered component by (case-insensitive) name.
		static const PropertyDescriptor* FindDescriptor(const ComponentInfo& info, std::string_view name);

	private:
		std::vector<AnimationTarget> m_Targets;
	};

}
