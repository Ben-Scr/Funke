# {{ProjectName}} - 2D starter

## Try the scene

Open `Assets/Scenes/SampleScene.scene` and press **Play**. The orange Box falls
onto the Ground using 2D physics. Press **Stop** to return to the authored scene.
No game scripts or player controls are included; the demo uses built-in components.

## What is included

- **Camera**: an orthographic Camera2D centered on the scene.
- **Ground**: a SpriteRenderer, a static Rigidbody2D and a BoxCollider2D.
- **Box**: a SpriteRenderer, a dynamic Rigidbody2D and a BoxCollider2D.
- **Engine modules**: `Funke.2D` and `Funke.Physics2D` (Box2D).

## Make it yours

1. Select an entity in the Hierarchy to edit its components in the Inspector.
2. Move or resize the Ground using Transform2D. The collider scales with it;
   its local size is already one unit, so do not apply the same scale twice.
3. Duplicate the Box to add more physics objects. Edit Gravity Scale, Friction
   and Bounciness to change how they move and collide.
4. For a top-down game, set dynamic bodies' Gravity Scale to `0`.
5. Add your own C# or C++ scripts through the Asset Browser when you need behavior.
6. Save the scene before building. SampleScene is already the startup scene.

Manage engine modules and optional packages in **Package Manager**. Both physics
and the 2D module are enabled; no optional gameplay packages are required.

Want a completely blank start? Choose **Empty 2D** in the Launcher. It has no
entities, physics module, starter scripts or Help.md. You must add a camera yourself.

This file is only a guide and can be deleted.
