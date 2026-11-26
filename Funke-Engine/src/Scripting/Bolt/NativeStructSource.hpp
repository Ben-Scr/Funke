#pragma once
#include "Scripting/Bolt/ComponentSource.hpp"
#include "Scripting/StructFieldBytes.hpp"
#include "Scene/SceneManager.hpp"

namespace Funke::BoltScriptHost {
    struct NativeStructLayout {
        std::string Name;
        uint32_t Size = 0, Alignment = 1;
        Json::Value Fields = Json::Value::MakeArray();
        std::map<std::string, uint32_t> Offsets;
    };

    inline void CollectNativeLayout(void* context, uint32_t size, uint32_t alignment, const BoltLayoutField* fields,
                                    size_t count) {
        auto& layout = *static_cast<NativeStructLayout*>(context);
        layout.Size = size;
        layout.Alignment = alignment;
        for (size_t i = 0; i < count; i++) {
            const auto& field = fields[i];
            std::string tag = field.type;
            if (tag.starts_with("Funke."))
                tag.erase(0, 6);
            if (tag == "Vector2" || tag == "Vector3" || tag == "Vector4" || tag == "Color")
                tag.front() = static_cast<char>(std::tolower(tag.front()));
            auto item = Json::Value::MakeObject();
            item.AddMember("name", std::string(field.name));
            item.AddMember("type", tag);
            item.AddMember("offset", static_cast<uint64_t>(field.offset));
            item.AddMember("size", static_cast<uint64_t>(field.size));
            item.AddMember("readOnly", !field.is_public);
            layout.Fields.Append(std::move(item));
            layout.Offsets.emplace(field.name, field.offset);
        }
    }

    inline bool ReadNativeStruct(const ComponentUnit& unit, decltype(&bolt_module_visit_layout) visit,
                                 const BoltCompiledModule* module, std::vector<NativeStructLayout>& layouts,
                                 std::string& error, bool includeManaged = false) {
        NativeStructLayout layout{unit.Name};
        char diagnostic[2048]{};
        if (!visit || visit(module, unit.Name.c_str(), CollectNativeLayout, &layout, diagnostic, sizeof(diagnostic)) !=
                          BOLT_HOST_OK) {
            error = unit.Path + ": " + (visit ? diagnostic : "Bolt compiler does not provide native struct layouts");
            return false;
        }
        if (unit.Managed) {
            if (unit.ManagedSize != layout.Size) {
                error = "C# and Bolt struct sizes differ: " + unit.Name;
                return false;
            }
            for (const auto& [field, offset] : unit.ManagedOffsets) {
                const auto it = layout.Offsets.find(field);
                if (it == layout.Offsets.end() || it->second != offset) {
                    error = "C# and Bolt field offsets differ: " + unit.Name + "." + field;
                    return false;
                }
            }
        }
        if (!unit.Managed || includeManaged)
            layouts.push_back(std::move(layout));
        return true;
    }

    inline bool InstallNativeStructs(const std::vector<NativeStructLayout>& layouts, std::string& error) {
        static std::unordered_map<std::string, std::string> schemas;
        auto& registry = SceneManager::GetComponentRegistry();
        for (const auto& layout : layouts) {
            const auto key = "bolt:" + layout.Name;
            if (const auto* existing = registry.FindBySerializedName(key);
                existing && (existing->rawSize != layout.Size || schemas[key] != Json::Stringify(layout.Fields))) {
                error = "ECS layout changed while storage is registered: " + layout.Name;
                return false;
            }
        }
        for (const auto& layout : layouts) {
            const auto key = "bolt:" + layout.Name;
            if (registry.FindBySerializedName(key))
                continue;
            const auto fields = Json::Stringify(layout.Fields);
            const auto id = registry.RegisterDynamic(layout.Name, key, "Native Components (Bolt)",
                                                     ComponentCategory::Component, layout.Size, layout.Alignment);
            if (!id) {
                error = "Could not register ECS component " + layout.Name;
                return false;
            }
            registry.SetDynamicProperties(id, BuildDynamicFieldProperties(id, layout.Size, fields));
            schemas[key] = fields;
        }
        return true;
    }
} // namespace Funke::BoltScriptHost
