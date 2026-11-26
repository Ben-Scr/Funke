"""Component bindings with compound inputs or managed-side convenience methods."""


def extend(classes, used_slots):
    rig = classes["CameraFollowRig"]
    rig.append("""    Entity? Target {
        get { return Funke.Entity.FromId(FkSceneAccess.Call(FkNativeSlot.CameraFollowRig_GetTarget, ID)); }
        set {
            ulong target = 0ul;
            if (value != null) {
                if (value.Value.FkPrefabAsset()) throw System.ArgumentException("Target must be a live scene entity.");
                target = value.Value.ID;
            }
            if (FkSceneAccess.Call(FkNativeSlot.CameraFollowRig_SetTarget, ID, target) == 0ul)
                throw System.ArgumentException("Target must be another live entity in the same scene.");
        }
    }
    Vector3 Offset {
        get {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            FkSceneAccess.Call(FkNativeSlot.CameraFollowRig_GetOffset, ID, FkNativeSlot.Pointer(&x), FkNativeSlot.Pointer(&y), FkNativeSlot.Pointer(&z));
            return Vector3(x, y, z);
        }
        set {
            Vector3.FkValidateFinite(value, "value");
            FkSceneAccess.Call(FkNativeSlot.CameraFollowRig_SetOffset, ID, (ulong)Reinterpret<uint>(value.X),
                (ulong)Reinterpret<uint>(value.Y), (ulong)Reinterpret<uint>(value.Z));
        }
    }
    void Reset() { FkSceneAccess.Call(FkNativeSlot.CameraFollowRig_Reset, ID); }""")
    for name in ("Orbit", "LookAtTarget"):
        rig.append(f"""    bool {name} {{
        get {{ return FkSceneAccess.Call(FkNativeSlot.CameraFollowRig_Get{name}, ID) != 0ul; }}
        set {{ FkSceneAccess.Call(FkNativeSlot.CameraFollowRig_Set{name}, ID, value ? 1ul : 0ul); }}
    }}""")
    for name, invalid in (("Damping", " || value < 0.0f"), ("Yaw", ""),
                          ("Pitch", " || value < -89.0f || value > 89.0f"), ("Distance", " || value <= 0.0f")):
        rig.append(f"""    float {name} {{
        get {{ return Reinterpret<float>((uint)FkSceneAccess.Call(FkNativeSlot.CameraFollowRig_Get{name}, ID)); }}
        set {{
            if (!Mathf.FkFinite(value){invalid}) throw System.ArgumentOutOfRangeException("value");
            FkSceneAccess.Call(FkNativeSlot.CameraFollowRig_Set{name}, ID, (ulong)Reinterpret<uint>(value));
        }}
    }}""")
    for renderer, kind in (("MeshRenderer", 0), ("SkinnedMeshRenderer", 1)):
        classes[renderer].append(f"""    int MaterialSlotCount => (int)FkSceneAccess.Call(FkNativeSlot.MeshRenderer_GetMaterialSlotCount, ID, {kind}ul);
    private void CheckMaterialSlot(int slot) {{
        if (slot < 0 || slot >= MaterialSlotCount) throw System.ArgumentOutOfRangeException("slot");
    }}
    string GetMaterialSlotName(int slot) {{
        CheckMaterialSlot(slot);
        ulong[] args = {{ID, {kind}ul, (ulong)slot, 0ul, 0ul}};
        return FkSceneAccess.Text(FkNativeSlot.MeshRenderer_GetMaterialSlotName, args);
    }}
    Funke.Material? GetMaterialOverride(int slot) {{
        CheckMaterialSlot(slot);
        return Funke.Material.FromAssetUUID(FkSceneAccess.Call(FkNativeSlot.MeshRenderer_GetMaterialSlotOverride, ID, {kind}ul, (ulong)slot));
    }}
    void SetMaterialOverride(int slot, Funke.Material? material) {{
        CheckMaterialSlot(slot);
        ulong guid = 0ul;
        if (material != null) guid = material.Value.UUID;
        if (FkSceneAccess.Call(FkNativeSlot.MeshRenderer_SetMaterialSlotOverride, ID, {kind}ul, (ulong)slot, guid) == 0ul)
            throw System.InvalidOperationException("The model material slot is no longer available.");
    }}""")
    classes["SkinnedMeshRenderer"].append("""    string Clip {
        get {
            ulong[] args = {ID, 0ul, 0ul};
            return FkSceneAccess.Text(FkNativeSlot.SkinnedMeshRenderer_GetClip, args);
        }
    }
    void Play(string clip, float crossFade = 0.2f) {
        byte[] name = FkSceneAccess.CString(clip);
        FkSceneAccess.Call(FkNativeSlot.SkinnedMeshRenderer_Play, ID, FkNativeSlot.Pointer(&name[0]), (ulong)Reinterpret<uint>(crossFade));
    }
    string[] ClipNames {
        get {
            int count = (int)FkSceneAccess.Call(FkNativeSlot.SkinnedMeshRenderer_GetClipCount, ID);
            string[] names = new string[count];
            for (int i = 0; i < count; i++) {
                ulong[] args = {ID, (ulong)i, 0ul, 0ul};
                names[i] = FkSceneAccess.Text(FkNativeSlot.SkinnedMeshRenderer_GetClipName, args);
            }
            return names;
        }
    }
    float GetClipLength(string clip) {
        string[] names = ClipNames;
        for (int i = 0; i < names.Length; i++) {
            if (clip == names[i])
                return Reinterpret<float>((uint)FkSceneAccess.Call(FkNativeSlot.SkinnedMeshRenderer_GetClipDuration, ID, (ulong)i));
        }
        return 0.0f;
    }
    bool TryGetBone(string bone, Vector3& position, Vector3& forward, Vector3& up) {
        byte[] name = FkSceneAccess.CString(bone);
        Vector3[] axes = new Vector3[3];
        ulong found = FkSceneAccess.Call(FkNativeSlot.SkinnedMeshRenderer_GetBone, ID, FkNativeSlot.Pointer(&name[0]), FkNativeSlot.Pointer(&axes[0]), 9ul);
        position = axes[0];
        forward = axes[1];
        up = axes[2];
        return found != 0ul;
    }""")
    classes["TerrainRenderer2D"].append("""    int PointCount => (int)FkSceneAccess.Call(FkNativeSlot.TerrainRenderer2D_GetPoints, ID, 0ul, 0ul);
    Vector2[] GetPoints() {
        int count = PointCount;
        Vector2[] points = new Vector2[count];
        if (count > 0) FkSceneAccess.Call(FkNativeSlot.TerrainRenderer2D_GetPoints, ID, FkNativeSlot.Pointer(&points[0]), (ulong)count);
        return points;
    }
    void SetPoints(System.ReadOnlySpan<Vector2> points) {
        Vector2[] copy = points.ToArray();
        ulong address = 0ul;
        if (copy.Length > 0) address = FkNativeSlot.Pointer(&copy[0]);
        FkSceneAccess.Call(FkNativeSlot.TerrainRenderer2D_SetPoints, ID, address, (ulong)copy.Length);
    }""")
    classes["Transform3D"].append("""    Quaternion LocalRotation {
        get { return Quaternion.Euler(Rotation); }
        set { Rotation = value.EulerAngles; }
    }
    private Quaternion ParentRotation() {
        Quaternion orientation = Quaternion.Identity;
        Funke.Entity owner = FunkeApiTypes.MakeEntity(ID);
        Funke.Entity? parent = owner.Parent;
        int depth = 0;
        while (parent != null) {
            if (++depth > 1024) throw System.InvalidOperationException("Transform hierarchy exceeds 1024 ancestors or contains a cycle.");
            Funke.Entity current = parent.Value;
            Transform3D? transform = current.GetComponent<Transform3D>();
            if (transform != null) orientation = transform.Value.LocalRotation * orientation;
            parent = current.Parent;
        }
        return orientation;
    }
    Quaternion WorldRotation {
        get { return ParentRotation() * LocalRotation; }
        set { LocalRotation = Quaternion.Inverse(ParentRotation()) * value; }
    }
    void SetLocalPose(Vector3 position, Quaternion rotation) {
        if (!Vector3.FkIsFinite(position) || !Quaternion.IsValidRotation(rotation))
            throw System.ArgumentOutOfRangeException("Invalid local pose.");
        Position = position;
        Rotation = rotation.EulerAngles;
    }""")
    # C# extension methods can be called through a transform instance.
    for result, name, parameters, args in (
            ("Vector3", "GetWorldPosition", "", ""),
            ("WorldPose", "GetWorldPose", "", ""),
            ("bool", "TrySetWorldPose", "Vector3 position, Quaternion rotation", ", position, rotation"),
            ("bool", "TrySetWorldPosition", "Vector3 position", ", position"),
            ("void", "SetWorldPosition", "Vector3 position", ", position"),
            *(( "Vector3", method, "Vector3 value", ", value") for method in
              ("TransformPoint", "InverseTransformPoint", "TransformDirection", "InverseTransformDirection",
               "TransformVector", "InverseTransformVector", "TransformNormal")),
            ("void", "Translate", "Vector3 displacement, Space space = Space.Local", ", displacement, space"),
            ("void", "Rotate", "Vector3 eulerDegrees, Space space = Space.Local", ", eulerDegrees, space"),
            ("void", "Rotate", "Vector3 axis, float degrees, Space space = Space.Local", ", axis, degrees, space"),
            ("void", "RotateAround", "Vector3 pivot, Vector3 axis, float degrees", ", pivot, axis, degrees"),
            ("void", "FaceTowards3D", "Vector3 target, float maxDegreesPerSecond, float deltaTime, Vector3? up = null", ", target, maxDegreesPerSecond, deltaTime, up"),
            ("bool", "FaceOnPlane3D", "Vector3 target, Vector3 normal", ", target, normal")):
        forward = "" if result == "void" else "return "
        classes["Transform3D"].append(f"    {result} {name}({parameters}) {{ {forward}TransformUtility.{name}(this{args}); }}")
    for widget, prop, value, observe in (
            ("CircularSlider", "Value", "float", "MarkValueObserved"),
            ("Slider", "Value", "float", "MarkValueObserved"),
            ("Scrollbar", "Value", "float", "MarkValueObserved"),
            ("ScrollRect", "NormalizedPosition", "Vector2", "MarkValueObserved"),
            ("Toggle", "IsOn", "bool", "MarkIsOnObserved"),
            ("InputField", "Text", "string", None)):
        method = "SetNormalizedPosition" if widget == "ScrollRect" else "SetValue"
        observed = f"Fk{widget}_{observe}(ID, 0ul);" if observe else "Events.FkObserveText(value);"
        if observe:
            used_slots.add(widget + "_" + observe)
        classes[widget].append(f"""    void {method}({value} value, bool notifyEvent = true) {{
        {value} previous = {prop};
        {prop} = value;
        {observed}
        {value} applied = {prop};
        if (notifyEvent && applied != previous) Events.FkChanged(applied);
    }}""")



PHYSICS_BASES = {
    "Collider2D": (["BoxCollider2D", "CircleCollider2D", "PolygonCollider2D"],
                    {"IsSensor", "Layer", "CollisionMask", "Friction", "Bounciness", "GroupIndex", "Density"}),
    "Collider3D": (["BoxCollider3D", "SphereCollider3D", "CapsuleCollider3D", "MeshCollider3D"],
                    {"Center", "IsTrigger", "Friction", "Bounciness"}),
    "Joint2D": (["RevoluteJoint2D", "PrismaticJoint2D", "WeldJoint2D"],
                {"ConnectedBody", "Anchor", "ConnectedAnchor", "AutoConfigureConnectedAnchor", "CollideConnected",
                 "IsCreated", "ReactionForce", "ReactionTorque"}),
}


def component_declaration(name, type_id, members):
    import re
    base = "Component"
    prefix = []
    for group, (children, inherited) in PHYSICS_BASES.items():
        if name not in children:
            continue
        base = group
        prefix = [f"    [ReadOnly] public override ulong FkShapeKind() {{ return {children.index(name)}ul; }}"]
        members = [member for member in members if not any(
            re.match(r"\s*[\w.?]+\s+" + prop + r"\s*(?:\{|=>)", member) for prop in inherited)]
        break
    prefix.append(f"    protected override ulong FkType() {{ return {type_id}ul; }}")
    return f"public class {name} : {base} {{\npublic:\n" + "\n".join(prefix + members) + "\n}"
