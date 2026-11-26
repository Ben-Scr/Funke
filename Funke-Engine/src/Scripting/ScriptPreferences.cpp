#include "pch.hpp"
#include "Scripting/ScriptPreferences.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>

namespace Funke::ScriptPreferences {
    namespace {
        struct Store {
            Json::Value Values = Json::Value::MakeObject();
            bool Dirty = false;
        };
        std::mutex s_Mutex;
        std::map<std::string, Store> s_Stores;

        Store& Open(const std::string& path) {
            auto [it, added] = s_Stores.try_emplace(path);
            Store& store = it->second;
            if (!added || !File::Exists(path)) return store;
            Json::Value root;
            if (Json::TryParse(File::ReadAllText(path), root)) {
                if (auto* values = root.FindMember("values"); values && values->IsObject()) {
                    store.Values = std::move(*values);
                    return store;
                }
            }
            const auto backup = path + ".corrupt-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
            std::error_code error;
            std::filesystem::rename(Path::FromUtf8(path), Path::FromUtf8(backup), error);
            if (error) {
                s_Stores.erase(it);
                throw std::runtime_error("Cannot preserve unreadable player preferences");
            }
            FNK_CORE_WARN_TAG("PlayerPrefs", "Unreadable preferences moved to {}", backup);
            return store;
        }

        bool Flush(const std::string& path, Store& store) {
            if (!store.Dirty) return true;
            auto root = Json::Value::MakeObject();
            root.AddMember("version", 1);
            root.AddMember("values", store.Values);
            if (!File::WriteAllText(path, Json::Stringify(root, true))) return false;
            store.Dirty = false;
            return true;
        }
    }

    int Access(const char* path, int op, const char* key, int keyBytes, uint64_t* value, char* text, int capacity) noexcept {
        if (!path || keyBytes < 0 || capacity < 0 || (keyBytes && !key)) return -2;
        try {
            std::lock_guard lock(s_Mutex);
            const auto filename = Path::ToUtf8(Path::FromUtf8(path).lexically_normal().make_preferred());
            if (op == Save && !s_Stores.contains(filename)) return 0;
            auto& store = Open(filename);
            if (op == Save) return Flush(filename, store) ? 0 : -2;
            if (op == DeleteAll) { store.Values = Json::Value::MakeObject(); store.Dirty = true; return 0; }
            if (!keyBytes) return -2;
            const std::string name(key, keyBytes);
            auto* entry = store.Values.FindMember(name);
            if (op == HasKey) return entry ? 1 : 0;
            if (op == DeleteKey) {
                if (entry) {
                    std::erase_if(store.Values.GetMembers(), [&](const auto& item) { return item.first == name; });
                    store.Dirty = true;
                }
                return 0;
            }
            if (!value || op < GetInt || op > SetString) return -2;
            static constexpr const char* kinds[] = {"int", "float", "string"};
            const char* kind = kinds[op % 3];
            if (op <= GetString) {
                const auto* saved = entry ? entry->FindMember(kind) : nullptr;
                if (!saved) return -1;
                if (op == GetString) {
                    if (!saved->IsString()) return -1;
                    const auto& str = saved->AsStringOr();
                    if (str.size() > INT_MAX) return -2;
                    if (text && capacity >= static_cast<int>(str.size())) std::memcpy(text, str.data(), str.size());
                    return static_cast<int>(str.size());
                }
                if (!saved->IsNumber()) return -1;
                *value = op == GetInt ? static_cast<uint64_t>(saved->AsInt64Or(0)) : std::bit_cast<uint64_t>(saved->AsDoubleOr(0.0));
                return 0;
            }
            if (op == SetString && capacity && !text) return -2;
            if (op == SetFloat && !std::isfinite(std::bit_cast<double>(*value))) return -2;
            auto typed = Json::Value::MakeObject();
            if (op == SetInt) typed.AddMember(kind, std::bit_cast<int64_t>(*value));
            if (op == SetFloat) typed.AddMember(kind, std::bit_cast<double>(*value));
            if (op == SetString) typed.AddMember(kind, std::string(text ? text : "", capacity));
            store.Values.AddMember(name, std::move(typed));
            store.Dirty = true;
            return 0;
        } catch (...) { return -2; }
    }

    void SavePending() noexcept {
        try {
            std::lock_guard lock(s_Mutex);
            for (auto& [path, store] : s_Stores)
                if (!Flush(path, store)) FNK_CORE_ERROR_TAG("PlayerPrefs", "Could not save {}", path);
        } catch (...) { FNK_CORE_ERROR_TAG("PlayerPrefs", "Could not save pending preferences"); }
    }
}

