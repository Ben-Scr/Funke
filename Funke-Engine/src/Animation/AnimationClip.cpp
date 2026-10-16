#include "pch.hpp"
#include "Animation/AnimationClip.hpp"

#include "Animation/JsonAssetCache.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Serialization/File.hpp"
#include "Serialization/FileStamp.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace Funke {
	namespace {
		constexpr float k_KeyTimeEpsilon = 0.0005f;

		float ReadFloat(const Json::Value& object, std::string_view key, float fallback) {
			const Json::Value* v = object.FindMember(key);
			return v ? static_cast<float>(v->AsDoubleOr(fallback)) : fallback;
		}

		std::string ReadString(const Json::Value& object, std::string_view key) {
			const Json::Value* v = object.FindMember(key);
			if (!v) return {};
			// Numbers are accepted so hand-written GUID values without quotes still work.
			if (v->IsNumber()) {
				switch (v->GetNumberKind()) {
				case Json::Value::NumberKind::Int64: return std::to_string(v->AsInt64Or(0));
				case Json::Value::NumberKind::UInt64: return std::to_string(v->AsUInt64Or(0));
				default: return std::format("{}", v->AsDoubleOr(0.0));
				}
			}
			if (v->IsBool()) return v->AsBoolOr(false) ? "true" : "false";
			return v->AsStringOr();
		}

		JsonAssetCache<AnimationClip>& Cache() {
			static JsonAssetCache<AnimationClip> s_Cache(&AnimationClipLibrary::LoadFromFile);
			return s_Cache;
		}
	}

	float AnimationCurve::Evaluate(float time) const {
		if (Keys.empty()) return 0.0f;
		if (Keys.size() == 1 || time <= Keys.front().Time) return Keys.front().Value;
		if (time >= Keys.back().Time) return Keys.back().Value;

		const auto next = std::upper_bound(Keys.begin(), Keys.end(), time,
			[](float t, const Keyframe& key) { return t < key.Time; });
		const Keyframe& k1 = *next;
		const Keyframe& k0 = *(next - 1);
		const float span = k1.Time - k0.Time;
		if (span <= 0.0f) return k1.Value;
		const float t = (time - k0.Time) / span;

		switch (k0.Interpolation) {
		case KeyInterpolation::Step:
			return k0.Value;
		case KeyInterpolation::Bezier: {
			const float t2 = t * t;
			const float t3 = t2 * t;
			const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
			const float h10 = t3 - 2.0f * t2 + t;
			const float h01 = -2.0f * t3 + 3.0f * t2;
			const float h11 = t3 - t2;
			return h00 * k0.Value + h10 * span * k0.OutTangent + h01 * k1.Value + h11 * span * k1.InTangent;
		}
		case KeyInterpolation::Linear:
		default:
			return k0.Value + (k1.Value - k0.Value) * t;
		}
	}

	int AnimationCurve::SetKey(float time, float value, KeyInterpolation interpolation) {
		for (size_t i = 0; i < Keys.size(); ++i) {
			if (std::abs(Keys[i].Time - time) <= k_KeyTimeEpsilon) {
				Keys[i].Value = value;
				return static_cast<int>(i);
			}
		}
		Keyframe key;
		key.Time = time;
		key.Value = value;
		key.Interpolation = interpolation;
		const auto it = std::upper_bound(Keys.begin(), Keys.end(), time,
			[](float t, const Keyframe& k) { return t < k.Time; });
		const auto inserted = Keys.insert(it, key);
		return static_cast<int>(inserted - Keys.begin());
	}

	void AnimationCurve::RemoveKey(int index) {
		if (index < 0 || index >= static_cast<int>(Keys.size())) return;
		Keys.erase(Keys.begin() + index);
	}

	void AnimationCurve::Sort() {
		std::stable_sort(Keys.begin(), Keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.Time < b.Time; });
	}

	void AnimationCurve::SmoothTangents(int index) {
		if (index < 0 || index >= static_cast<int>(Keys.size())) return;
		Keyframe& key = Keys[static_cast<size_t>(index)];
		if (index == 0 || index == static_cast<int>(Keys.size()) - 1) {
			key.InTangent = key.OutTangent = 0.0f;
			return;
		}
		const Keyframe& prev = Keys[static_cast<size_t>(index - 1)];
		const Keyframe& next = Keys[static_cast<size_t>(index + 1)];
		const float span = next.Time - prev.Time;
		const float slope = span > 0.0f ? (next.Value - prev.Value) / span : 0.0f;
		key.InTangent = key.OutTangent = slope;
	}

	float AnimationTrack::GetEndTime() const {
		if (IsDiscrete()) return DiscreteKeys.back().Time;
		return Curve.GetEndTime();
	}

	std::string AnimationTrack::GetDiscreteValue(float time) const {
		if (DiscreteKeys.empty()) return {};
		const auto next = std::upper_bound(DiscreteKeys.begin(), DiscreteKeys.end(), time,
			[](float t, const DiscreteKeyframe& key) { return t < key.Time; });
		if (next == DiscreteKeys.begin()) return DiscreteKeys.front().Value;
		return (next - 1)->Value;
	}

	AnimationTrack* AnimationClip::FindTrack(const std::string& path, const std::string& component, const std::string& property) {
		for (AnimationTrack& track : Tracks) {
			if (track.Path == path && track.Component == component && track.Property == property) return &track;
		}
		return nullptr;
	}

	const AnimationTrack* AnimationClip::FindTrack(const std::string& path, const std::string& component, const std::string& property) const {
		return const_cast<AnimationClip*>(this)->FindTrack(path, component, property);
	}

	float AnimationClip::GetContentEndTime() const {
		float end = 0.0f;
		for (const AnimationTrack& track : Tracks) {
			if (!track.Curve.Empty() || track.IsDiscrete()) end = std::max(end, track.GetEndTime());
		}
		for (const AnimationEvent& e : Events) end = std::max(end, e.Time);
		return end;
	}

	const char* KeyInterpolationToString(KeyInterpolation interpolation) {
		switch (interpolation) {
		case KeyInterpolation::Step: return "step";
		case KeyInterpolation::Bezier: return "bezier";
		case KeyInterpolation::Linear:
		default: return "linear";
		}
	}

	KeyInterpolation KeyInterpolationFromString(const std::string& name) {
		if (name == "step" || name == "Step") return KeyInterpolation::Step;
		if (name == "bezier" || name == "Bezier" || name == "smooth") return KeyInterpolation::Bezier;
		return KeyInterpolation::Linear;
	}

	Json::Value AnimationClipToJson(const AnimationClip& clip) {
		Json::Value root = Json::Value::MakeObject();
		root.AddMember("type", Json::Value("AnimationClip"));
		root.AddMember("version", Json::Value(k_AnimationClipFormatVersion));
		root.AddMember("length", Json::Value(clip.Length));
		root.AddMember("loop", Json::Value(clip.Loop));
		root.AddMember("sampleRate", Json::Value(clip.SampleRate));

		Json::Value tracks = Json::Value::MakeArray();
		for (const AnimationTrack& track : clip.Tracks) {
			Json::Value t = Json::Value::MakeObject();
			t.AddMember("path", Json::Value(track.Path));
			t.AddMember("component", Json::Value(track.Component));
			t.AddMember("property", Json::Value(track.Property));
			if (track.IsDiscrete()) {
				Json::Value keys = Json::Value::MakeArray();
				for (const DiscreteKeyframe& key : track.DiscreteKeys) {
					Json::Value k = Json::Value::MakeObject();
					k.AddMember("t", Json::Value(key.Time));
					k.AddMember("v", Json::Value(key.Value));
					keys.Append(std::move(k));
				}
				t.AddMember("discrete", std::move(keys));
			}
			else {
				Json::Value keys = Json::Value::MakeArray();
				for (const Keyframe& key : track.Curve.Keys) {
					Json::Value k = Json::Value::MakeObject();
					k.AddMember("t", Json::Value(key.Time));
					k.AddMember("v", Json::Value(key.Value));
					if (key.Interpolation != KeyInterpolation::Linear) k.AddMember("interp", Json::Value(KeyInterpolationToString(key.Interpolation)));
					if (key.InTangent != 0.0f) k.AddMember("in", Json::Value(key.InTangent));
					if (key.OutTangent != 0.0f) k.AddMember("out", Json::Value(key.OutTangent));
					keys.Append(std::move(k));
				}
				t.AddMember("keys", std::move(keys));
			}
			tracks.Append(std::move(t));
		}
		root.AddMember("tracks", std::move(tracks));

		if (!clip.Events.empty()) {
			Json::Value events = Json::Value::MakeArray();
			for (const AnimationEvent& e : clip.Events) {
				Json::Value v = Json::Value::MakeObject();
				v.AddMember("t", Json::Value(e.Time));
				v.AddMember("function", Json::Value(e.Function));
				if (!e.Argument.empty()) v.AddMember("argument", Json::Value(e.Argument));
				events.Append(std::move(v));
			}
			root.AddMember("events", std::move(events));
		}
		return root;
	}

	AnimationClip AnimationClipFromJson(const Json::Value& value) {
		const AnimationClip d{};
		AnimationClip clip;
		if (!value.IsObject()) return clip;
		clip.Length = std::max(ReadFloat(value, "length", d.Length), 0.0f);
		const Json::Value* loop = value.FindMember("loop");
		clip.Loop = loop ? loop->AsBoolOr(d.Loop) : d.Loop;
		clip.SampleRate = std::clamp(ReadFloat(value, "sampleRate", d.SampleRate), 1.0f, 1000.0f);

		if (const Json::Value* tracks = value.FindMember("tracks"); tracks && tracks->IsArray()) {
			for (const Json::Value& t : tracks->GetArray()) {
				if (!t.IsObject()) continue;
				AnimationTrack track;
				track.Path = ReadString(t, "path");
				track.Component = ReadString(t, "component");
				track.Property = ReadString(t, "property");
				if (track.Component.empty() || track.Property.empty()) continue;

				if (const Json::Value* keys = t.FindMember("discrete"); keys && keys->IsArray()) {
					for (const Json::Value& k : keys->GetArray()) {
						if (!k.IsObject()) continue;
						DiscreteKeyframe key;
						key.Time = std::max(ReadFloat(k, "t", 0.0f), 0.0f);
						key.Value = ReadString(k, "v");
						track.DiscreteKeys.push_back(std::move(key));
					}
					std::stable_sort(track.DiscreteKeys.begin(), track.DiscreteKeys.end(),
						[](const DiscreteKeyframe& a, const DiscreteKeyframe& b) { return a.Time < b.Time; });
				}
				else if (const Json::Value* keys = t.FindMember("keys"); keys && keys->IsArray()) {
					for (const Json::Value& k : keys->GetArray()) {
						if (!k.IsObject()) continue;
						Keyframe key;
						key.Time = std::max(ReadFloat(k, "t", 0.0f), 0.0f);
						key.Value = ReadFloat(k, "v", 0.0f);
						key.Interpolation = KeyInterpolationFromString(ReadString(k, "interp"));
						key.InTangent = ReadFloat(k, "in", 0.0f);
						key.OutTangent = ReadFloat(k, "out", 0.0f);
						track.Curve.Keys.push_back(key);
					}
					track.Curve.Sort();
				}
				clip.Tracks.push_back(std::move(track));
			}
		}

		if (const Json::Value* events = value.FindMember("events"); events && events->IsArray()) {
			for (const Json::Value& v : events->GetArray()) {
				if (!v.IsObject()) continue;
				AnimationEvent e;
				e.Time = std::max(ReadFloat(v, "t", 0.0f), 0.0f);
				e.Function = ReadString(v, "function");
				e.Argument = ReadString(v, "argument");
				if (!e.Function.empty()) clip.Events.push_back(std::move(e));
			}
			std::stable_sort(clip.Events.begin(), clip.Events.end(),
				[](const AnimationEvent& a, const AnimationEvent& b) { return a.Time < b.Time; });
		}
		return clip;
	}

	std::shared_ptr<const AnimationClip> AnimationClipLibrary::Get(uint64_t guid) {
		return Cache().Get(guid);
	}

	bool AnimationClipLibrary::LoadFromFile(const std::string& path, AnimationClip& outClip) {
		if (!File::Exists(path)) return false;
		Json::Value root;
		if (!Json::TryParse(File::ReadAllText(path), root) || !root.IsObject()) {
			FNK_CORE_WARN_TAG("Animation", "Could not parse animation clip '{}'", path);
			return false;
		}
		KnownFileStamps::Record(path);
		outClip = AnimationClipFromJson(root);
		return true;
	}

	bool AnimationClipLibrary::SaveToFile(const std::string& path, const AnimationClip& clip) {
		if (!File::WriteAllText(path, Json::Stringify(AnimationClipToJson(clip), true))) {
			FNK_CORE_ERROR_TAG("Animation", "Could not write animation clip '{}'", path);
			return false;
		}
		KnownFileStamps::Record(path);
		return true;
	}

	bool AnimationClipLibrary::Save(uint64_t guid, const AnimationClip& clip) {
		if (guid == 0) return false;
		const std::string path = AssetRegistry::ResolvePath(guid);
		if (path.empty() || !SaveToFile(path, clip)) return false;
		Cache().Store(guid, clip, path);
		return true;
	}

	void AnimationClipLibrary::Set(uint64_t guid, const AnimationClip& clip) {
		if (guid != 0) Cache().Store(guid, clip);
	}

	void AnimationClipLibrary::Invalidate(uint64_t guid) { Cache().Invalidate(guid); }
	size_t AnimationClipLibrary::ReloadChangedFromDisk() { return Cache().ReloadChangedFromDisk(); }
	void AnimationClipLibrary::Clear() { Cache().Clear(); }

}
