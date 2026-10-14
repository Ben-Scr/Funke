using Index.Interop;

namespace Index;

// Values must match the native PrimitiveMeshType (serialized as int).
public enum PrimitiveMesh
{
    Cube = 0,
    Sphere = 1,
    Plane = 2,
    Cylinder = 3,
}

public enum CameraProjection
{
    Perspective = 0,
    Orthographic = 1,
}

// Values must match the native CameraBackground.
public enum CameraBackground
{
    Skybox = 0,
    SolidColor = 1,
}

// ── Transform3D ─────────────────────────────────────────────────────

/// <summary>Position, rotation (Euler degrees) and scale relative to the parent's Transform3D.</summary>
public class Transform3D : Component
{
    public Vector3 Position
    {
        get => InternalCalls.Transform3D_GetPosition(RequireComponent<Transform3D>());
        set => InternalCalls.Transform3D_SetPosition(RequireComponent<Transform3D>(), value);
    }

    /// <summary>Euler angles in degrees: X = pitch, Y = yaw, Z = roll.</summary>
    public Vector3 Rotation
    {
        get => InternalCalls.Transform3D_GetRotation(RequireComponent<Transform3D>());
        set => InternalCalls.Transform3D_SetRotation(RequireComponent<Transform3D>(), value);
    }

    public Vector3 Scale
    {
        get => InternalCalls.Transform3D_GetScale(RequireComponent<Transform3D>());
        set => InternalCalls.Transform3D_SetScale(RequireComponent<Transform3D>(), value);
    }

    /// <summary>World-space position as of the last transform update (one frame behind a Position set this frame).</summary>
    public Vector3 WorldPosition => InternalCalls.Transform3D_GetWorldPosition(RequireComponent<Transform3D>());

    /// <summary>World-space forward direction (local -Z).</summary>
    public Vector3 Forward => InternalCalls.Transform3D_GetForward(RequireComponent<Transform3D>());
}

// ── Camera3D ────────────────────────────────────────────────────────

public class Camera3D : Component
{
    public static Camera3D? Main
    {
        get
        {
            ulong entityId = InternalCalls.Camera3D_GetMainEntity();
            if (entityId == 0) return null;
            return new Entity(entityId).GetComponent<Camera3D>();
        }
    }

    public CameraProjection Projection
    {
        get => (CameraProjection)InternalCalls.Camera3D_GetProjection(RequireComponent<Camera3D>());
        set => InternalCalls.Camera3D_SetProjection(RequireComponent<Camera3D>(), (int)value);
    }

    /// <summary>Vertical field of view in degrees.</summary>
    public float FieldOfView
    {
        get => InternalCalls.Camera3D_GetFieldOfView(RequireComponent<Camera3D>());
        set => InternalCalls.Camera3D_SetFieldOfView(RequireComponent<Camera3D>(), value);
    }

    public float OrthographicSize
    {
        get => InternalCalls.Camera3D_GetOrthographicSize(RequireComponent<Camera3D>());
        set => InternalCalls.Camera3D_SetOrthographicSize(RequireComponent<Camera3D>(), value);
    }

    public float NearClip
    {
        get => InternalCalls.Camera3D_GetNearClip(RequireComponent<Camera3D>());
        set => InternalCalls.Camera3D_SetNearClip(RequireComponent<Camera3D>(), value);
    }

    public float FarClip
    {
        get => InternalCalls.Camera3D_GetFarClip(RequireComponent<Camera3D>());
        set => InternalCalls.Camera3D_SetFarClip(RequireComponent<Camera3D>(), value);
    }

    /// <summary>Used when Background is SolidColor.</summary>
    public Color ClearColor
    {
        get => InternalCalls.Camera3D_GetClearColor(RequireComponent<Camera3D>());
        set => InternalCalls.Camera3D_SetClearColor(RequireComponent<Camera3D>(), value);
    }

    public CameraBackground Background
    {
        get => (CameraBackground)(int)ReadSky()[0];
        set => WriteSky(v => v[0] = (int)value);
    }

    public Color SkyTopColor
    {
        get => ReadSkyColor(1);
        set => WriteSky(v => PutColor(v, 1, value));
    }

    public Color SkyHorizonColor
    {
        get => ReadSkyColor(5);
        set => WriteSky(v => PutColor(v, 5, value));
    }

    public Color SkyGroundColor
    {
        get => ReadSkyColor(9);
        set => WriteSky(v => PutColor(v, 9, value));
    }

    /// <summary>Angular radius in degrees of the sun disc drawn towards the main DirectionalLight; 0 hides it.</summary>
    public float SunSize
    {
        get => ReadSky()[13];
        set => WriteSky(v => v[13] = value);
    }

    /// <summary>How strongly the sky colors light meshes as hemispheric ambient (0 = off).</summary>
    public float SkyAmbient
    {
        get => ReadSky()[14];
        set => WriteSky(v => v[14] = value);
    }

    private float[] ReadSky()
    {
        var values = new float[InternalCalls.CameraSkyValueCount];
        InternalCalls.Camera3D_GetSky(RequireComponent<Camera3D>(), values);
        return values;
    }

    private Color ReadSkyColor(int index)
    {
        float[] v = ReadSky();
        return new Color(v[index], v[index + 1], v[index + 2], v[index + 3]);
    }

    private void WriteSky(System.Action<float[]> change)
    {
        float[] values = ReadSky();
        change(values);
        InternalCalls.Camera3D_SetSky(RequireComponent<Camera3D>(), values);
    }

    private static void PutColor(float[] values, int index, Color color)
    {
        values[index] = color.R; values[index + 1] = color.G; values[index + 2] = color.B; values[index + 3] = color.A;
    }
}

// ── MeshRenderer ────────────────────────────────────────────────────

public class MeshRenderer : Component
{
    /// <summary>Built-in primitive; only drawn while <see cref="CustomMesh"/> is null.</summary>
    public PrimitiveMesh Mesh
    {
        get => (PrimitiveMesh)InternalCalls.MeshRenderer_GetMesh(RequireComponent<MeshRenderer>());
        set => InternalCalls.MeshRenderer_SetMesh(RequireComponent<MeshRenderer>(), (int)value);
    }

    /// <summary>A mesh asset (.obj) drawn instead of the primitive <see cref="Mesh"/>; null draws the primitive.</summary>
    public global::Index.Mesh? CustomMesh
    {
        get => global::Index.Mesh.FromAssetUUID(InternalCalls.MeshRenderer_GetCustomMesh(RequireComponent<MeshRenderer>()));
        set => InternalCalls.MeshRenderer_SetCustomMesh(RequireComponent<MeshRenderer>(), value?.UUID ?? 0);
    }

    /// <summary>The .material asset; null draws a white default material tinted by Color.</summary>
    public Material? Material
    {
        get => Material.FromAssetUUID(InternalCalls.MeshRenderer_GetMaterial(RequireComponent<MeshRenderer>()));
        set => InternalCalls.MeshRenderer_SetMaterial(RequireComponent<MeshRenderer>(), value?.UUID ?? 0);
    }

    /// <summary>Tint multiplied into the material's base color.</summary>
    public Color Color
    {
        get => InternalCalls.MeshRenderer_GetColor(RequireComponent<MeshRenderer>());
        set => InternalCalls.MeshRenderer_SetColor(RequireComponent<MeshRenderer>(), value);
    }

    /// <summary>Only used without a material: false draws the flat color and ignores lights.</summary>
    public bool Lit
    {
        get => InternalCalls.MeshRenderer_GetLit(RequireComponent<MeshRenderer>());
        set => InternalCalls.MeshRenderer_SetLit(RequireComponent<MeshRenderer>(), value);
    }

    public bool CastShadows
    {
        get => InternalCalls.MeshRenderer_GetCastShadows(RequireComponent<MeshRenderer>());
        set => InternalCalls.MeshRenderer_SetCastShadows(RequireComponent<MeshRenderer>(), value);
    }
}

// ── DirectionalLight ────────────────────────────────────────────────

/// <summary>Shines along the entity's Transform3D forward. The first enabled one is the main light (sun disc, shadows).</summary>
public class DirectionalLight : Component
{
    public Color Color
    {
        get => InternalCalls.DirectionalLight_GetColor(RequireComponent<DirectionalLight>());
        set => InternalCalls.DirectionalLight_SetColor(RequireComponent<DirectionalLight>(), value);
    }

    public float Intensity
    {
        get => InternalCalls.DirectionalLight_GetIntensity(RequireComponent<DirectionalLight>());
        set => InternalCalls.DirectionalLight_SetIntensity(RequireComponent<DirectionalLight>(), value);
    }

    public Color Ambient
    {
        get => InternalCalls.DirectionalLight_GetAmbient(RequireComponent<DirectionalLight>());
        set => InternalCalls.DirectionalLight_SetAmbient(RequireComponent<DirectionalLight>(), value);
    }

    /// <summary>Only the main (first) directional light casts shadows.</summary>
    public bool CastShadows
    {
        get => InternalCalls.DirectionalLight_GetCastShadows(RequireComponent<DirectionalLight>());
        set => InternalCalls.DirectionalLight_SetCastShadows(RequireComponent<DirectionalLight>(), value);
    }
}

// ── PointLight ──────────────────────────────────────────────────────

/// <summary>Omnidirectional light at the entity's position; inverse-square falloff that reaches zero at Range.</summary>
public class PointLight : Component
{
    public Color Color
    {
        get { var v = Read(); return new Color(v[0], v[1], v[2], v[3]); }
        set => Write(v => { v[0] = value.R; v[1] = value.G; v[2] = value.B; v[3] = value.A; });
    }

    public float Intensity
    {
        get => Read()[4];
        set => Write(v => v[4] = value);
    }

    /// <summary>World units.</summary>
    public float Range
    {
        get => Read()[5];
        set => Write(v => v[5] = value);
    }

    private float[] Read()
    {
        var values = new float[InternalCalls.PointLightValueCount];
        InternalCalls.PointLight_Get(RequireComponent<PointLight>(), values);
        return values;
    }

    private void Write(System.Action<float[]> change)
    {
        float[] values = Read();
        change(values);
        InternalCalls.PointLight_Set(RequireComponent<PointLight>(), values);
    }
}

// ── SpotLight ───────────────────────────────────────────────────────

/// <summary>Cone light along the entity's Transform3D forward with a soft edge between InnerAngle and OuterAngle.</summary>
public class SpotLight : Component
{
    public Color Color
    {
        get { var v = Read(); return new Color(v[0], v[1], v[2], v[3]); }
        set => Write(v => { v[0] = value.R; v[1] = value.G; v[2] = value.B; v[3] = value.A; });
    }

    public float Intensity
    {
        get => Read()[4];
        set => Write(v => v[4] = value);
    }

    /// <summary>World units.</summary>
    public float Range
    {
        get => Read()[5];
        set => Write(v => v[5] = value);
    }

    /// <summary>Half angle in degrees with full intensity.</summary>
    public float InnerAngle
    {
        get => Read()[6];
        set => Write(v => v[6] = value);
    }

    /// <summary>Half angle in degrees where the cone fades to zero.</summary>
    public float OuterAngle
    {
        get => Read()[7];
        set => Write(v => v[7] = value);
    }

    private float[] Read()
    {
        var values = new float[InternalCalls.SpotLightValueCount];
        InternalCalls.SpotLight_Get(RequireComponent<SpotLight>(), values);
        return values;
    }

    private void Write(System.Action<float[]> change)
    {
        float[] values = Read();
        change(values);
        InternalCalls.SpotLight_Set(RequireComponent<SpotLight>(), values);
    }
}

// ── ParticleSystem3D ────────────────────────────────────────────────

// Values must match the native ParticleShape3D / ParticleSpace3D / ParticleBlendMode3D.
public enum ParticleShape3D
{
    Sphere = 0,
    Hemisphere = 1,
    Cone = 2,
    Box = 3,
}

public enum ParticleSpace3D
{
    Local = 0,
    World = 1,
}

public enum ParticleBlendMode3D
{
    Alpha = 0,
    Additive = 1,
}

/// <summary>Camera-facing billboard particles emitted around the entity's Transform3D. Cone and Hemisphere open along local +Y.</summary>
public class ParticleSystem3D : Component
{
    public bool IsEmitting => (State(out _) & 1) != 0;
    public bool IsPlaying => IsEmitting;
    public int ParticleCount { get { State(out int count); return count; } }

    public void Play() => InternalCalls.ParticleSystem3D_Control(RequireComponent<ParticleSystem3D>(), 0);
    /// <summary>Freezes emission and simulation; live particles stay visible.</summary>
    public void Pause() => InternalCalls.ParticleSystem3D_Control(RequireComponent<ParticleSystem3D>(), 1);
    /// <summary>Stops emitting; live particles finish their lifetime.</summary>
    public void Stop() => InternalCalls.ParticleSystem3D_Control(RequireComponent<ParticleSystem3D>(), 2);
    public void Restart() => InternalCalls.ParticleSystem3D_Control(RequireComponent<ParticleSystem3D>(), 3);
    /// <summary>Stops and removes every live particle.</summary>
    public void Clear() => InternalCalls.ParticleSystem3D_Control(RequireComponent<ParticleSystem3D>(), 4);
    /// <summary>Emits immediately, even while stopped (one-shot effects: explosions, impacts).</summary>
    public void Emit(int count) => InternalCalls.ParticleSystem3D_Emit(RequireComponent<ParticleSystem3D>(), count);

    public float LifeTime { get => Read()[0]; set => Write(v => v[0] = value); }
    public float Speed { get => Read()[1]; set => Write(v => v[1] = value); }
    /// <summary>Billboard edge length in world units.</summary>
    public float Size { get => Read()[2]; set => Write(v => v[2] = value); }
    /// <summary>Particles per second.</summary>
    public float EmitOverTime { get => Read()[3]; set => Write(v => v[3] = value); }

    public Color Color
    {
        get { var v = Read(); return new Color(v[4], v[5], v[6], v[7]); }
        set => Write(v => { v[4] = value.R; v[5] = value.G; v[6] = value.B; v[7] = value.A; });
    }

    public bool UseGravity { get => Read()[8] != 0.0f; set => Write(v => v[8] = value ? 1.0f : 0.0f); }

    public Vector3 Gravity
    {
        get { var v = Read(); return new Vector3(v[9], v[10], v[11]); }
        set => Write(v => { v[9] = value.X; v[10] = value.Y; v[11] = value.Z; });
    }

    public int MaxParticles { get => (int)Read()[12]; set => Write(v => v[12] = value); }
    public bool PlayOnAwake { get => Read()[13] != 0.0f; set => Write(v => v[13] = value ? 1.0f : 0.0f); }
    public ParticleBlendMode3D BlendMode { get => (ParticleBlendMode3D)(int)Read()[14]; set => Write(v => v[14] = (int)value); }
    public ParticleSpace3D SimulationSpace { get => (ParticleSpace3D)(int)Read()[15]; set => Write(v => v[15] = (int)value); }
    public ParticleShape3D Shape { get => (ParticleShape3D)(int)Read()[16]; set => Write(v => v[16] = (int)value); }
    public float Radius { get => Read()[17]; set => Write(v => v[17] = value); }
    /// <summary>Half angle in degrees.</summary>
    public float ConeAngle { get => Read()[18]; set => Write(v => v[18] = value); }
    /// <summary>Fraction of velocity lost per second.</summary>
    public float Drag { get => Read()[19]; set => Write(v => v[19] = value); }
    /// <summary>Degrees per second around the view axis.</summary>
    public float AngularVelocity { get => Read()[20]; set => Write(v => v[20] = value); }

    /// <summary>Null draws soft round dots.</summary>
    public Texture? Texture
    {
        get => Texture.FromAssetUUID(InternalCalls.ParticleSystem3D_GetTexture(RequireComponent<ParticleSystem3D>()));
        set => InternalCalls.ParticleSystem3D_SetTexture(RequireComponent<ParticleSystem3D>(), value?.UUID ?? 0);
    }

    private int State(out int particleCount) => InternalCalls.ParticleSystem3D_GetState(RequireComponent<ParticleSystem3D>(), out particleCount);

    private float[] Read()
    {
        var values = new float[InternalCalls.ParticleSystem3DValueCount];
        InternalCalls.ParticleSystem3D_Get(RequireComponent<ParticleSystem3D>(), values);
        return values;
    }

    private void Write(System.Action<float[]> change)
    {
        float[] values = Read();
        change(values);
        InternalCalls.ParticleSystem3D_Set(RequireComponent<ParticleSystem3D>(), values);
    }
}
