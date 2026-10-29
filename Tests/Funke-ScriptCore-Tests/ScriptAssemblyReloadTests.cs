using System;
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe sealed class ScriptAssemblyReloadTests : IDisposable
{
    private readonly NativeBindingsStruct m_PreviousBindings = NativeCallbacks.Bindings;
    private readonly string m_Directory = Path.Combine(Path.GetTempPath(), "FunkeReload-" + Guid.NewGuid().ToString("N"));
    private static int s_UnregisterCount;

    public ScriptAssemblyReloadTests()
    {
        Directory.CreateDirectory(m_Directory);
        NativeCallbacks.Bindings.Component_UnregisterAllDynamic = &Unregister;
        ScriptInstanceManager.SetCoreAssembly(typeof(EntityScript).Assembly);
        s_UnregisterCount = 0;
    }

    [UnmanagedCallersOnly]
    private static void Unregister() => ++s_UnregisterCount;

    public void Dispose()
    {
        delegate* unmanaged<void> unload = &ScriptInstanceManager.UnloadUserAssembly;
        unload();
        NativeCallbacks.Bindings = m_PreviousBindings;
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
        WriteAssembly(path, 2);
        Assert.Equal(1, ScriptInstanceManager.LoadUserAssemblyPath(path));
        return previous;
    }

    private static int ReadVersion()
        => (int)ScriptInstanceManager.UserAssembly!.GetType("ReloadScript")!.GetMethod("Version")!.Invoke(null, null)!;

    private static void WriteAssembly(string path, int version, Type? fieldType = null)
    {
        var assembly = new PersistedAssemblyBuilder(new AssemblyName("ReloadGame"), typeof(object).Assembly);
        ModuleBuilder module = assembly.DefineDynamicModule("ReloadGame");
        TypeBuilder script = module.DefineType("ReloadScript", TypeAttributes.Public, typeof(EntityScript));
        script.DefineDefaultConstructor(MethodAttributes.Public);
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
