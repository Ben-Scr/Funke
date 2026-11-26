#pragma once

#include "Scripting/Bolt/CompilerHost.h"
#include "Scripting/Bolt/DataAssetEnums.hpp"
#include "Scripting/BoltScriptHost.hpp"
#include <algorithm>
#include <charconv>
#include <string_view>

namespace Funke::BoltScriptHost {
	using ReferenceNames = std::unordered_map<std::string, std::string>;
	inline ReferenceNames BuiltInReferences() {
		return {
#include "Scripting/Bolt/ComponentReferenceNames.inc"
		};
	}
	inline const ReferenceNames& BuiltInFieldReferences() {
		static const auto names = BuiltInReferences();
		return names;
	}
	struct FieldList {
		std::string Owner;
		std::vector<Field> Fields;
		ReferenceNames References = BuiltInReferences();
        DataEnums Enums;
	};

	inline std::string_view FieldChannels(std::string_view type) {
		if (type == "Vector2") return "XY";
		if (type == "Vector3") return "XYZ";
		if (type == "Vector4") return "XYZW";
		if (type == "Color") return "RGBA";
		return {};
	}

    inline std::string AssetFieldTag(std::string_view type, const ReferenceNames& references) {
        if (type.starts_with("Funke.")) type.remove_prefix(6);
        static const ReferenceNames tags = {{"Texture","texture"},{"Audio","audio"},{"Font","font"},
            {"Material","material"},{"Mesh","mesh"},{"AnimationClip","animationclip"},{"AnimatorController","animatorcontroller"}};
        if (auto it = tags.find(std::string(type)); it != tags.end()) return it->second;
        const auto found = references.find(std::string(type));
        return found != references.end() && found->second.starts_with("dataasset:") ? found->second : std::string{};
    }

	inline void CollectField(void* context, const BoltFieldInfo* info) {
		auto& list = *static_cast<FieldList*>(context);
		if (list.Owner != info->owner || info->is_static) return;
		std::string_view type(info->type);
		const bool optional = type.ends_with("?");
		if (optional) type.remove_suffix(1);
		const bool funkeQualified = type.starts_with("Funke.");
		if (funkeQualified) type.remove_prefix(6);
		static constexpr std::string_view supported[] = {
			"string", "bool", "byte", "short", "ushort", "int", "uint", "long", "ulong", "float", "double"
		};
		auto shortType = type.substr(type.rfind('.') == type.npos ? 0 : type.rfind('.') + 1);
		const auto references = funkeQualified ? &BuiltInFieldReferences() : &list.References;
		auto reference = references->find(std::string(type));
		if (reference == references->end() && type.starts_with("UI."))
			reference = references->find(std::string(shortType));
		const bool isEntity = type == "Entity";
        const auto assetTag = AssetFieldTag(type, list.References);
		const auto* enumType = DataEnumFor(list.Enums, list.Owner, info->type);
		if (!enumType && assetTag.empty() && !isEntity && reference == references->end() && std::find(std::begin(supported), std::end(supported), type) == std::end(supported) && FieldChannels(type).empty()) return;
		bool visible = info->is_public;
		Field field{info->name, std::string(type), info->name, info->literal, {}, info->is_constant != 0};
		for (size_t i = 0; i < info->attribute_count; ++i) {
			const auto& attr = info->attributes[i];
			std::string_view name(attr.name);
			if (auto dot = name.rfind('.'); dot != name.npos) name.remove_prefix(dot + 1);
			if (name.ends_with("Attribute")) name.remove_suffix(9);
			if (name == "HideFromEditor" || name == "EditorIgnore") return;
			if (name == "ShowInEditor") {
				visible = true;
				if (attr.argument_count && *attr.arguments[0]) field.DisplayName = attr.arguments[0];
				if (attr.argument_count > 1 && std::string_view(attr.arguments[1]) == "true") field.ReadOnly = true;
			}
			if (name == "EditorReadOnly") field.ReadOnly = true;
			if ((name == "Tooltip" || name == "ToolTip") && attr.argument_count) field.Tooltip = attr.arguments[0];
            const auto number = [&](size_t index, float fallback) {
                if (index >= attr.argument_count) return fallback;
                std::string_view text(attr.arguments[index]);
                float result{};
                return std::from_chars(text.data(), text.data()+text.size(), result).ec == std::errc{} ? result : fallback;
            };
            if (name == "Header") {
                field.HeaderContent = attr.argument_count ? attr.arguments[0] : "";
                field.HeaderSize = static_cast<int>(number(1, 5));
            } else if (name == "Space") {
                field.HasSpace = true;
                field.SpaceHeight = number(0, 8);
            } else if (name == "ClampValue" && attr.argument_count >= 2) {
                field.HasClamp = true;
                field.ClampMin = number(0, 0);
                field.ClampMax = number(1, 0);
            } else if (name == "EnabledIf" && attr.argument_count >= 1) {
                field.HasEnabledIf = true;
                field.EnabledIfField = attr.arguments[0];
                field.EnabledIfAny = attr.argument_count == 1;
                if (!field.EnabledIfAny) field.EnabledIfValue = attr.arguments[1];
            }
		}
		if (!visible) return;
		if (enumType) {
            field.Type = enumType->Flags ? "flagenum" : "enum";
            field.Value = "0";
        } else if (!assetTag.empty() || isEntity || reference != references->end()) {
			field.ReferenceType = std::string(info->type);
			if (optional) field.ReferenceType.pop_back();
			field.Optional = optional;
			field.Type = !assetTag.empty() ? assetTag : isEntity ? "entity" : "component:" + reference->second;
			field.Value.clear();
		} else if (FieldChannels(type).empty() && type != "string" && type != "bool") {
			while (!field.Value.empty() && std::string_view("fFuUlLdD").find(field.Value.back()) != std::string_view::npos)
				field.Value.pop_back();
			if (field.Value.empty()) field.Value = "0";
		}
		if (type == "bool" && field.Value.empty()) field.Value = "false";
		list.Fields.push_back(std::move(field));
	}

	inline std::string FieldAccessors(const std::vector<Field>& fields) {
		if (fields.empty()) return {};
		std::string write = " public override void FkReadFields() {";
		std::string read = " public override void FkApplyFields() {";
		for (size_t i = 0; i < fields.size(); ++i) {
			const auto& f = fields[i];
			const std::string index = std::to_string(i) + "ul";
			const std::string member = "this." + f.Name;
            if (!f.ReferenceType.empty() && f.Type != "entity" && !f.Type.starts_with("component:")) {
                const auto value = f.Optional ? member + ".Value" : member;
                const std::string id = value + (f.Type.starts_with("dataasset:") ? ".FkAssetId()" : ".UUID");
                write += "Funke.FunkeInspector.Write(" + index + "," + (f.Optional ? "(" + member + "==null ? \"0\" : (string)" + id + ")" : "(string)" + id) + ");";
                if (!f.ReadOnly) {
                    read += "{string? raw = Funke.FunkeInspector.Read("+index+"); if (raw != null) {ulong id = 0ul; ulong.TryParse(raw.Value,id); " + f.ReferenceType +
                        "? asset = Funke.AssetManager.LoadByGUID<"+f.ReferenceType+">(id);";
                    read += f.Optional ? member + "=asset;" : "if(asset!=null) "+member+"=asset.Value; else "+member+".FkSetGuid(0ul);";
                    read += "}}";
                }
                continue;
            }
			if (!f.ReferenceType.empty()) {
				const auto token = f.Type == "entity" ? std::string{} : f.Type.substr(10);
				const auto value = f.Optional ? member + ".Value" : member;
				std::string formatted = "(string)" + value + ".ID";
				if (!token.empty()) formatted = "(string)" + value + ".ID + \":" + token + "\"";
				if (f.Optional) formatted = member + " == null ? \"\" : " + formatted;
				write += "Funke.FunkeInspector.Write(" + index + ", " + formatted + ");";
				if (!f.ReadOnly) {
					read += "{string? raw = Funke.FunkeInspector.Read(" + index + "); if (raw != null) {";
					read += "ulong refId = Funke.FunkeInspector.ReferenceId(raw.Value, \"" + token + "\");";
					std::string result = token.empty() ? "Funke.Entity.FromId(refId)" : "Funke.Entity(refId).GetComponent<" + f.ReferenceType + ">()";
					if (f.Optional) read += member + " = " + result + ";";
					else read += f.ReferenceType + "? target = " + result + "; if (target != null) " + member + " = target.Value; else " + member + ".ID = 0ul;";
					read += "}}";
				}
				continue;
			}
			const auto channels = FieldChannels(f.Type);
			std::string value;
			for (char channel : channels) {
				if (!value.empty()) value += " + \",\" + ";
				value += "(string)" + member + "." + channel;
			}
			if (value.empty()) value = "(string)" + member;
			write += "Funke.FunkeInspector.Write(" + index + ", " + value + ");";
			if (f.ReadOnly) continue;
			read += "{string? raw = Funke.FunkeInspector.Read(" + index + "); if (raw != null) {";
			if (!channels.empty()) {
				read += "string[] parts = raw.Value.Split(','); if (parts.Length == " + std::to_string(channels.size()) + ") {";
				for (char channel : channels) read += std::string("float v") + channel + ";";
				std::string condition, values;
				for (size_t n = 0; n < channels.size(); ++n) {
					if (n) { condition += " && "; values += ", "; }
					const std::string v = std::string("v") + channels[n];
					condition += "float.TryParse(parts[" + std::to_string(n) + "], " + v + ")";
					values += v;
				}
				read += "if (" + condition + ") " + member + " = Funke." + f.Type + "(" + values + ");}";
			}
			else if (f.Type == "string") read += member + " = raw.Value;";
			else if (f.Type == "bool") read += member + " = raw.Value == \"true\" || raw.Value == \"True\" || raw.Value == \"1\";";
			else read += f.Type + " parsed; if (" + f.Type + ".TryParse(raw.Value, parsed)) " + member + " = parsed;";
			read += "}}";
		}
		// Preserve the original line numbers in compiler diagnostics.
		return write + "} " + read + "} ";
	}
}

