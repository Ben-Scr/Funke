#pragma once
#include "Core/Export.hpp"
#include "Physics/CollisionDispatcher.hpp"
#include "Scene/EntityHandle.hpp"
#include "Physics/PhysicsTypes.hpp"
#include "Collections/Vec2.hpp"
#include <box2d/box2d.h>
#include <cstdint>
#include <unordered_map>

namespace Funke {
    class Scene;
}

namespace Funke {

    class Box2DWorld {
    public:
		Box2DWorld();
		~Box2DWorld();

		Box2DWorld(const Box2DWorld&) = delete;
		Box2DWorld& operator=(const Box2DWorld&) = delete;

		// Move deleted: CollisionDispatcher holds b2ShapeId/b2BodyId keys into b2WorldId; moving would orphan them.
		Box2DWorld(Box2DWorld&&) = delete;
		Box2DWorld& operator=(Box2DWorld&&) = delete;

        void Step(float dt);


        b2BodyId CreateBody(EntityHandle nativeEntity, Scene& scene, BodyType bodyType);
        b2ShapeId CreateShape(EntityHandle nativeEntity, Scene& scene, b2BodyId bodyId, ShapeType shapeType, bool isSensor = false);
        CollisionBodyRef ResolveShape(b2ShapeId shapeId) const;
        void UnregisterBodyBinding(b2BodyId bodyId);

        // Destroy every body whose binding points at `scene`, then erase the
        // bindings. Called on scene unload so subsequent contact dispatches and
        // raycast resolves cannot return a Scene* that has just been freed.
        void DestroyAllBodiesForScene(Scene* scene);

        // Static body at the origin that joints without a connected body attach to; made on first use.
        b2BodyId GetGroundBody();
        Vec2 GetGravity() const;
        void SetGravity(const Vec2& gravity);

        CollisionDispatcher& GetDispatcher();
        b2WorldId GetWorldID() { return m_WorldId; }
		FUNKE_API int GetWorkerCount() const;
        void Destroy();
    private:
        struct BodyBinding {
            EntityHandle Entity = entt::null;
            Scene* OwningScene = nullptr;
        };

        b2WorldId m_WorldId = b2_nullWorldId;
        b2BodyId m_GroundBody = b2_nullBodyId;
        CollisionDispatcher m_Dispatcher{};
        std::unordered_map<uint64_t, BodyBinding> m_BodyBindings;
    };
}
