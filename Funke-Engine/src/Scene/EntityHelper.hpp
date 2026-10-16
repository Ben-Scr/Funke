#pragma once

#include "Scene/EntityHandle.hpp"
#include "Scene/Entity.hpp"
#include "Scene/SceneExecutionContext.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Scene.hpp"
#include "Components/ComponentUtils.hpp"
#include "Core/Export.hpp"
#include "Core/Log.hpp"

namespace Funke {
    class FUNKE_API EntityHelper {
    public:
        template<typename... Components>
        static Entity CreateWith(Scene& scene) {
            Entity entity = scene.GetEntity(scene.CreateEntityHandle());

            (entity.AddComponent<Components>(), ...);

            return entity;
        }

        // Info: Creates an entity with the entered Components
        template<typename... Components>
        static Entity CreateWith() {
            Scene* targetScene = ResolveImplicitScene();
            if (!targetScene) {
                FNK_ERROR_TAG("EntityHelper", "Cannot create entity because there is no current or active scene");
                return Entity::Null;
            }

            Entity entity = targetScene->GetEntity(targetScene->CreateEntityHandle());

            (entity.AddComponent<Components>(), ...);

            return entity;
        }

        template<typename... Components>
        static EntityHandle CreateHandleWith(Scene& scene) {
            EntityHandle entity = scene.CreateEntityHandle();

            (scene.AddComponent<Components>(entity), ...);
            return entity;
        }

        template<typename... Components>
        static EntityHandle CreateHandleWith() {
            Scene* targetScene = ResolveImplicitScene();
            if (!targetScene) {
                FNK_ERROR_TAG("EntityHelper", "Cannot create entity handle because there is no current or active scene");
                return entt::null;
            }

            EntityHandle entity = targetScene->CreateEntityHandle();

            (targetScene->AddComponent<Components>(entity), ...);
            return entity;
        }

        static void SetEnabled(Entity entity, bool enabled);
        static bool IsEnabled(Entity entity);

        // Info: Gives you back the global entities count
        static std::size_t EntitiesCount();

        static Entity CreateCamera2DEntity(Scene& scene);
        // Info: Basically calls CreateWith<Transform2D, Canera2D>();
        static Entity CreateCamera2DEntity();
        static Entity CreateSpriteEntity(Scene& scene);
        // Info: Basically calls CreateWith<Transform2D, SpriteRenderer>();
        static Entity CreateSpriteEntity();
        static Entity CreateImageEntity(Scene& scene);
        // Info: Basically calls CreateWith<RectTransform2D, Image>();
        static Entity CreateImageEntity();

        static Entity CreateUIPanel(Scene& scene);
        static Entity CreateUIPanel();
        static Entity CreateUIButton(Scene& scene);
        static Entity CreateUIButton();
        static Entity CreateUISlider(Scene& scene);
        static Entity CreateUISlider();
        static Entity CreateUIProgressBar(Scene& scene);
        static Entity CreateUIProgressBar();
        static Entity CreateUICircularSlider(Scene& scene);
        static Entity CreateUICircularSlider();
        static Entity CreateUIInputField(Scene& scene);
        static Entity CreateUIInputField();
        static Entity CreateUIDropdown(Scene& scene);
        static Entity CreateUIDropdown();
        static Entity CreateUIToggle(Scene& scene);
        static Entity CreateUIToggle();
        static Entity CreateUIScrollbar(Scene& scene);
        static Entity CreateUIScrollbar();
        static Entity CreateUIScrollRect(Scene& scene);
        static Entity CreateUIScrollRect();
        static Entity CreateUIHorizontalLayout(Scene& scene);
        static Entity CreateUIHorizontalLayout();
        static Entity CreateUIVerticalLayout(Scene& scene);
        static Entity CreateUIVerticalLayout();
        static Entity CreateUIGridLayout(Scene& scene);
        static Entity CreateUIGridLayout();
    };

}
