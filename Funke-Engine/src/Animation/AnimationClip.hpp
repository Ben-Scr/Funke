#pragma once
#include "Core/Export.hpp"
#include "Serialization/Json.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Funke {

	// Serialized by name ("linear", "step", "bezier"); the interpolation of a key applies to the segment after it.
	enum class KeyInterpolation : uint8_t {
		Linear = 0,
		Step = 1,
		// Cubic Hermite through the key's OutTangent and the next key's InTangent (slopes in value per second).
		Bezier = 2
	};

	struct FUNKE_API Keyframe {
		float Time = 0.0f;
		float Value = 0.0f;
		KeyInterpolation Interpolation = KeyInterpolation::Linear;
		float InTangent = 0.0f;
		float OutTangent = 0.0f;

		bool operator==(const Keyframe&) const = default;
	};

	struct FUNKE_API AnimationCurve {
		// Sorted by Time; Evaluate clamps outside the first and last key.
		std::vector<Keyframe> Keys;

		float Evaluate(float time) const;
		bool Empty() const { return Keys.empty(); }
		float GetEndTime() const { return Keys.empty() ? 0.0f : Keys.back().Time; }

		// Inserts or replaces the key at time (within half a millisecond); returns its index.
		int SetKey(float time, float value, KeyInterpolation interpolation = KeyInterpolation::Linear);
		void RemoveKey(int index);
		void Sort();
		// Catmull-Rom slopes from the neighbours; flat at the ends.
		void SmoothTangents(int index);

		bool operator==(const AnimationCurve&) const = default;
	};

	// A key that holds a value in the property's text form (asset GUID, "<guid>|slice|<name>", enum number); applied as a step.
	struct FUNKE_API DiscreteKeyframe {
		float Time = 0.0f;
		std::string Value;

		bool operator==(const DiscreteKeyframe&) const = default;
	};

	// One animated value: the Property ("position.x", "color.a", "Texture") of Component on the entity at Path.
	struct FUNKE_API AnimationTrack {
		// Child names joined by '/', relative to the Animator's entity; empty = the Animator's entity itself.
		std::string Path;
		// Serialized component name ("Transform3D") or C# script class name.
		std::string Component;
		std::string Property;

		// Numeric tracks use Curve; asset/string/enum tracks use DiscreteKeys. A track never has both.
		AnimationCurve Curve;
		std::vector<DiscreteKeyframe> DiscreteKeys;

		bool IsDiscrete() const { return !DiscreteKeys.empty(); }
		float GetEndTime() const;
		std::string GetDiscreteValue(float time) const;

		bool operator==(const AnimationTrack&) const = default;
	};

	// Calls Function(Argument) on the scripts of the Animator's entity when playback passes Time.
	struct FUNKE_API AnimationEvent {
		float Time = 0.0f;
		std::string Function;
		std::string Argument;

		bool operator==(const AnimationEvent&) const = default;
	};

	// .anim asset (JSON, GUID via its .meta).
	struct FUNKE_API AnimationClip {
		float Length = 1.0f;
		bool Loop = true;
		// Frame grid of the Animation window; playback is continuous.
		float SampleRate = 60.0f;
		std::vector<AnimationTrack> Tracks;
		// Sorted by Time.
		std::vector<AnimationEvent> Events;

		AnimationTrack* FindTrack(const std::string& path, const std::string& component, const std::string& property);
		const AnimationTrack* FindTrack(const std::string& path, const std::string& component, const std::string& property) const;
		// Latest key or event time.
		float GetContentEndTime() const;

		bool operator==(const AnimationClip&) const = default;
	};

	inline constexpr int k_AnimationClipFormatVersion = 1;
	inline constexpr const char* k_AnimationClipExtension = ".anim";

	FUNKE_API const char* KeyInterpolationToString(KeyInterpolation interpolation);
	FUNKE_API KeyInterpolation KeyInterpolationFromString(const std::string& name);

	// Missing keys keep the defaults; keys and events are sorted on load so hand-written files work.
	FUNKE_API Json::Value AnimationClipToJson(const AnimationClip& clip);
	FUNKE_API AnimationClip AnimationClipFromJson(const Json::Value& value);

	// Shared live .anim instances by asset GUID (main thread). A reload or Save replaces the pointer, so holders compare
	// it to notice changes.
	class FUNKE_API AnimationClipLibrary {
	public:
		AnimationClipLibrary() = delete;

		// nullptr for 0, unknown GUIDs and unreadable files (retried at most once per second).
		static std::shared_ptr<const AnimationClip> Get(uint64_t guid);

		static bool LoadFromFile(const std::string& path, AnimationClip& outClip);
		static bool SaveToFile(const std::string& path, const AnimationClip& clip);

		// Writes the asset's file and replaces the live instance.
		static bool Save(uint64_t guid, const AnimationClip& clip);
		// Replaces the live instance only (editor preview of unsaved edits); the file is untouched.
		static void Set(uint64_t guid, const AnimationClip& clip);

		static void Invalidate(uint64_t guid);
		static size_t ReloadChangedFromDisk();
		static void Clear();
	};

}
