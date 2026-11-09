using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.InteropServices;
using System.Text.Json;
using Funke;
using Funke.Interop;
using Xunit;

namespace CatalogEnemies { public class Controller : EntityScript { public override void OnUpdate() { } } }
namespace CatalogUI { public class Controller : CatalogEnemies.Controller { public override void OnStart() { } } }

namespace FunkeScriptCoreTests
{
    [Collection(NativeCallbacksCollection.Name)]
    public unsafe class TypeIdentityTests
    {
        [UnmanagedCallersOnly] private static int Size(byte* _) => 0;

        [Fact]
        public void CatalogUsesActualConstructibleTypesAndPortablePdbSources()
        {
            var previous = NativeCallbacks.Bindings;
            NativeCallbacks.Bindings.Entity_GetComponentSize = &Size;
            try {
                Assembly assembly = typeof(TypeIdentityTests).Assembly;
                using var symbols = File.OpenRead(Path.ChangeExtension(assembly.Location, ".pdb"));
                using var json = JsonDocument.Parse(PackageScriptRegistry.BuildProjectTypesJson(assembly, symbols));
                var rows = json.RootElement.EnumerateArray().ToArray();
                var enemy = rows.Single(r => r.GetProperty("name").GetString() == "type:CatalogEnemies.Controller");
                var ui = rows.Single(r => r.GetProperty("name").GetString() == "type:CatalogUI.Controller");
                Assert.Equal("script", ui.GetProperty("kind").GetString());
                Assert.EndsWith("TypeIdentityTests.cs", enemy.GetProperty("path").GetString());
                Assert.Equal(enemy.GetProperty("path").GetString(), ui.GetProperty("path").GetString());
                Assert.DoesNotContain(rows, r => r.GetProperty("name").GetString()!.Contains(nameof(PackageTestPlainClass)));
                Assert.DoesNotContain(rows, r => r.GetProperty("name").GetString()!.Contains(nameof(PackageTestAbstractScript)));
                Assert.DoesNotContain(rows, r => r.GetProperty("name").GetString()!.Contains("PackageTestGenericScript"));
                Assert.DoesNotContain(rows, r => r.GetProperty("name").GetString()!.Contains(nameof(PackageTestNoDefaultCtorScript)));
            }
            finally { NativeCallbacks.Bindings = previous; }
        }

        [Fact]
        public void ExactCanonicalNamesAndAmbiguousLegacyNamesNeverSelectTheFirstMatch()
        {
            var first = AssemblyBuilder.DefineDynamicAssembly(new AssemblyName("IdentityA"), AssemblyBuilderAccess.RunAndCollect);
            var module = first.DefineDynamicModule("IdentityA");
            Type global = module.DefineType("Player", TypeAttributes.Public, typeof(EntityScript)).CreateType()!;
            Type preferred = module.DefineType("Funke.Player", TypeAttributes.Public, typeof(EntityScript)).CreateType()!;
            Assert.Null(ScriptInstanceManager.ResolveTypeIdentity("Player", new[] { first }));
            Assert.Same(global, ScriptInstanceManager.ResolveTypeIdentity("type:Player", new[] { first }));
            Assert.Same(preferred, ScriptInstanceManager.ResolveTypeIdentity("type:Funke.Player", new[] { first }));
            var second = AssemblyBuilder.DefineDynamicAssembly(new AssemblyName("IdentityB"), AssemblyBuilderAccess.RunAndCollect);
            second.DefineDynamicModule("IdentityB").DefineType("Player", TypeAttributes.Public, typeof(EntityScript)).CreateType();
            Assert.Null(ScriptInstanceManager.ResolveTypeIdentity("type:Player", new Assembly[] { first, second }));
            var test = typeof(TypeIdentityTests).Assembly;
            Assert.Null(ScriptInstanceManager.ResolveTypeIdentity("Controller", new[] { test }));
            Assert.Equal(typeof(CatalogUI.Controller), ScriptInstanceManager.ResolveTypeIdentity("type:CatalogUI.Controller", new[] { test }));
        }
    }
}
