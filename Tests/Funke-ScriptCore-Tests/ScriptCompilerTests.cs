using System;
using System.Collections;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using Funke.ScriptCompiler;
using Xunit;

namespace Funke.Tests
{
    // End-to-end: real MSBuild evaluation + in-process Roslyn against throwaway projects in %TEMP%.
    public sealed class ScriptCompilerTests : IDisposable
    {
        const string k_Name = "TestScripts";
        readonly string m_Root = Path.Combine(Path.GetTempPath(), "FunkeScriptCompilerTests", Guid.NewGuid().ToString("N"));

        string ProjectPath => Path.Combine(m_Root, k_Name + ".csproj");
        string OutputPath => Path.Combine(m_Root, "bin", "Release", k_Name + ".dll");

        public void Dispose()
        {
            try { Directory.Delete(m_Root, recursive: true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        }

        [Fact]
        public void EditedScriptRecompilesWithoutReevaluation()
        {
            CreateProject();
            WriteScript("Player.cs", "public class Player { public int Speed => 1; }");

            var first = Compile();
            AssertStatus("compiled", first);
            Assert.True(first.Evaluated);
            Assert.Contains("Player", ReadTypeNames());

            WriteScript("Player.cs", "public class Player { public int Speed => 2; } public class Enemy { }");
            var second = Compile();
            AssertStatus("compiled", second);
            Assert.False(second.Evaluated);
            Assert.Contains("Enemy", ReadTypeNames());
        }

        [Fact]
        public void SyntaxErrorReportsCompilerDiagnostics()
        {
            CreateProject();
            WriteScript("Broken.cs", "public class Broken { int x = 1 }");

            var result = Compile();
            AssertStatus("failed", result);
            Assert.Contains("CS1002", result.Diagnostics);
            Assert.False(File.Exists(OutputPath));
        }

        [Fact]
        public void AddedScriptTriggersReevaluation()
        {
            CreateProject();
            WriteScript("A.cs", "public class A { }");
            AssertStatus("compiled", Compile());

            WriteScript(Path.Combine("Sub", "B.cs"), "public class B { }");
            var result = Compile();
            AssertStatus("compiled", result);
            Assert.True(result.Evaluated);
            Assert.Contains("B", ReadTypeNames());
        }

        [Fact]
        public void ProjectFileEditFallsBackToFullBuildOnce()
        {
            CreateProject();
            WriteScript("A.cs", "public class A { }");
            AssertStatus("compiled", Compile());

            CreateProject(extraProperties: "<LangVersion>latest</LangVersion>");
            var changed = Compile();
            AssertStatus("fallback", changed);
            Assert.Contains("project files changed", changed.Message);

            var next = Compile();
            AssertStatus("compiled", next);
            Assert.False(next.Evaluated);
        }

        [Fact]
        public void DefinesComeFromRequestProperties()
        {
            CreateProject();
            WriteScript("Gate.cs", "#if !FUNKE_EDITOR\n#error FUNKE_EDITOR missing\n#endif\npublic class Gate { }");

            AssertStatus("failed", Compile());
            AssertStatus("compiled", Compile("-p:DefineConstants=FUNKE_EDITOR%3BFUNKE_WINDOWS"));
        }

        [Fact]
        public void SourceGeneratorsRun()
        {
            CreateProject();
            // Without the regex generator this partial method has no implementation (CS8795).
            WriteScript("Patterns.cs", "using System.Text.RegularExpressions;\npublic static partial class Patterns { [GeneratedRegex(\"^[a-z]+$\")] public static partial Regex Word(); }");

            var result = Compile();
            AssertStatus("compiled", result);
        }

        [Fact]
        public void MissingCopyLocalDependencyFallsBackUntilDeployed()
        {
            string library = Path.Combine(m_Root, "Packages", "Lib", "Lib.dll");
            BuildLibrary(library);
            CreateProject(extraItems: $"<Reference Include=\"Lib\"><HintPath>{library}</HintPath><Private>true</Private></Reference>");
            WriteScript("UsesLib.cs", "public class UsesLib { public object Value = new Lib.Thing(); }");

            var first = Compile();
            AssertStatus("fallback", first);
            Assert.Contains("Lib.dll", first.Message);

            Directory.CreateDirectory(Path.GetDirectoryName(OutputPath)!);
            File.Copy(library, Path.Combine(Path.GetDirectoryName(OutputPath)!, "Lib.dll"));
            AssertStatus("compiled", Compile());
        }

        [Fact]
        public void OutputPathMismatchFallsBack()
        {
            CreateProject();
            WriteScript("A.cs", "public class A { }");
            var result = Compile(outputPath: Path.Combine(m_Root, "elsewhere", "Other.dll"));
            AssertStatus("fallback", result);
        }

        [Fact]
        public void RebuiltDependencyMetadataIsReleasedAfterItsSessionsAreReleased()
        {
            InitializeCompiler();
            WeakReference[] versions = LoadDependencyVersions();
            CollectMetadata();
            Assert.All(versions.Take(versions.Length - 1), version => Assert.False(version.IsAlive));
            Assert.True(versions[^1].IsAlive);
        }

        [Fact]
        public void ReferenceCacheHandlesLengthChangesAliasesAndConcurrentSessions()
        {
            InitializeCompiler();
            string dependency = Path.Combine(m_Root, "ChangingDependency.dll");
            DateTime stamp = DateTime.UtcNow;
            ReplaceDependency(dependency, typeof(ScriptCompilerTests).Assembly.Location, stamp);
            object first = CreateReferenceSession(dependency);
            Assert.Equal(typeof(ScriptCompilerTests).Assembly.GetName().Name, ReferenceAssemblyName(SessionReference(first)));
            long originalLength = new FileInfo(dependency).Length;

            ReplaceDependency(dependency, typeof(Funke.Entity).Assembly.Location, stamp);
            Assert.NotEqual(originalLength, new FileInfo(dependency).Length);
            object aliased = SessionReference(CreateReferenceSession(dependency, "Library"));
            Assert.Equal(typeof(Funke.Entity).Assembly.GetName().Name, ReferenceAssemblyName(aliased));
            object properties = aliased.GetType().GetProperty("Properties")!.GetValue(aliased)!;
            var aliases = (IEnumerable)properties.GetType().GetProperty("Aliases")!.GetValue(properties)!;
            Assert.Equal(new[] { "Library" }, aliases.Cast<string>());

            var references = new object[24];
            Parallel.For(0, references.Length, i => references[i] = SessionReference(CreateReferenceSession(dependency)));
            Assert.All(references, reference => Assert.Same(references[0], reference));
            Assert.Equal(typeof(ScriptCompilerTests).Assembly.GetName().Name, ReferenceAssemblyName(SessionReference(first)));
            GC.KeepAlive(first);
        }

        void InitializeCompiler()
        {
            CreateProject();
            WriteScript("Bootstrap.cs", "public class Bootstrap { }");
            AssertStatus("compiled", Compile());
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        WeakReference[] LoadDependencyVersions()
        {
            string dependency = Path.Combine(m_Root, "RebuiltDependency.dll");
            DateTime stamp = DateTime.UtcNow;
            ReplaceDependency(dependency, typeof(ScriptCompilerTests).Assembly.Location, stamp);
            object first = CreateReferenceSession(dependency);
            Assert.Equal(typeof(ScriptCompilerTests).Assembly.GetName().Name, ReferenceAssemblyName(SessionReference(first)));
            var versions = new WeakReference[10];
            versions[0] = new WeakReference(SessionReference(first));
            for (int i = 1; i < versions.Length; ++i)
                versions[i] = LoadDependencyVersion(dependency, stamp.AddSeconds(i), i % 2 == 0);

            CollectMetadata();
            Assert.True(versions[0].IsAlive);
            Assert.All(versions.Skip(1).Take(versions.Length - 2), version => Assert.False(version.IsAlive));
            Assert.Equal(typeof(ScriptCompilerTests).Assembly.GetName().Name, ReferenceAssemblyName(SessionReference(first)));
            GC.KeepAlive(first);
            return versions;
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        WeakReference LoadDependencyVersion(string dependency, DateTime stamp, bool useTestsAssembly)
        {
            Assembly assembly = useTestsAssembly ? typeof(ScriptCompilerTests).Assembly : typeof(Funke.Entity).Assembly;
            ReplaceDependency(dependency, assembly.Location, stamp);
            object reference = SessionReference(CreateReferenceSession(dependency));
            Assert.Equal(assembly.GetName().Name, ReferenceAssemblyName(reference));
            return new WeakReference(reference);
        }

        static void ReplaceDependency(string path, string source, DateTime stamp)
        {
            File.Copy(source, path + ".new", overwrite: true);
            File.SetLastWriteTimeUtc(path + ".new", stamp);
            File.Move(path + ".new", path, overwrite: true);
        }

        object CreateReferenceSession(string dependency, string? alias = null)
        {
            Assembly compiler = typeof(CompilerHost).Assembly;
            Type evaluationType = compiler.GetType("Funke.ScriptCompiler.ProjectEvaluation")!;
            object evaluation = Activator.CreateInstance(evaluationType, nonPublic: true)!;
            evaluationType.GetProperty("ProjectDirectory")!.SetValue(evaluation, m_Root);
            string reference = alias == null ? "" : alias + "=";
            evaluationType.GetProperty("CscArguments")!.SetValue(evaluation,
                new[] { "/nostdlib", "/target:library", "/out:unused.dll", "/reference:" + reference + "\"" + dependency + "\"" });
            return Activator.CreateInstance(compiler.GetType("Funke.ScriptCompiler.RoslynSession")!, new[] { evaluation })!;
        }

        static object SessionReference(object session)
            => ((IEnumerable)session.GetType().GetField("m_References", BindingFlags.NonPublic | BindingFlags.Instance)!
                .GetValue(session)!).Cast<object>().Single();

        static string ReferenceAssemblyName(object reference)
        {
            using var metadata = (IDisposable)reference.GetType().GetMethod("GetMetadata", Type.EmptyTypes)!.Invoke(reference, null)!;
            var modules = (IEnumerable)metadata.GetType().GetMethod("GetModules")!.Invoke(metadata, null)!;
            object module = modules.Cast<object>().First();
            var reader = (MetadataReader)module.GetType().GetMethod("GetMetadataReader")!.Invoke(module, null)!;
            return reader.GetString(reader.GetAssemblyDefinition().Name);
        }

        static void CollectMetadata()
        {
            for (int i = 0; i < 4; ++i)
            {
                GC.Collect();
                GC.WaitForPendingFinalizers();
                GC.Collect();
            }
        }

        void CreateProject(string extraProperties = "", string extraItems = "")
        {
            Directory.CreateDirectory(m_Root);
            // Mirrors the user-project template in FunkeProject.cpp.
            File.WriteAllText(ProjectPath, $@"<Project Sdk=""Microsoft.NET.Sdk"">
  <PropertyGroup>
    <OutputType>Library</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <Configurations>Debug;Release;Dist</Configurations>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <EnableDynamicLoading>true</EnableDynamicLoading>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <EnableDefaultNoneItems>false</EnableDefaultNoneItems>
    <Nullable>enable</Nullable>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
    {extraProperties}
  </PropertyGroup>
  <PropertyGroup Condition=""'$(Configuration)' == 'Release'"">
    <OutputPath>bin\Release\</OutputPath>
    <Optimize>true</Optimize>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include=""Assets\**\*.cs"" />
    {extraItems}
  </ItemGroup>
</Project>
");
        }

        void WriteScript(string relativePath, string source)
        {
            string path = Path.Combine(m_Root, "Assets", relativePath);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, source);
        }

        string[] ReadTypeNames()
        {
            using var stream = File.OpenRead(OutputPath);
            using var pe = new PEReader(stream);
            MetadataReader reader = pe.GetMetadataReader();
            return reader.TypeDefinitions.Select(h => reader.GetString(reader.GetTypeDefinition(h).Name)).ToArray();
        }

        void BuildLibrary(string outputPath)
        {
            string directory = Path.Combine(m_Root, "LibSource");
            Directory.CreateDirectory(directory);
            File.WriteAllText(Path.Combine(directory, "Lib.csproj"),
                "<Project Sdk=\"Microsoft.NET.Sdk\"><PropertyGroup><TargetFramework>net10.0</TargetFramework></PropertyGroup></Project>");
            File.WriteAllText(Path.Combine(directory, "Thing.cs"), "namespace Lib { public class Thing { } }");
            var start = new ProcessStartInfo("dotnet") { RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false };
            foreach (string arg in new[] { "build", Path.Combine(directory, "Lib.csproj"), "-c", "Release", "-o", Path.GetDirectoryName(outputPath)!, "--nologo", "-v", "q" })
            {
                start.ArgumentList.Add(arg);
            }
            using var process = Process.Start(start)!;
            string output = process.StandardOutput.ReadToEnd() + process.StandardError.ReadToEnd();
            process.WaitForExit();
            Assert.True(process.ExitCode == 0, output);
        }

        static void AssertStatus(string expected, Response response) =>
            Assert.True(response.Status == expected, $"expected {expected}, got {response.Status}: {response.Message}\n{response.Diagnostics}");

        Response Compile(string? property = null, string? outputPath = null)
        {
            string request = JsonSerializer.Serialize(new
            {
                project = ProjectPath,
                configuration = "Release",
                properties = property == null ? new[] { "-p:FunkeCodegenEnabled=false" } : new[] { "-p:FunkeCodegenEnabled=false", property },
                outputPath = outputPath ?? OutputPath,
                dotnet = "dotnet",
            });
            return Invoke(request);
        }

        static unsafe Response Invoke(string request)
        {
            byte[] utf8 = Encoding.UTF8.GetBytes(request + "\0");
            fixed (byte* input = utf8)
            {
                delegate* unmanaged<byte*, byte*> compile = &CompilerHost.Compile;
                delegate* unmanaged<byte*, void> free = &CompilerHost.FreeResult;
                byte* output = compile(input);
                string json = Marshal.PtrToStringUTF8((nint)output)!;
                free(output);
                return JsonSerializer.Deserialize<Response>(json, new JsonSerializerOptions { PropertyNameCaseInsensitive = true })!;
            }
        }

        sealed class Response
        {
            public string Status { get; set; } = "";
            public string? Message { get; set; }
            public string? Diagnostics { get; set; }
            public bool Evaluated { get; set; }
            public double ElapsedMs { get; set; }
        }
    }
}
