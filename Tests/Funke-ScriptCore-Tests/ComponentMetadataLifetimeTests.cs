using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using Funke;
using Xunit;

namespace FunkeScriptCoreTests;

public sealed class CollectibleMetadataComponent : Component { }

public sealed class ComponentMetadataLifetimeTests
{
    private sealed class ComponentLoadContext() : AssemblyLoadContext(isCollectible: true)
    {
        protected override Assembly? Load(AssemblyName name)
            => name.Name == typeof(Entity).Assembly.GetName().Name ? typeof(Entity).Assembly : null;
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void ComponentMetadataDoesNotKeepUnloadedScriptContextsAlive(bool registerNative)
    {
        WeakReference[] contexts = Enumerable.Range(0, 10)
            .Select(_ => LoadAndUnload(registerNative)).ToArray();

        for (int attempt = 0; attempt < 10 && contexts.Any(context => context.IsAlive); ++attempt)
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
        }

        Assert.All(contexts, context => Assert.False(context.IsAlive));
        Assert.True(Entity.TryGetNativeComponentName(typeof(Transform2D), out string? nativeName));
        Assert.Equal("Transform 2D", nativeName);
        Assert.Same(typeof(Transform2D), FindRegisteredComponent("Transform 2D"));
    }

    [Fact]
    public void NativeRegistrationSurvivesGcAndRetiresBeforeAReplacementContextRegisters()
    {
        var original = new ComponentLoadContext();
        var replacement = new ComponentLoadContext();
        string nativeName = "Reloaded " + Guid.NewGuid().ToString("N");
        try
        {
            LoadAndRegister(original, nativeName);
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
            Assert.Same(original, RegisteredContext(nativeName));

            original.Unload();
            Assert.Null(FindRegisteredComponent(nativeName));

            LoadAndRegister(replacement, nativeName);
            Assert.Same(replacement, RegisteredContext(nativeName));
            replacement.Unload();
            Assert.Null(FindRegisteredComponent(nativeName));
        }
        finally
        {
            original.Unload();
            replacement.Unload();
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void LoadAndRegister(ComponentLoadContext context, string nativeName)
    {
        using var stream = File.OpenRead(typeof(ComponentMetadataLifetimeTests).Assembly.Location);
        Assembly assembly = context.LoadFromStream(stream);
        Type component = assembly.GetType(typeof(CollectibleMetadataComponent).FullName!)!;
        typeof(Entity).GetMethod(nameof(Entity.RegisterNativeComponent))!.MakeGenericMethod(component)
            .Invoke(null, new object[] { nativeName });
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static AssemblyLoadContext? RegisteredContext(string nativeName)
    {
        Type? component = FindRegisteredComponent(nativeName);
        return component != null ? AssemblyLoadContext.GetLoadContext(component.Assembly) : null;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference LoadAndUnload(bool registerNative)
    {
        var context = new ComponentLoadContext();
        using var stream = File.OpenRead(typeof(ComponentMetadataLifetimeTests).Assembly.Location);
        Assembly assembly = context.LoadFromStream(stream);
        Type component = assembly.GetType(typeof(CollectibleMetadataComponent).FullName!)!;
        string nativeName = "Collectible " + Guid.NewGuid().ToString("N");

        try
        {
            Assert.False(Entity.TryGetNativeComponentName(component, out _));
            Assert.False(Entity.TryGetNativeComponentName(component, out _));
            if (registerNative)
            {
                MethodInfo register = typeof(Entity).GetMethod(nameof(Entity.RegisterNativeComponent))!
                    .MakeGenericMethod(component);
                register.Invoke(null, new object[] { nativeName + " old" });
                register.Invoke(null, new object[] { nativeName });
                Assert.True(Entity.TryGetNativeComponentName(component, out string? registeredName));
                Assert.Equal(nativeName, registeredName);
                Assert.Same(component, FindRegisteredComponent(nativeName));
                Assert.Null(FindRegisteredComponent(nativeName + " old"));
            }

            return new WeakReference(context);
        }
        finally
        {
            context.Unload();
        }
    }

    private static Type? FindRegisteredComponent(string name)
        => (Type?)typeof(Entity).GetMethod("FindRegisteredComponentType", BindingFlags.NonPublic | BindingFlags.Static)!
            .Invoke(null, new object[] { name });
}
