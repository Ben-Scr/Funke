using Funke;

namespace ScriptingExamples;

public class SpawnExamples : EntityScript
{
    public Entity? Prefab2D;
    public Entity? Prefab3D;

    public override void OnStart()
    {
        Entity root2D = Create2D("2D parent", new Vector2(10, 20), 90);
        Entity root3D = Create3D("3D parent", new Vector3(10, 20, 30), new Vector3(0, 90, 0));
        if (Prefab2D != null) Instantiate2D(Prefab2D, new Vector2(2, 3), 90, root2D);
        if (Prefab3D != null) Instantiate3D(Prefab3D, new Vector3(2, 3, 7), new Vector3(0, 90, 0), root3D);
    }
}
