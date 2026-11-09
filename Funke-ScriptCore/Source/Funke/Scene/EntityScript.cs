using System;
using System.Diagnostics.CodeAnalysis;
using System.Threading;
using System.Threading.Tasks;
using Funke.Components;
using Funke.Coroutines;

namespace Funke;

/// <summary>Opt-in reset before a new editor Play session; only static, non-generic, synchronous parameterless void methods are supported.</summary>
[AttributeUsage(AttributeTargets.Method, Inherited = false)]
public sealed class ResetOnPlayAttribute : Attribute { }

/// <summary>Opt-in EntityScript state transfer across a successful Play hot reload.</summary>
public interface IHotReloadState
{
    void CaptureReloadState(ReloadState state);
    /// <summary>Runs after OnAwake, OnEnable and OnStart, before the first regular update. Rebuild derived state here.</summary>
    void RestoreReloadState(ReloadState state);
}

/// <summary>Named scalar values for one reload. Store stable entity/scene/asset IDs as ulong; objects and asynchronous work are not supported.</summary>
public sealed class ReloadState
{
    private readonly System.Collections.Generic.Dictionary<string, string[]> m_Values;
    private readonly System.Collections.Generic.HashSet<string> m_Read = new(StringComparer.Ordinal);
    private readonly bool m_ReadOnly;
    internal ReloadState() { m_Values = new(StringComparer.Ordinal); }
    internal ReloadState(string json)
    {
        m_Values = System.Text.Json.JsonSerializer.Deserialize<System.Collections.Generic.Dictionary<string, string[]>>(json)!;
        m_ReadOnly = true;
    }
    private static string ScalarType<T>()
    {
        Type type = typeof(T);
        if (type != typeof(string) && type != typeof(bool) && type != typeof(int) && type != typeof(uint)
            && type != typeof(long) && type != typeof(ulong) && type != typeof(float) && type != typeof(double))
            throw new NotSupportedException($"ReloadState does not support {type.FullName}. Use scalar values or stable ulong IDs.");
        return type.FullName!;
    }
    /// <summary>Captures bool, int, uint, long, ulong, float, double or string. Other types throw NotSupportedException.</summary>
    public void Set<T>(string key, T value)
    {
        if (m_ReadOnly) throw new InvalidOperationException("Restore state is read-only.");
        ArgumentException.ThrowIfNullOrEmpty(key);
        m_Values[key] = new[] { ScalarType<T>(), System.Text.Json.JsonSerializer.Serialize(value) };
    }
    /// <summary>Reads an exact scalar type. Missing or changed types return false; unread values are reported after restore.</summary>
    public bool TryGet<T>(string key, out T value)
    {
        string type = ScalarType<T>();
        value = default!;
        if (!m_Values.TryGetValue(key, out var entry) || entry[0] != type) return false;
        value = System.Text.Json.JsonSerializer.Deserialize<T>(entry[1])!;
        m_Read.Add(key);
        return true;
    }
    internal string Serialize() => System.Text.Json.JsonSerializer.Serialize(m_Values);
    internal string UnreadKeys => string.Join(", ", System.Linq.Enumerable.Where(m_Values.Keys, key => !m_Read.Contains(key)));
}

/// <summary>Base class for user C# scripts; also acts as a Component so GetComponent/AddComponent/HasComponent work uniformly.</summary>
public abstract class EntityScript : Component
{
    public Transform2D? Transform => Entity.Transform;
    /// <summary>Cached 2D component, or null when absent; never adds a transform.</summary>
    public Transform2D? Transform2D => Entity.Transform2D;
    /// <summary>Cached 3D component, or null when absent; never adds a transform.</summary>
    public Transform3D? Transform3D => Entity.Transform3D;

    internal void _SetEntityID(ulong id)
    {
        Entity = new Entity(id);
    }

    protected Entity? FindEntityByName(string name) => Entity.FindByName(name);

    protected T? GetComponent<T>() where T : Component, new() => Entity.GetComponent<T>();
    /// <summary>Checks the attached entity without adding a component.</summary>
    protected bool HasComponent<T>() where T : Component, new() => Entity.HasComponent<T>();
    /// <summary>A true result guarantees a non-null component.</summary>
    protected bool TryGetComponent<T>([NotNullWhen(true)] out T? component) where T : Component, new() => Entity.TryGetComponent(out component);
    protected object? GetComponent(string componentOrScriptName) => Entity.GetComponent(componentOrScriptName);
    protected EntityScript? GetScript(string scriptName) => Entity.GetScript(scriptName);
    protected T? AddComponent<T>() where T : Component, new() => Entity.AddComponent<T>();
    protected bool RemoveComponent<T>() where T : Component, new() => Entity.RemoveComponent<T>();

    protected bool AddNativeComponent<T>() where T : unmanaged, IComponent => Entity.AddNativeComponent<T>();
    /// <summary>Checks the attached entity without adding a native component.</summary>
    protected bool HasNativeComponent<T>() where T : unmanaged, IComponent => Entity.HasNativeComponent<T>();
    /// <summary>Removal invalidates borrowed component references.</summary>
    protected bool RemoveNativeComponent<T>() where T : unmanaged, IComponent => Entity.RemoveNativeComponent<T>();
    /// <inheritdoc cref="Funke.Entity.GetRefOrThrow{T}"/>
    protected unsafe ref T GetRefOrThrow<T>() where T : unmanaged, IComponent => ref Entity.GetRefOrThrow<T>();
    /// <inheritdoc cref="Funke.Entity.GetNativeComponent{T}"/>
    protected unsafe ref T GetNativeComponent<T>() where T : unmanaged, IComponent => ref Entity.GetNativeComponent<T>();
    /// <inheritdoc cref="Funke.Entity.GetNativeComponentReadOnly{T}"/>
    protected unsafe ref readonly T GetNativeComponentReadOnly<T>() where T : unmanaged, IComponent => ref Entity.GetNativeComponentReadOnly<T>();


    /// <inheritdoc cref="Funke.Entity.Create2D"/>
    protected Entity Create2D(string? name = null, Vector2 localPosition = default, float localRotationDegrees = 0, Entity? parent = null) => Entity.Create2D(name, localPosition, localRotationDegrees, parent);
    /// <inheritdoc cref="Funke.Entity.Create3D"/>
    protected Entity Create3D(string? name = null, Vector3 localPosition = default, Vector3 localRotationDegrees = default, Entity? parent = null) => Entity.Create3D(name, localPosition, localRotationDegrees, parent);
    /// <inheritdoc cref="Funke.Entity.Instantiate2D"/>
    protected Entity Instantiate2D(Entity source, Vector2 localPosition = default, float localRotationDegrees = 0, Entity? parent = null) => Entity.Instantiate2D(source, localPosition, localRotationDegrees, parent);
    /// <inheritdoc cref="Funke.Entity.Instantiate3D"/>
    protected Entity Instantiate3D(Entity source, Vector3 localPosition = default, Vector3 localRotationDegrees = default, Entity? parent = null) => Entity.Instantiate3D(source, localPosition, localRotationDegrees, parent);

    protected Entity Create(string? name = null) => Entity.Create(name);
    protected Entity Create(string? name, Entity? parent) => Entity.Create(name, parent);
    protected Entity Create(Entity source) => Entity.Create(source);
    protected Entity Instantiate(Entity prefabOrSource) => Entity.Instantiate(prefabOrSource);
    protected Entity Instantiate(Entity prefabOrSource, Entity? parent) => Entity.Instantiate(prefabOrSource, parent);
    protected Entity Instantiate(Entity prefabOrSource, Vector3 position, float rotation = 0.0f, Entity? parent = null) => Entity.Instantiate(prefabOrSource, position, rotation, parent);

    protected void Destroy(Entity entity) => Entity.Destroy(entity);
    protected void Print(object? obj, [System.Runtime.CompilerServices.CallerFilePath] string file = "",
        [System.Runtime.CompilerServices.CallerLineNumber] int line = 0) => Log.Print(obj, file, line);

    private CancellationTokenSource? m_CoroutineCts;
    private bool m_CoroutineCtsTerminated;

    /// <summary>Token cancelled before OnDestroy(); pass to external async work for automatic teardown.</summary>
    protected CancellationToken DestroyToken
    {
        get
        {
            if (m_CoroutineCtsTerminated)
                return new CancellationToken(canceled: true);
            return (m_CoroutineCts ??= new CancellationTokenSource()).Token;
        }
    }

    /// <summary>Fire-and-forget coroutine entry point. Swallows OperationCanceledException on destroy; logs other exceptions.</summary>
    protected void RunCoroutine(Func<Task> coroutine)
    {
        CancellationToken token = DestroyToken;
        CancellationToken previous = CoroutineContext.CurrentToken.Value;
        CoroutineContext.CurrentToken.Value = token;
        try
        {
            _ = ObserveCoroutine(coroutine());
        }
        finally
        {
            CoroutineContext.CurrentToken.Value = previous;
        }
    }

    private static async Task ObserveCoroutine(Task task)
    {
        try { await task; }
        catch (OperationCanceledException) { }
        catch (Exception ex) { Log.Error($"[Coroutine] {ex}"); }
    }

    internal void _CancelPendingCoroutines()
    {
        var cts = m_CoroutineCts;
        m_CoroutineCtsTerminated = true;
        m_CoroutineCts = null;
        if (cts == null) return;
        try { cts.Cancel(); }
        finally { cts.Dispose(); }
    }


    public virtual void OnAwake() { }
    public virtual void OnStart() { }
    public virtual void OnUpdate() { }
    public virtual void OnFixedUpdate() { }
    public virtual void OnDestroy() { }

    public virtual void OnApplicationStart() { }
    public virtual void OnApplicationPaused() { }
    public virtual void OnApplicationQuit() { }
    public virtual void OnFocusChanged(bool focused) { }

    public virtual void OnEnable() { }
    public virtual void OnDisable() { }

    public virtual void OnCollisionEnter2D(Collision2D collision) { }
    public virtual void OnCollisionStay2D(Collision2D collision) { }
    public virtual void OnCollisionExit2D(Collision2D collision) { }

    /// <summary>Called when another collider starts overlapping a sensor ("Sensor" enabled) on this entity, or this entity's collider enters another entity's sensor.</summary>
    public virtual void OnTriggerEnter2D(Collision2D other) { }
    /// <summary>Called every physics step while a sensor overlap persists.</summary>
    public virtual void OnTriggerStay2D(Collision2D other) { }
    /// <summary>Called when a sensor overlap ends, including when either collider is destroyed or disabled.</summary>
    public virtual void OnTriggerExit2D(Collision2D other) { }

    /// <summary>Called when this entity's Rigidbody3D (or static collider) starts touching another one.</summary>
    public virtual void OnCollisionEnter3D(Collision3D collision) { }
    /// <summary>Called every physics step while the contact persists.</summary>
    public virtual void OnCollisionStay3D(Collision3D collision) { }
    public virtual void OnCollisionExit3D(Collision3D collision) { }

    /// <summary>Called when a 3D body starts overlapping a trigger ("Is Trigger"), on both the trigger and the other entity.</summary>
    public virtual void OnTriggerEnter3D(Collision3D other) { }
    public virtual void OnTriggerStay3D(Collision3D other) { }
    public virtual void OnTriggerExit3D(Collision3D other) { }
}
