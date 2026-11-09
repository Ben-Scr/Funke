using System;
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using Funke;
using Funke.Interop;
using Funke.Components;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe sealed class ScriptAssemblyReloadTests : IDisposable
{
    private readonly NativeBindingsStruct m_PreviousBindings = NativeCallbacks.Bindings;
    private readonly delegate* unmanaged<byte*, byte*, int, int> m_PreviousLayout = ComponentTypes.NativeLayoutQuery;
    private readonly string m_Directory = Path.Combine(Path.GetTempPath(), "FunkeReload-" + Guid.NewGuid().ToString("N"));
    private static int s_UnregisterCount;
    private static uint s_RegisteredSize;

    public ScriptAssemblyReloadTests()
    {
        Directory.CreateDirectory(m_Directory);
        NativeCallbacks.Bindings.Component_UnregisterAllDynamic = &Unregister;
        NativeCallbacks.Bindings.Entity_GetComponentSize = &ComponentSize;
        NativeCallbacks.Bindings.Component_RegisterDynamic = &RegisterDynamic;
        ComponentTypes.NativeLayoutQuery = &ComponentLayout;
        ScriptInstanceManager.SetCoreAssembly(typeof(EntityScript).Assembly);
        s_UnregisterCount = 0;
        s_RegisteredSize = 0;
    }

    [UnmanagedCallersOnly]
    private static void Unregister() => ++s_UnregisterCount;

    [UnmanagedCallersOnly]
    private static uint RegisterDynamic(byte* name, byte* serial, byte* category, uint code, uint size, uint alignment)
    {
        s_RegisteredSize = size;
        return 1;
    }

    [UnmanagedCallersOnly]
    private static int ComponentSize(byte* name) => Marshal.PtrToStringUTF8((nint)name) == "Reload Value" ? 8 : 0;

    [UnmanagedCallersOnly]
    private static int ComponentLayout(byte* name, byte* output, int capacity)
    {
        ReadOnlySpan<byte> json = "{\"version\":1,\"dynamic\":false,\"size\":8,\"managedSize\":8,\"fields\":[{\"path\":\"Left\",\"type\":\"f32\",\"offset\":0,\"size\":4},{\"path\":\"Right\",\"type\":\"f32\",\"offset\":4,\"size\":4}]}"u8;
        if (output != null && capacity > json.Length)
        {
            json.CopyTo(new Span<byte>(output, json.Length));
            output[json.Length] = 0;
        }
        return json.Length;
    }

    public void Dispose()
    {
        delegate* unmanaged<void> unload = &ScriptInstanceManager.UnloadUserAssembly;
        unload();
        NativeCallbacks.Bindings = m_PreviousBindings;
        ComponentTypes.NativeLayoutQuery = m_PreviousLayout;
        Directory.Delete(m_Directory, recursive: true);
    }

    [Fact]
    public void Broken_replacement_keeps_previous_types_and_registrations_and_can_retry()
    {
        string path = Path.Combine(m_Directory, "Game.dll");
        WriteAssembly(path, 7);
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        Assembly previous = ScriptInstanceManager.UserAssembly!;
        Assert.Equal(7, ReadVersion());

        File.WriteAllText(path, "broken assembly");
        Assert.Equal(-1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        Assert.Same(previous, ScriptInstanceManager.UserAssembly);
        Assert.Equal(7, ReadVersion());
        Assert.Equal(0, s_UnregisterCount);

        WriteAssembly(path, 11);
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        Assert.NotSame(previous, ScriptInstanceManager.UserAssembly);
        Assert.Equal(11, ReadVersion());
        Assert.Equal(1, s_UnregisterCount);
    }

    [Fact]
    public void Missing_first_assembly_remains_unavailable_and_does_not_publish_candidate()
    {
        Assert.Equal(0, ScriptInstanceManager.LoadUserAssemblyPath(Path.Combine(m_Directory, "Missing.dll")));
        Assert.Null(ScriptInstanceManager.UserAssembly);
        Assert.Null(ScriptInstanceManager.UserAssemblyDirectory);
        Assert.Equal(0, s_UnregisterCount);
    }

    [Fact]
    public void Missing_replacement_preserves_the_original_dependency_directory()
    {
        string path = Path.Combine(m_Directory, "Game.dll");
        WriteAssembly(path, 3);
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        Assert.Equal(-1, ScriptInstanceManager.LoadUserAssemblyPath(Path.Combine(m_Directory, "Other", "Game.dll")));
        Assert.Equal(m_Directory, ScriptInstanceManager.UserAssemblyDirectory);
        Assert.Equal(3, ReadVersion());
    }

    [Fact]
    public void Successful_replacement_releases_previous_collectible_context()
    {
        WeakReference previous = LoadThenReplace(Path.Combine(m_Directory, "Game.dll"));
        for (int attempt = 0; attempt < 10 && previous.IsAlive; ++attempt)
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
        }
        Assert.False(previous.IsAlive);
        Assert.Equal(2, ReadVersion());
    }

    [Fact]
    public void Missing_field_dependency_rejects_candidate_before_retiring_previous_assembly()
    {
        string path = Path.Combine(m_Directory, "Game.dll");
        WriteAssembly(path, 5);
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        Assembly previous = ScriptInstanceManager.UserAssembly!;

        var dependency = new PersistedAssemblyBuilder(new AssemblyName("MissingDependency"), typeof(object).Assembly);
        Type dependencyType = dependency.DefineDynamicModule("MissingDependency")
            .DefineType("MissingValue", TypeAttributes.Public).CreateType()!;
        WriteAssembly(path, 9, dependencyType);

        Assert.Equal(-1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        Assert.Same(previous, ScriptInstanceManager.UserAssembly);
        Assert.Equal(5, ReadVersion());
        Assert.Equal(0, s_UnregisterCount);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference LoadThenReplace(string path)
    {
        WriteAssembly(path, 1);
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        var previous = new WeakReference(AssemblyLoadContext.GetLoadContext(ScriptInstanceManager.UserAssembly!));
        byte[] name = System.Text.Encoding.UTF8.GetBytes("ReloadScript\0");
        fixed (byte* pointer = name)
        {
            delegate* unmanaged<byte*, byte*> fields = &ScriptInstanceManager.GetClassFieldDefs;
            Assert.Contains("VisibleValue", Marshal.PtrToStringUTF8((nint)fields(pointer)));
        }
        WriteAssembly(path, 2);
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        return previous;
    }

    private static int ReadVersion()
        => (int)ScriptInstanceManager.UserAssembly!.GetType("ReloadScript")!.GetMethod("Version")!.Invoke(null, null)!;

    [Fact]
    public void ResetMethodsAreRegisteredBeforeStartAndReplacedOnReload()
    {
        string path = Path.Combine(m_Directory, "Game.dll");
        WriteAssembly(path, 1);
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        ScriptInstanceManager.ResetRegisteredPlayState();
        ScriptInstanceManager.ResetRegisteredPlayState();
        Assert.Equal(2, ScriptInstanceManager.UserAssembly!.GetType("ReloadScript")!.GetField("ResetVisits")!.GetValue(null));
        WriteAssembly(path, 2);
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        ScriptInstanceManager.ResetRegisteredPlayState();
        Assert.Equal(1, ScriptInstanceManager.UserAssembly!.GetType("ReloadScript")!.GetField("ResetVisits")!.GetValue(null));
    }

    [Theory]
    [InlineData("reorder")]
    [InlineData("size")]
    [InlineData("type")]
    public void Native_layout_change_is_refused_before_registrations_retire_and_can_retry(string change)
    {
        string path = Path.Combine(m_Directory, "Game.dll");
        WriteAssembly(path, 1, nativeLayout: "matching");
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        Assembly previous = ScriptInstanceManager.UserAssembly!;
        WriteAssembly(path, 2, nativeLayout: change);
        var error = Assert.Throws<InvalidOperationException>(() => ScriptInstanceManager.PrepareUserAssemblyPath(path));
        Assert.Contains("Layout mismatch", error.Message);
        Assert.Contains("FunkeCodegenEnabled=true", error.Message);
        Assert.Contains("restart the editor", error.Message);
        Assert.Same(previous, ScriptInstanceManager.UserAssembly);
        Assert.Equal(0, s_UnregisterCount);
        Assert.Equal(-1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        Assert.Equal(1, ReadVersion());
        WriteAssembly(path, 3, nativeLayout: "matching");
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        Assert.Equal(3, ReadVersion());
        Assert.Equal(1, s_UnregisterCount);
    }

    [Fact]
    public void Prepared_candidate_is_the_validated_snapshot_even_if_disk_changes_before_swap()
    {
        string path = Path.Combine(m_Directory, "Game.dll");
        WriteAssembly(path, 1, nativeLayout: "matching");
        ScriptInstanceManager.PrepareUserAssemblyPath(path);
        WriteAssembly(path, 2, nativeLayout: "reorder");
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        Assert.Equal(1, ReadVersion());
    }

    [Fact]
    public void Replaced_script_core_requires_restart_even_if_assembly_version_is_unchanged()
    {
        string path = Path.Combine(m_Directory, "Game.dll");
        WriteAssembly(path, 1);
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        string corePath = Path.Combine(m_Directory, "Funke-ScriptCore.dll");
        var core = new PersistedAssemblyBuilder(typeof(EntityScript).Assembly.GetName(), typeof(object).Assembly);
        core.DefineDynamicModule("Funke-ScriptCore");
        core.Save(corePath);
        var error = Assert.Throws<InvalidOperationException>(() => ScriptInstanceManager.PrepareUserAssemblyPath(path));
        Assert.Contains("Funke-ScriptCore changed on disk", error.Message);
        Assert.Contains("restart the editor", error.Message);
        Assert.Equal(0, s_UnregisterCount);
        Assert.Equal(1, ReadVersion());
        File.Delete(corePath);
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
    }

    [Fact]
    public void Dynamic_component_reorder_and_size_changes_still_reload()
    {
        string path = Path.Combine(m_Directory, "Game.dll");
        WriteAssembly(path, 1, nativeLayout: "dynamic");
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        Assert.Equal(8u, s_RegisteredSize);
        WriteAssembly(path, 2, nativeLayout: "dynamic-reorder");
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        Assert.Equal(8u, s_RegisteredSize);
        WriteAssembly(path, 3, nativeLayout: "dynamic-size");
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        Assert.Equal(12u, s_RegisteredSize);
        Assert.Equal(2, s_UnregisterCount);
    }

    [Fact]
    public void Class_schema_does_not_invoke_a_user_constructor()
    {
        string path = Path.Combine(m_Directory, "Game.dll");
        var assembly = new PersistedAssemblyBuilder(new AssemblyName("MetadataGame"), typeof(object).Assembly);
        var script = assembly.DefineDynamicModule("MetadataGame").DefineType("MetadataScript", TypeAttributes.Public, typeof(EntityScript));
        script.DefineField("VisibleValue", typeof(int), FieldAttributes.Public);
        var body = script.DefineConstructor(MethodAttributes.Public, CallingConventions.Standard, Type.EmptyTypes).GetILGenerator();
        body.Emit(OpCodes.Newobj, typeof(InvalidOperationException).GetConstructor(Type.EmptyTypes)!);
        body.Emit(OpCodes.Throw);
        script.CreateType();
        assembly.Save(path);
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        byte[] name = System.Text.Encoding.UTF8.GetBytes("schema:MetadataScript\0");
        fixed (byte* pointer = name)
        {
            delegate* unmanaged<byte*, byte*> fields = &ScriptInstanceManager.GetClassFieldDefs;
            Assert.Contains("VisibleValue", Marshal.PtrToStringUTF8((nint)fields(pointer)));
        }
    }

    [Fact]
    public void DiscardedPreflightReleasesItsCollectibleContext()
    {
        string path = Path.Combine(m_Directory, "Game.dll");
        WriteAssembly(path, 1);
        WeakReference candidate = PrepareAndDiscard(path);
        for (int attempt = 0; attempt < 10 && candidate.IsAlive; ++attempt) {
            GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        }
        Assert.False(candidate.IsAlive);
        Assert.Null(ScriptInstanceManager.UserAssembly);
        Assert.Equal("[]", Metadata("catalog:"));
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference PrepareAndDiscard(string path)
    {
        ScriptInstanceManager.PrepareUserAssemblyPath(path);
        var candidate = new WeakReference(typeof(ScriptInstanceManager)
            .GetField("s_PreparedContext", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null));
        ScriptInstanceManager.DiscardPreparedAssembly();
        return candidate;
    }

    private static string Metadata(string query)
    {
        byte[] bytes = System.Text.Encoding.UTF8.GetBytes(query + "\0");
        fixed (byte* pointer = bytes) {
            delegate* unmanaged<byte*, byte*> get = &ScriptInstanceManager.GetClassFieldDefs;
            return Marshal.PtrToStringUTF8((nint)get(pointer))!;
        }
    }

    [Fact]
    public void CandidateCatalogPublishesOnlyAtCommitAndDiscardReleasesCandidate()
    {
        string path = Path.Combine(m_Directory, "Game.dll");
        WriteAssembly(path, 1);
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        string active = Metadata("catalog:");
        Assert.Contains("type:ReloadScript", active);
        WriteAssembly(path, 2, nativeLayout: "dynamic");
        ScriptInstanceManager.PrepareUserAssemblyPath(path);
        Assert.Equal(active, Metadata("catalog:"));
        ScriptInstanceManager.DiscardPreparedAssembly();
        Assert.Equal(active, Metadata("catalog:"));
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        Assert.Contains("type:NativeReloadValue", Metadata("catalog:"));
        string committed = Metadata("catalog:");
        File.WriteAllText(path, "bad");
        Assert.Equal(-1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        Assert.Equal(committed, Metadata("catalog:"));
    }

    [Fact]
    public void RejectedCandidateLeavesLiveInstanceAndLifecycleUntouched()
    {
        string path = Path.Combine(m_Directory, "Game.dll");
        WriteAssembly(path, 1);
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        byte[] name = System.Text.Encoding.UTF8.GetBytes("type:ReloadScript\0");
        delegate* unmanaged<byte*, ulong, int> create = &ScriptInstanceManager.CreateScriptInstance;
        delegate* unmanaged<int, void> start = &ScriptInstanceManager.InvokeStart;
        delegate* unmanaged<int, void> destroy = &ScriptInstanceManager.InvokeOnDestroy;
        delegate* unmanaged<int, void> release = &ScriptInstanceManager.DestroyScriptInstance;
        int handle;
        fixed (byte* p = name) handle = create(p, 42);
        Assert.NotEqual(0, handle); start(handle);
        object original = ScriptInstanceManager.GetScriptInstance(42, "type:ReloadScript")!;
        Assert.Equal(1, original.GetType().GetField("Starts")!.GetValue(original));
        File.WriteAllText(path, "bad");
        Assert.ThrowsAny<Exception>(() => ScriptInstanceManager.PrepareUserAssemblyPath(path));
        Assert.Same(original, ScriptInstanceManager.GetScriptInstance(42, "type:ReloadScript"));
        Assert.Equal(1, original.GetType().GetField("Starts")!.GetValue(original));
        Assert.Equal(0, original.GetType().GetField("Destroys")!.GetValue(original));
        WriteAssembly(path, 2);
        ScriptInstanceManager.PrepareUserAssemblyPath(path);
        destroy(handle); release(handle);
        Assert.Equal(1, original.GetType().GetField("Destroys")!.GetValue(original));
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        fixed (byte* p = name) handle = create(p, 42);
        start(handle); start(handle);
        object replacement = ScriptInstanceManager.GetScriptInstance(42, "type:ReloadScript")!;
        Assert.NotSame(original, replacement);
        Assert.Equal(1, replacement.GetType().GetField("Starts")!.GetValue(replacement));
        destroy(handle); release(handle);
    }

    private static void WriteAssembly(string path, int version, Type? fieldType = null, string? nativeLayout = null)
    {
        var assembly = new PersistedAssemblyBuilder(new AssemblyName("ReloadGame"), typeof(object).Assembly);
        ModuleBuilder module = assembly.DefineDynamicModule("ReloadGame");
        if (nativeLayout != null)
        {
            TypeBuilder component = module.DefineType("NativeReloadValue", TypeAttributes.Public | TypeAttributes.SequentialLayout | TypeAttributes.Sealed,
                typeof(ValueType), new[] { typeof(IComponent) });
            if (!nativeLayout.StartsWith("dynamic", StringComparison.Ordinal))
                component.DefineField("NativeName", typeof(string), FieldAttributes.Public | FieldAttributes.Static | FieldAttributes.Literal).SetConstant("Reload Value");
            string first = nativeLayout.EndsWith("reorder", StringComparison.Ordinal) ? "Right" : "Left";
            string second = nativeLayout.EndsWith("reorder", StringComparison.Ordinal) ? "Left" : "Right";
            component.DefineField(first, nativeLayout == "type" ? typeof(int) : typeof(float), FieldAttributes.Public);
            component.DefineField(second, typeof(float), FieldAttributes.Public);
            if (nativeLayout.EndsWith("size", StringComparison.Ordinal)) component.DefineField("Extra", typeof(float), FieldAttributes.Public);
            component.CreateType();
        }
        TypeBuilder script = module.DefineType("ReloadScript", TypeAttributes.Public, typeof(EntityScript));
        script.DefineDefaultConstructor(MethodAttributes.Public);
        script.DefineField("VisibleValue", typeof(int), FieldAttributes.Public);
        foreach (var pair in new[] { ("OnStart", "Starts"), ("OnDestroy", "Destroys") }) {
            FieldBuilder counter = script.DefineField(pair.Item2, typeof(int), FieldAttributes.Public);
            MethodBuilder method = script.DefineMethod(pair.Item1, MethodAttributes.Public | MethodAttributes.Virtual, typeof(void), Type.EmptyTypes);
            ILGenerator il = method.GetILGenerator();
            il.Emit(OpCodes.Ldarg_0); il.Emit(OpCodes.Dup); il.Emit(OpCodes.Ldfld, counter);
            il.Emit(OpCodes.Ldc_I4_1); il.Emit(OpCodes.Add); il.Emit(OpCodes.Stfld, counter); il.Emit(OpCodes.Ret);
            script.DefineMethodOverride(method, typeof(EntityScript).GetMethod(pair.Item1)!);
        }
        FieldBuilder visits = script.DefineField("ResetVisits", typeof(int), FieldAttributes.Public | FieldAttributes.Static);
        MethodBuilder reset = script.DefineMethod("ResetSession", MethodAttributes.Public | MethodAttributes.Static, typeof(void), Type.EmptyTypes);
        reset.SetCustomAttribute(new CustomAttributeBuilder(typeof(ResetOnPlayAttribute).GetConstructor(Type.EmptyTypes)!, Array.Empty<object>()));
        ILGenerator resetBody = reset.GetILGenerator();
        resetBody.Emit(OpCodes.Ldsfld, visits);
        resetBody.Emit(OpCodes.Ldc_I4_1);
        resetBody.Emit(OpCodes.Add);
        resetBody.Emit(OpCodes.Stsfld, visits);
        resetBody.Emit(OpCodes.Ret);
        if (fieldType != null)
            script.DefineField("Value", fieldType, FieldAttributes.Public);
        ILGenerator body = script.DefineMethod("Version", MethodAttributes.Public | MethodAttributes.Static,
            typeof(int), Type.EmptyTypes).GetILGenerator();
        body.Emit(OpCodes.Ldc_I4, version);
        body.Emit(OpCodes.Ret);
        script.CreateType();
        assembly.Save(path);
    }
}
