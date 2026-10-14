using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Index.ScriptCompiler;
using Xunit;

namespace Index.Tests
{
    // End-to-end: real MSBuild evaluation + in-process Roslyn against throwaway projects in %TEMP%.
    public sealed class ScriptCompilerTests : IDisposable
    {
        const string k_Name = "TestScripts";
        readonly string m_Root = Path.Combine(Path.GetTempPath(), "IndexScriptCompilerTests", Guid.NewGuid().ToString("N"));

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
            WriteScript("Gate.cs", "#if !INDEX_EDITOR\n#error INDEX_EDITOR missing\n#endif\npublic class Gate { }");

            AssertStatus("failed", Compile());
            AssertStatus("compiled", Compile("-p:DefineConstants=INDEX_EDITOR%3BINDEX_WINDOWS"));
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

        void CreateProject(string extraProperties = "", string extraItems = "")
        {
            Directory.CreateDirectory(m_Root);
            // Mirrors the user-project template in IndexProject.cpp.
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
                properties = property == null ? new[] { "-p:IndexCodegenEnabled=false" } : new[] { "-p:IndexCodegenEnabled=false", property },
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
