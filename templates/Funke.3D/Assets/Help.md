# {{ProjectName}} - 3D starter

## Try the scene

Open `Assets/Scenes/SampleScene.scene` and press **Play**. The orange Box falls
onto the Ground using 3D physics. Press **Stop** to return to the authored scene.
No game scripts or player controls are included; the demo uses built-in components.

## What is included

- **Camera**: a perspective Camera3D looking at the ground and falling box.
- **Directional Light**: lighting and ambient fill for the scene.
- **Ground**: a scaled cube MeshRenderer, a static Rigidbody3D and a BoxCollider3D.
- **Box**: a cube MeshRenderer, a dynamic Rigidbody3D and a BoxCollider3D.
- **Engine modules**: `Funke.3D` and `Funke.Physics3D` (Jolt).

## Make it yours

1. Select an entity in the Hierarchy to edit its components in the Inspector.
2. Move or resize the Ground using Transform3D. The collider scales with it;
   its local size is already one unit, so do not apply the same scale twice.
3. Duplicate the Box to add more physics objects. Edit Mass, Gravity Scale,
   Friction and Bounciness to change how they move and collide.
4. Adjust the camera transform and Field of View to frame your scene, and the
   Directional Light to change the lighting.
5. Add your own C# or C++ scripts through the Asset Browser when you need behavior.
6. Save the scene before building. SampleScene is already the startup scene.

Manage engine modules and optional packages in **Package Manager**. Both physics
and the 3D module are enabled; no optional gameplay packages are required.

Want a completely blank start? Choose **Empty 3D** in the Launcher. It has no
entities, physics module, starter scripts or Help.md. Add your own camera and lights.

This file is only a guide and can be deleted.
