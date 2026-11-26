#pragma once
#include "Serialization/Json.hpp"
#include <charconv>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace Funke {
    // Bound to a model GUID so selecting another model cannot apply unrelated material slots.
    // Unique glTF names survive material-array reordering; unnamed/duplicate names use an index key.
    struct ModelMaterialOverrides {
        uint64_t Model = 0;
        std::map<std::string, uint64_t, std::less<>> Slots;

        uint64_t Get(uint64_t model, std::string_view key) const {
            if (model == 0 || Model != model) return 0;
            const auto found = Slots.find(key);
            return found == Slots.end() ? 0 : found->second;
        }
        void Set(uint64_t model, std::string_view key, uint64_t material) {
            if (model == 0 || key.empty()) return;
            if (Model != model) { Slots.clear(); Model = model; }
            if (material) Slots[std::string(key)] = material;
            else if (auto found = Slots.find(key); found != Slots.end()) Slots.erase(found);
            if (Slots.empty()) Model = 0;
        }
        void Serialize(Json::Value& owner) const {
            if (Model == 0 || Slots.empty()) return;
            auto data = Json::Value::MakeObject();
            data.AddMember("model", std::to_string(Model));
            auto slots = Json::Value::MakeArray();
            for (const auto& [key, guid] : Slots) {
                auto item = Json::Value::MakeObject();
                item.AddMember("slot", key);
                item.AddMember("material", std::to_string(guid));
                slots.Append(std::move(item));
            }
            data.AddMember("slots", std::move(slots));
            owner.AddMember("materialOverrides", std::move(data));
        }
        void Deserialize(const Json::Value& owner, uint64_t model) {
            Model = 0; Slots.clear();
            const auto* data = owner.FindMember("materialOverrides");
            if (!data || !data->IsObject() || model == 0) return;
            const auto guid = [](const Json::Value* v) -> uint64_t {
                if (!v) return 0;
                if (v->IsNumber()) return v->AsUInt64Or(0);
                const auto text = v->AsStringOr(); uint64_t result = 0;
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
                return parsed.ec == std::errc() && parsed.ptr == text.data() + text.size() ? result : 0;
            };
            if (guid(data->FindMember("model")) != model) return;
            const auto* slots = data->FindMember("slots");
            if (!slots || !slots->IsArray()) return;
            for (const auto& item : slots->GetArray()) {
                if (!item.IsObject()) continue;
                const auto* key = item.FindMember("slot");
                const uint64_t material = guid(item.FindMember("material"));
                if (key && key->IsString() && material) Set(model, key->AsStringOr(), material);
            }
        }
        bool operator==(const ModelMaterialOverrides&) const = default;
    };
}
