using System;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

// Stand-ins for what a package ships; never instantiated (no native bindings here).
public class PackageTestScript : EntityScript { }
public class PackageTestComponent : Component { }
public class PackageTestSceneSystem : SceneSystem { }
public class PackageTestGlobalSystem : GlobalSystem { }
public abstract class PackageTestAbstractScript : EntityScript { }
public class PackageTestGenericScript<T> : EntityScript { }
public class PackageTestNoDefaultCtorScript : EntityScript { public PackageTestNoDefaultCtorScript(int value) { } }
internal class PackageTestInternalScript : EntityScript { }
public class PackageTestPlainClass { }

[Collection("PackageScriptRegistry")]
public class PackageScriptRegistryTests : IDisposable
{
    public PackageScriptRegistryTests()
    {
        PackageScriptRegistry.Clear();
        PackageScriptRegistry.AssemblySource = () => new[] { typeof(PackageScriptRegistryTests).Assembly };
    }

    public void Dispose()
    {
        PackageScriptRegistry.Clear();
        PackageScriptRegistry.AssemblySource = null;
    }

    [Fact]
    public void Lists_scripts_and_systems_by_full_name_and_components_by_class_name()
    {
        string json = PackageScriptRegistry.BuildTypesJson();
        Assert.Contains("{\"name\":\"FunkeScriptCoreTests.PackageTestScript\",\"kind\":\"script\",\"assembly\":\"Funke-ScriptCore-Tests\"}", json);
        Assert.Contains("{\"name\":\"PackageTestComponent\",\"kind\":\"component\",", json);
        Assert.Contains("{\"name\":\"FunkeScriptCoreTests.PackageTestSceneSystem\",\"kind\":\"sceneSystem\",", json);
        Assert.Contains("{\"name\":\"FunkeScriptCoreTests.PackageTestGlobalSystem\",\"kind\":\"globalSystem\",", json);
    }

    [Theory]
    [InlineData(typeof(PackageTestAbstractScript))]
    [InlineData(typeof(PackageTestGenericScript<>))]
    [InlineData(typeof(PackageTestNoDefaultCtorScript))]
    [InlineData(typeof(PackageTestInternalScript))]
    [InlineData(typeof(PackageTestPlainClass))]
    public void Skips_types_a_scene_cannot_create(Type type)
    {
        Assert.Null(PackageScriptRegistry.KindOf(type));
        Assert.DoesNotContain(type.Name.Split('`')[0] + "\"", PackageScriptRegistry.BuildTypesJson());
    }

    [Fact]
    public void Caches_until_cleared()
    {
        string first = PackageScriptRegistry.BuildTypesJson();
        PackageScriptRegistry.AssemblySource = () => Array.Empty<System.Reflection.Assembly>();
        Assert.Same(first, PackageScriptRegistry.BuildTypesJson());
        PackageScriptRegistry.Clear();
        Assert.Equal("[]", PackageScriptRegistry.BuildTypesJson());
    }
}
