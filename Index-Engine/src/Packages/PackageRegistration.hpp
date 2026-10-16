#pragma once

#include "Core/Assert.hpp"
#include "Scene/ComponentInfo.hpp"
#include "Scene/Entity.hpp"
#include "Scene/SceneManager.hpp"

#include <initializer_list>
#include <functional>
#include <span>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

namespace Index::Package {

    template <typename TComponent>
    void RegisterComponent(
        const char* displayName,
        const char* subcategory                                = "",
        const char* serializedName                             = "",
        void (*drawInspector)(std::span<const Entity>)         = nullptr,
        ComponentCategory category = ComponentCategory::Component,
        const char* icon                                       = nullptr) {

        ComponentInfo info(displayName, subcategory ? subcategory : "", category);
        if (serializedName && *serializedName) {
            info.serializedName = serializedName;
        }
        info.drawInspector = drawInspector;
        if (icon) info.icon = icon;
        SceneManager::RegisterComponentType<TComponent>(info);
    }

    template <typename TComponent>
    struct ComponentSerializerTrampoline {
        static inline Json::Value (*s_Serialize)(const TComponent&) = nullptr;
        static inline void (*s_Deserialize)(TComponent&, const Json::Value&) = nullptr;
        static Json::Value SerializeAdapter(Entity entity) {
            return s_Serialize(entity.GetComponent<TComponent>());
        }
        static void DeserializeAdapter(Entity entity, const Json::Value& value) {
            s_Deserialize(entity.GetComponent<TComponent>(), value);
        }
    };

    template <typename TComponent>
    void RegisterSerializableComponent(
        const char* displayName,
        const char* subcategory,
        const char* serializedName,
        void (*drawInspector)(std::span<const Entity>),
        Json::Value (*serialize)(const TComponent&),
        void (*deserialize)(TComponent&, const Json::Value&),
        ComponentCategory category = ComponentCategory::Component,
        std::vector<PropertyDescriptor> properties = {},
        const char* icon = nullptr) {

        ComponentInfo info(displayName, subcategory ? subcategory : "", category);
        if (serializedName && *serializedName) {
            info.serializedName = serializedName;
        }
        info.drawInspector = drawInspector;
        info.properties = std::move(properties);
        if (icon) info.icon = icon;

        using Trampoline = ComponentSerializerTrampoline<TComponent>;
        Trampoline::s_Serialize = serialize;
        Trampoline::s_Deserialize = deserialize;
        info.serialize = serialize ? &Trampoline::SerializeAdapter : nullptr;
        info.deserialize = deserialize ? &Trampoline::DeserializeAdapter : nullptr;

        SceneManager::RegisterComponentType<TComponent>(info);
    }

    // Editor icon by name from IndexAssets/Textures/Editor/General (e.g. "particles"). Call AFTER RegisterComponent.
    template <typename TComponent>
    void SetComponentIcon(const char* icon) {
        ComponentInfo* info = SceneManager::GetComponentRegistry().GetInfo<TComponent>();
        IDX_CORE_ASSERT(info, IndexErrorCode::InvalidValue,
            "SetComponentIcon<T>: component type is not registered yet — "
            "call RegisterComponent / RegisterSerializableComponent first.");
        if (info) info->icon = icon ? icon : "";
    }

    // Call AFTER RegisterComponent; asserts if TComponent isn't registered yet (avoids silently dropping the gizmo).
    template <typename TComponent>
    void SetEditorGizmo(void (*drawEditorGizmo)(Entity), bool always = false) {
        const std::type_index typeId(typeid(TComponent));
        bool patched = false;
        SceneManager::GetComponentRegistry().ForEachComponentInfo(
            [&](const std::type_index& id, ComponentInfo& info) {
                if (id == typeId) {
                    info.drawEditorGizmo = drawEditorGizmo;
                    info.drawEditorGizmoAlways = always;
                    patched = true;
                }
            });
        IDX_CORE_ASSERT(patched, IndexErrorCode::InvalidValue,
            "SetEditorGizmo<T>: component type is not registered yet — "
            "call RegisterComponent / RegisterSerializableComponent first.");
    }

    inline void RegisterEntityPreset(
        const char* menuPath,
        const char* label,
        const char* defaultName,
        std::function<Entity(Scene&)> create) {

        SceneManager::RegisterEntityPreset({
            menuPath ? menuPath : "",
            label ? label : "",
            defaultName ? defaultName : (label ? label : "Entity"),
            std::move(create)
        });
    }

    template <typename TComponent>
    void RegisterComponentEntityPreset(
        const char* menuPath,
        const char* label,
        const char* defaultName = nullptr) {

        std::string baseName = defaultName ? defaultName : (label ? label : "Entity");
        RegisterEntityPreset(menuPath, label, baseName.c_str(),
            [baseName](Scene& scene) {
                Entity entity = scene.CreateEntity(baseName);
                SceneManager::GetComponentRegistry().AddWithDependencies(entity, typeid(TComponent));
                return entity;
            });
    }

    template <typename A, typename B>
    void DeclareComponentConflict() {
        const std::type_index aId(typeid(A));
        const std::type_index bId(typeid(B));
        SceneManager::GetComponentRegistry().ForEachComponentInfo(
            [&](const std::type_index& id, ComponentInfo& info) {
                if (id == aId) {
                    bool present = false;
                    for (const auto& c : info.conflictsWith) if (c == bId) { present = true; break; }
                    if (!present) info.conflictsWith.push_back(bId);
                }
                else if (id == bId) {
                    bool present = false;
                    for (const auto& c : info.conflictsWith) if (c == aId) { present = true; break; }
                    if (!present) info.conflictsWith.push_back(aId);
                }
            });
    }

} // namespace Index::Package
