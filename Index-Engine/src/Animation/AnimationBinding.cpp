#include "pch.hpp"
#include "Animation/AnimationBinding.hpp"

#include "Animation/AnimationClip.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Inspector/PropertyDescriptor.hpp"
#include "Scene/ComponentInfo.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Serialization/Json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Index {
	namespace {
		bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
			if (a.size() != b.size()) return false;
			for (size_t i = 0; i < a.size(); ++i) {
				if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
			}
			return true;
		}

		int ChannelFromSuffix(std::string_view suffix) {
			if (suffix.size() != 1) return -1;
			switch (std::tolower(static_cast<unsigned char>(suffix[0]))) {
			case 'x': case 'r': return 0;
			case 'y': case 'g': return 1;
			case 'z': case 'b': return 2;
			case 'w': case 'a': return 3;
			default: return -1;
			}
		}

		PropertyType ScriptTagToType(std::string_view tag) {
			if (tag == "float" || tag == "double") return PropertyType::Float;
			if (tag == "int" || tag == "short" || tag == "byte" || tag == "long" || tag == "sbyte") return PropertyType::Int32;
			if (tag == "uint" || tag == "ushort" || tag == "ulong") return PropertyType::UInt32;
			if (tag == "bool") return PropertyType::Bool;
			if (tag == "string" || tag == "enum") return PropertyType::String;
			if (tag == "color") return PropertyType::Color;
			if (tag == "vector2") return PropertyType::Vec2;
			if (tag == "vector3") return PropertyType::Vec3;
			if (tag == "vector4") return PropertyType::Vec4;
			if (tag == "vector2Int") return PropertyType::IntVec2;
			if (tag == "vector3Int") return PropertyType::IntVec3;
			if (tag == "vector4Int") return PropertyType::IntVec4;
			// Asset slots (texture, audio, material, mesh, ...) hold a GUID that only steps.
			if (tag == "texture" || tag == "audio" || tag == "material" || tag == "mesh" || tag == "scene"
				|| tag == "animationclip" || tag == "animatorcontroller" || tag.starts_with("dataasset")) {
				return PropertyType::AssetRef;
			}
			return PropertyType::None;
		}

		const ScriptInstance* FindScript(Scene& scene, EntityHandle entity, const std::string& className) {
			auto& registry = scene.GetRegistry();
			if (!registry.valid(entity)) return nullptr;
			const ScriptComponent* scripts = registry.try_get<ScriptComponent>(entity);
			if (!scripts) return nullptr;
			for (const ScriptInstance& script : scripts->Scripts) {
				if (script.GetClassName() == className && script.HasManagedInstance()) return &script;
			}
			return nullptr;
		}

		// Text of the field as C# formats it (InvariantCulture, "x,y" vectors); empty when the field is not visible.
		bool ReadScriptFieldText(uint32_t handle, const std::string& field, std::string& outType, std::string& outValue) {
			const auto& callbacks = ScriptEngine::GetCallbacks();
			if (!callbacks.GetScriptFields) return false;
			const char* json = callbacks.GetScriptFields(static_cast<int32_t>(handle));
			if (!json) return false;
			Json::Value root;
			if (!Json::TryParse(json, root) || !root.IsArray()) return false;
			for (const Json::Value& entry : root.GetArray()) {
				const Json::Value* name = entry.FindMember("name");
				if (!name || name->AsStringOr() != field) continue;
				const Json::Value* type = entry.FindMember("type");
				const Json::Value* value = entry.FindMember("value");
				outType = type ? type->AsStringOr() : std::string{};
				outValue = value ? value->AsStringOr() : std::string{};
				return true;
			}
			return false;
		}
	}

	AnimationPropertyPath ParseAnimationProperty(std::string_view property) {
		AnimationPropertyPath path;
		const size_t dot = property.rfind('.');
		if (dot != std::string_view::npos) {
			const int channel = ChannelFromSuffix(property.substr(dot + 1));
			if (channel >= 0) {
				path.Base = std::string(property.substr(0, dot));
				path.Channel = channel;
				return path;
			}
		}
		path.Base = std::string(property);
		return path;
	}

	const char* AnimationChannelSuffix(PropertyType type, int channel) {
		static const char* k_Xyzw[] = { "x", "y", "z", "w" };
		static const char* k_Rgba[] = { "r", "g", "b", "a" };
		if (channel < 0 || channel > 3) return "";
		return type == PropertyType::Color ? k_Rgba[channel] : k_Xyzw[channel];
	}

	int AnimationChannelCount(PropertyType type) {
		switch (type) {
		case PropertyType::Vec2: case PropertyType::IntVec2: return 2;
		case PropertyType::Vec3: case PropertyType::IntVec3: return 3;
		case PropertyType::Vec4: case PropertyType::IntVec4: case PropertyType::Color: return 4;
		default: return IsCurveAnimatable(type) ? 1 : 0;
		}
	}

	bool IsCurveAnimatable(PropertyType type) {
		switch (type) {
		case PropertyType::Bool:
		case PropertyType::Int8: case PropertyType::Int16: case PropertyType::Int32: case PropertyType::Int64:
		case PropertyType::UInt8: case PropertyType::UInt16: case PropertyType::UInt32: case PropertyType::UInt64:
		case PropertyType::Float: case PropertyType::Double:
		case PropertyType::Vec2: case PropertyType::Vec3: case PropertyType::Vec4:
		case PropertyType::IntVec2: case PropertyType::IntVec3: case PropertyType::IntVec4:
		case PropertyType::Color:
			return true;
		default:
			return false;
		}
	}

	float ReadAnimationChannel(const PropertyValue& value, int channel) {
		switch (value.Type) {
		case PropertyType::Bool: return value.BoolValue ? 1.0f : 0.0f;
		case PropertyType::Int8: case PropertyType::Int16: case PropertyType::Int32: case PropertyType::Int64:
		case PropertyType::Enum:
			return static_cast<float>(value.IntValue);
		case PropertyType::UInt8: case PropertyType::UInt16: case PropertyType::UInt32: case PropertyType::UInt64:
			return static_cast<float>(value.UIntValue);
		case PropertyType::Float: case PropertyType::Double: return static_cast<float>(value.FloatValue);
		case PropertyType::Vec2: case PropertyType::Vec3: case PropertyType::Vec4: case PropertyType::Color:
			return value.FloatVec[static_cast<size_t>(std::clamp(channel, 0, 3))];
		case PropertyType::IntVec2: case PropertyType::IntVec3: case PropertyType::IntVec4:
			return static_cast<float>(value.IntVec[static_cast<size_t>(std::clamp(channel, 0, 3))]);
		default: return 0.0f;
		}
	}

	void WriteAnimationChannel(PropertyValue& value, int channel, float channelValue) {
		switch (value.Type) {
		case PropertyType::Bool: value.BoolValue = channelValue >= 0.5f; break;
		case PropertyType::Int8: case PropertyType::Int16: case PropertyType::Int32: case PropertyType::Int64:
		case PropertyType::Enum:
			value.IntValue = static_cast<int64_t>(std::lround(channelValue));
			break;
		case PropertyType::UInt8: case PropertyType::UInt16: case PropertyType::UInt32: case PropertyType::UInt64:
			value.UIntValue = static_cast<uint64_t>(std::max<long>(std::lround(channelValue), 0));
			break;
		case PropertyType::Float: case PropertyType::Double: value.FloatValue = channelValue; break;
		case PropertyType::Vec2: case PropertyType::Vec3: case PropertyType::Vec4: case PropertyType::Color:
			value.FloatVec[static_cast<size_t>(std::clamp(channel, 0, 3))] = channelValue;
			break;
		case PropertyType::IntVec2: case PropertyType::IntVec3: case PropertyType::IntVec4:
			value.IntVec[static_cast<size_t>(std::clamp(channel, 0, 3))] = static_cast<int32_t>(std::lround(channelValue));
			break;
		default: break;
		}
	}

	Entity ResolveAnimationPath(Entity root, std::string_view path) {
		Entity current = root;
		while (!path.empty() && current.IsValid()) {
			const size_t slash = path.find('/');
			const std::string_view name = path.substr(0, slash);
			path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
			if (name.empty()) continue;

			Entity next = Entity::Null;
			Scene* scene = current.GetScene();
			for (EntityHandle child : current.GetChildren()) {
				const NameComponent* childName = scene ? scene->GetRegistry().try_get<NameComponent>(child) : nullptr;
				if (childName && childName->Name == name) {
					next = scene->GetEntity(child);
					break;
				}
			}
			current = next;
		}
		return current;
	}

	std::string MakeAnimationPath(Entity root, Entity target) {
		std::vector<std::string> names;
		Entity current = target;
		while (current.IsValid() && !(current == root)) {
			names.push_back(current.GetName());
			current = current.GetParent();
		}
		if (!(current == root)) return {};
		std::string path;
		for (auto it = names.rbegin(); it != names.rend(); ++it) {
			if (!path.empty()) path += '/';
			path += *it;
		}
		return path;
	}

	const PropertyDescriptor* AnimationTargetSet::FindDescriptor(const ComponentInfo& info, std::string_view name) {
		for (const PropertyDescriptor& d : info.properties) {
			if (EqualsIgnoreCase(d.Name, name)) return &d;
		}
		return nullptr;
	}

	int AnimationTargetSet::Resolve(Scene& scene, Entity root, const AnimationTrack& track, int& outChannel, std::string& outError) {
		const AnimationPropertyPath path = ParseAnimationProperty(track.Property);
		outChannel = path.Channel;

		Entity entity = ResolveAnimationPath(root, track.Path);
		if (!entity.IsValid()) {
			outError = "no child entity '" + track.Path + "'";
			return -1;
		}
		const EntityHandle handle = entity.GetHandle();

		for (size_t i = 0; i < m_Targets.size(); ++i) {
			const AnimationTarget& t = m_Targets[i];
			if (t.EntityId == handle && t.Component == track.Component && EqualsIgnoreCase(t.Property, path.Base)) return static_cast<int>(i);
		}

		AnimationTarget target;
		target.EntityId = handle;
		target.Component = track.Component;
		target.Property = path.Base;

		const ComponentInfo* info = SceneManager::GetComponentRegistryOrBuiltIn().FindBySerializedName(track.Component);
		if (info) {
			if (!info->has || !info->has(entity)) {
				outError = "entity has no " + track.Component;
				return -1;
			}
			const PropertyDescriptor* descriptor = FindDescriptor(*info, path.Base);
			if (!descriptor || !descriptor->Get || !descriptor->Set) {
				outError = track.Component + " has no animatable property '" + path.Base + "'";
				return -1;
			}
			target.TargetKind = AnimationTarget::Kind::Property;
			target.Info = info;
			target.Descriptor = descriptor;
			target.IsTransform = track.Component == "Transform2D" || track.Component == "Transform3D" || track.Component == "RectTransform2D";
			target.IsUI = scene.GetRegistry().all_of<RectTransform2DComponent>(handle);
		}
		else {
			const ScriptInstance* script = FindScript(scene, handle, track.Component);
			if (!script) {
				outError = "no component or C# script '" + track.Component + "'";
				return -1;
			}
			std::string tag, text;
			if (!ReadScriptFieldText(script->GetGCHandle(), path.Base, tag, text)) {
				outError = track.Component + " has no visible field '" + path.Base + "'";
				return -1;
			}
			target.TargetKind = AnimationTarget::Kind::ScriptField;
			target.ScriptClass = track.Component;
			target.ScriptHandle = script->GetGCHandle();
			target.ScriptType = ScriptTagToType(tag);
			if (target.ScriptType == PropertyType::None) {
				outError = "field '" + path.Base + "' of type '" + tag + "' cannot be animated";
				return -1;
			}
			target.Cached = PropertyValue::FromString(target.ScriptType, text);
			target.Cached.Type = target.ScriptType;
			target.HasCached = true;
		}

		m_Targets.push_back(std::move(target));
		return static_cast<int>(m_Targets.size()) - 1;
	}

	int AnimationTargetSet::Add(const AnimationTarget& target) {
		m_Targets.push_back(target);
		return static_cast<int>(m_Targets.size()) - 1;
	}

	PropertyType AnimationTargetSet::GetType(int target) const {
		if (target < 0 || target >= static_cast<int>(m_Targets.size())) return PropertyType::None;
		const AnimationTarget& t = m_Targets[static_cast<size_t>(target)];
		return t.TargetKind == AnimationTarget::Kind::Property ? t.Descriptor->Type : t.ScriptType;
	}

	bool AnimationTargetSet::Read(Scene& scene, int target, PropertyValue& outValue) {
		if (target < 0 || target >= static_cast<int>(m_Targets.size())) return false;
		AnimationTarget& t = m_Targets[static_cast<size_t>(target)];
		if (!scene.GetRegistry().valid(t.EntityId)) return false;
		if (t.TargetKind == AnimationTarget::Kind::ScriptField) {
			outValue = t.Cached;
			return t.HasCached;
		}
		Entity entity = scene.GetEntity(t.EntityId);
		if (!t.Info->has(entity)) return false;
		outValue = t.Descriptor->Get(entity);
		return true;
	}

	bool AnimationTargetSet::Write(Scene& scene, int target, const PropertyValue& value) {
		if (target < 0 || target >= static_cast<int>(m_Targets.size())) return false;
		AnimationTarget& t = m_Targets[static_cast<size_t>(target)];
		auto& registry = scene.GetRegistry();
		if (!registry.valid(t.EntityId)) return false;

		if (t.TargetKind == AnimationTarget::Kind::ScriptField) {
			const ScriptInstance* script = FindScript(scene, t.EntityId, t.ScriptClass);
			if (!script || script->GetGCHandle() != t.ScriptHandle) return false;
			if (t.HasCached && t.Cached == value) return true;
			const auto& callbacks = ScriptEngine::GetCallbacks();
			if (!callbacks.SetScriptField) return false;
			callbacks.SetScriptField(static_cast<int32_t>(t.ScriptHandle), t.Property.c_str(), value.ToString().c_str());
			t.Cached = value;
			t.HasCached = true;
			return true;
		}

		Entity entity = scene.GetEntity(t.EntityId);
		if (!t.Info->has(entity)) return false;
		t.Descriptor->Set(entity, value);
		if (t.IsTransform) scene.MarkTransformDirty(t.EntityId);
		if (t.IsUI) scene.MarkUIDirty();
		return true;
	}

}
