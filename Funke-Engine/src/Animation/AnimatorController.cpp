#include "pch.hpp"
#include "Animation/AnimatorController.hpp"

#include "Animation/JsonAssetCache.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Serialization/File.hpp"
#include "Serialization/FileStamp.hpp"

#include <algorithm>
#include <charconv>

namespace Funke {
	namespace {
		float ReadFloat(const Json::Value& object, std::string_view key, float fallback) {
			const Json::Value* v = object.FindMember(key);
			return v ? static_cast<float>(v->AsDoubleOr(fallback)) : fallback;
		}

		std::string ReadString(const Json::Value& object, std::string_view key) {
			const Json::Value* v = object.FindMember(key);
			return v ? v->AsStringOr() : std::string{};
		}

		uint64_t ReadGuid(const Json::Value* v) {
			if (!v) return 0;
			if (v->IsNumber()) return v->AsUInt64Or(0);
			const std::string text = v->AsStringOr();
			uint64_t guid = 0;
			std::from_chars(text.data(), text.data() + text.size(), guid);
			return guid;
		}

		Json::Value Vec2ToJson(const glm::vec2& value) {
			Json::Value v = Json::Value::MakeArray();
			v.Append(Json::Value(value.x));
			v.Append(Json::Value(value.y));
			return v;
		}

		glm::vec2 Vec2FromJson(const Json::Value* v, const glm::vec2& fallback) {
			if (!v || !v->IsArray() || v->GetArray().size() < 2) return fallback;
			return { static_cast<float>(v->GetArray()[0].AsDoubleOr(fallback.x)), static_cast<float>(v->GetArray()[1].AsDoubleOr(fallback.y)) };
		}

		AnimatorParameterType ParameterTypeFromString(const std::string& name) {
			if (name == "int") return AnimatorParameterType::Int;
			if (name == "bool") return AnimatorParameterType::Bool;
			if (name == "trigger") return AnimatorParameterType::Trigger;
			return AnimatorParameterType::Float;
		}

		AnimatorConditionMode ConditionModeFromString(const std::string& name) {
			if (name == "greater") return AnimatorConditionMode::Greater;
			if (name == "less") return AnimatorConditionMode::Less;
			if (name == "equals") return AnimatorConditionMode::Equals;
			if (name == "notEquals") return AnimatorConditionMode::NotEquals;
			if (name == "ifNot") return AnimatorConditionMode::IfNot;
			return AnimatorConditionMode::If;
		}

		const char* StateLoopToString(AnimatorStateLoop loop) {
			switch (loop) {
			case AnimatorStateLoop::Loop: return "loop";
			case AnimatorStateLoop::Once: return "once";
			case AnimatorStateLoop::FromClip:
			default: return "clip";
			}
		}

		AnimatorStateLoop StateLoopFromString(const std::string& name) {
			if (name == "loop") return AnimatorStateLoop::Loop;
			if (name == "once") return AnimatorStateLoop::Once;
			return AnimatorStateLoop::FromClip;
		}

		JsonAssetCache<AnimatorController>& Cache() {
			static JsonAssetCache<AnimatorController> s_Cache(&AnimatorControllerLibrary::LoadFromFile);
			return s_Cache;
		}
	}

	int AnimatorController::FindState(const std::string& name) const {
		for (size_t i = 0; i < States.size(); ++i) {
			if (States[i].Name == name) return static_cast<int>(i);
		}
		return -1;
	}

	int AnimatorController::FindParameter(const std::string& name) const {
		for (size_t i = 0; i < Parameters.size(); ++i) {
			if (Parameters[i].Name == name) return static_cast<int>(i);
		}
		return -1;
	}

	void AnimatorController::RemoveState(int index) {
		if (index < 0 || index >= static_cast<int>(States.size())) return;
		States.erase(States.begin() + index);
		std::erase_if(Transitions, [index](const AnimatorTransition& t) { return t.From == index || t.To == index; });
		for (AnimatorTransition& t : Transitions) {
			if (t.From > index) --t.From;
			if (t.To > index) --t.To;
		}
		if (DefaultState == index) DefaultState = 0;
		else if (DefaultState > index) --DefaultState;
	}

	const char* AnimatorParameterTypeToString(AnimatorParameterType type) {
		switch (type) {
		case AnimatorParameterType::Int: return "int";
		case AnimatorParameterType::Bool: return "bool";
		case AnimatorParameterType::Trigger: return "trigger";
		case AnimatorParameterType::Float:
		default: return "float";
		}
	}

	const char* AnimatorConditionModeToString(AnimatorConditionMode mode) {
		switch (mode) {
		case AnimatorConditionMode::Greater: return "greater";
		case AnimatorConditionMode::Less: return "less";
		case AnimatorConditionMode::Equals: return "equals";
		case AnimatorConditionMode::NotEquals: return "notEquals";
		case AnimatorConditionMode::IfNot: return "ifNot";
		case AnimatorConditionMode::If:
		default: return "if";
		}
	}

	Json::Value AnimatorControllerToJson(const AnimatorController& controller) {
		Json::Value root = Json::Value::MakeObject();
		root.AddMember("type", Json::Value("AnimatorController"));
		root.AddMember("version", Json::Value(k_AnimatorControllerFormatVersion));

		Json::Value parameters = Json::Value::MakeArray();
		for (const AnimatorParameter& p : controller.Parameters) {
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("name", Json::Value(p.Name));
			v.AddMember("type", Json::Value(AnimatorParameterTypeToString(p.Type)));
			switch (p.Type) {
			case AnimatorParameterType::Float: v.AddMember("default", Json::Value(p.DefaultFloat)); break;
			case AnimatorParameterType::Int: v.AddMember("default", Json::Value(p.DefaultInt)); break;
			case AnimatorParameterType::Bool: v.AddMember("default", Json::Value(p.DefaultBool)); break;
			case AnimatorParameterType::Trigger: break;
			}
			parameters.Append(std::move(v));
		}
		root.AddMember("parameters", std::move(parameters));

		Json::Value states = Json::Value::MakeArray();
		for (const AnimatorState& s : controller.States) {
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("name", Json::Value(s.Name));
			v.AddMember("clip", Json::Value(std::to_string(s.Clip)));
			v.AddMember("speed", Json::Value(s.Speed));
			if (s.Loop != AnimatorStateLoop::FromClip) v.AddMember("loop", Json::Value(StateLoopToString(s.Loop)));
			v.AddMember("position", Vec2ToJson(s.Position));
			states.Append(std::move(v));
		}
		root.AddMember("states", std::move(states));

		const bool hasDefault = controller.DefaultState >= 0 && controller.DefaultState < static_cast<int>(controller.States.size());
		root.AddMember("defaultState", Json::Value(hasDefault ? controller.States[static_cast<size_t>(controller.DefaultState)].Name : std::string{}));
		root.AddMember("entryPosition", Vec2ToJson(controller.EntryPosition));
		root.AddMember("anyStatePosition", Vec2ToJson(controller.AnyStatePosition));

		auto stateName = [&controller](int index) -> std::string {
			return index >= 0 && index < static_cast<int>(controller.States.size()) ? controller.States[static_cast<size_t>(index)].Name : std::string{};
		};
		Json::Value transitions = Json::Value::MakeArray();
		for (const AnimatorTransition& t : controller.Transitions) {
			Json::Value v = Json::Value::MakeObject();
			if (t.From == AnimatorTransition::k_AnyState) v.AddMember("fromAny", Json::Value(true));
			else v.AddMember("from", Json::Value(stateName(t.From)));
			v.AddMember("to", Json::Value(stateName(t.To)));
			Json::Value conditions = Json::Value::MakeArray();
			for (const AnimatorCondition& c : t.Conditions) {
				Json::Value cv = Json::Value::MakeObject();
				cv.AddMember("parameter", Json::Value(c.Parameter));
				cv.AddMember("mode", Json::Value(AnimatorConditionModeToString(c.Mode)));
				if (c.Mode != AnimatorConditionMode::If && c.Mode != AnimatorConditionMode::IfNot) cv.AddMember("threshold", Json::Value(c.Threshold));
				conditions.Append(std::move(cv));
			}
			v.AddMember("conditions", std::move(conditions));
			v.AddMember("hasExitTime", Json::Value(t.HasExitTime));
			v.AddMember("exitTime", Json::Value(t.ExitTime));
			v.AddMember("duration", Json::Value(t.Duration));
			if (t.CanTransitionToSelf) v.AddMember("canTransitionToSelf", Json::Value(true));
			transitions.Append(std::move(v));
		}
		root.AddMember("transitions", std::move(transitions));
		return root;
	}

	AnimatorController AnimatorControllerFromJson(const Json::Value& value) {
		const AnimatorController d{};
		AnimatorController controller;
		if (!value.IsObject()) return controller;

		if (const Json::Value* parameters = value.FindMember("parameters"); parameters && parameters->IsArray()) {
			for (const Json::Value& v : parameters->GetArray()) {
				if (!v.IsObject()) continue;
				AnimatorParameter p;
				p.Name = ReadString(v, "name");
				if (p.Name.empty() || controller.FindParameter(p.Name) >= 0) continue;
				p.Type = ParameterTypeFromString(ReadString(v, "type"));
				if (const Json::Value* def = v.FindMember("default")) {
					p.DefaultFloat = static_cast<float>(def->AsDoubleOr(0.0));
					p.DefaultInt = def->AsIntOr(0);
					p.DefaultBool = def->IsBool() ? def->AsBoolOr(false) : def->AsDoubleOr(0.0) != 0.0;
				}
				controller.Parameters.push_back(std::move(p));
			}
		}

		if (const Json::Value* states = value.FindMember("states"); states && states->IsArray()) {
			for (const Json::Value& v : states->GetArray()) {
				if (!v.IsObject()) continue;
				AnimatorState s;
				s.Name = ReadString(v, "name");
				if (s.Name.empty() || controller.FindState(s.Name) >= 0) continue;
				s.Clip = ReadGuid(v.FindMember("clip"));
				s.Speed = ReadFloat(v, "speed", 1.0f);
				s.Loop = StateLoopFromString(ReadString(v, "loop"));
				s.Position = Vec2FromJson(v.FindMember("position"), glm::vec2(static_cast<float>(controller.States.size()) * 220.0f, 0.0f));
				controller.States.push_back(std::move(s));
			}
		}

		const int defaultState = controller.FindState(ReadString(value, "defaultState"));
		controller.DefaultState = defaultState >= 0 ? defaultState : 0;
		controller.EntryPosition = Vec2FromJson(value.FindMember("entryPosition"), d.EntryPosition);
		controller.AnyStatePosition = Vec2FromJson(value.FindMember("anyStatePosition"), d.AnyStatePosition);

		if (const Json::Value* transitions = value.FindMember("transitions"); transitions && transitions->IsArray()) {
			for (const Json::Value& v : transitions->GetArray()) {
				if (!v.IsObject()) continue;
				AnimatorTransition t;
				const Json::Value* fromAny = v.FindMember("fromAny");
				if (fromAny && fromAny->AsBoolOr(false)) {
					t.From = AnimatorTransition::k_AnyState;
				}
				else {
					t.From = controller.FindState(ReadString(v, "from"));
					if (t.From < 0) continue;
				}
				t.To = controller.FindState(ReadString(v, "to"));
				if (t.To < 0) continue;
				if (const Json::Value* conditions = v.FindMember("conditions"); conditions && conditions->IsArray()) {
					for (const Json::Value& cv : conditions->GetArray()) {
						if (!cv.IsObject()) continue;
						AnimatorCondition c;
						c.Parameter = ReadString(cv, "parameter");
						if (c.Parameter.empty()) continue;
						c.Mode = ConditionModeFromString(ReadString(cv, "mode"));
						c.Threshold = ReadFloat(cv, "threshold", 0.0f);
						t.Conditions.push_back(std::move(c));
					}
				}
				const Json::Value* hasExitTime = v.FindMember("hasExitTime");
				t.HasExitTime = hasExitTime ? hasExitTime->AsBoolOr(false) : false;
				t.ExitTime = std::max(ReadFloat(v, "exitTime", 1.0f), 0.0f);
				t.Duration = std::max(ReadFloat(v, "duration", 0.25f), 0.0f);
				const Json::Value* toSelf = v.FindMember("canTransitionToSelf");
				t.CanTransitionToSelf = toSelf ? toSelf->AsBoolOr(false) : false;
				controller.Transitions.push_back(std::move(t));
			}
		}
		return controller;
	}

	std::shared_ptr<const AnimatorController> AnimatorControllerLibrary::Get(uint64_t guid) {
		return Cache().Get(guid);
	}

	bool AnimatorControllerLibrary::LoadFromFile(const std::string& path, AnimatorController& outController) {
		if (!File::Exists(path)) return false;
		Json::Value root;
		if (!Json::TryParse(File::ReadAllText(path), root) || !root.IsObject()) {
			FNK_CORE_WARN_TAG("Animation", "Could not parse animator controller '{}'", path);
			return false;
		}
		KnownFileStamps::Record(path);
		outController = AnimatorControllerFromJson(root);
		return true;
	}

	bool AnimatorControllerLibrary::SaveToFile(const std::string& path, const AnimatorController& controller) {
		if (!File::WriteAllText(path, Json::Stringify(AnimatorControllerToJson(controller), true))) {
			FNK_CORE_ERROR_TAG("Animation", "Could not write animator controller '{}'", path);
			return false;
		}
		KnownFileStamps::Record(path);
		return true;
	}

	bool AnimatorControllerLibrary::Save(uint64_t guid, const AnimatorController& controller) {
		if (guid == 0) return false;
		const std::string path = AssetRegistry::ResolvePath(guid);
		if (path.empty() || !SaveToFile(path, controller)) return false;
		Cache().Store(guid, controller, path);
		return true;
	}

	void AnimatorControllerLibrary::Set(uint64_t guid, const AnimatorController& controller) {
		if (guid != 0) Cache().Store(guid, controller);
	}

	void AnimatorControllerLibrary::Invalidate(uint64_t guid) { Cache().Invalidate(guid); }
	size_t AnimatorControllerLibrary::ReloadChangedFromDisk() { return Cache().ReloadChangedFromDisk(); }
	void AnimatorControllerLibrary::Clear() { Cache().Clear(); }

}
