using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.JavaScript;
using System.Text;
using System.Text.Json;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using System.Runtime.Versioning;

[SupportedOSPlatform("browser")]
public static partial class BrowserEditor
{
    [DllImport("FunkeWeb", EntryPoint = "FunkeWeb_Start")]
    private static extern int StartNative();
    [DllImport("FunkeWeb", EntryPoint = "FunkeWebEditorState")]
    private static extern IntPtr StateNative();
    [DllImport("FunkeWeb", EntryPoint = "FunkeWebEditorReloadScripts")]
    private static extern IntPtr ReloadNative();

    private static MetadataReference[]? s_References;

    public static int Main()
    {
        AppContext.SetSwitch("Funke.Scripting.CollectibleUserAssembly", false);
        return StartNative();
    }

    [JSExport]
    public static string Compile(bool release)
    {
        try
        {
            using var state = JsonDocument.Parse(Marshal.PtrToStringUTF8(StateNative())!);
            if (state.RootElement.GetProperty("playing").GetBoolean())
                throw new InvalidOperationException("Bitte zuerst den Play-Modus beenden.");
            string root = state.RootElement.GetProperty("projectRoot").GetString()!;
            string output = release ? "/tmp/FunkeExport.dll" : state.RootElement.GetProperty("assemblyPath").GetString()!;
            var files = Directory.GetFiles(Path.Combine(root, "Assets"), "*.cs", SearchOption.AllDirectories);
            s_References ??= Directory.GetFiles("/references", "*.dll").Select(path => MetadataReference.CreateFromFile(path)).ToArray();
            string[] defines = release
                ? ["FUNKE_BUILD", "FUNKE_BUILD_RELEASE", "FUNKE_WEB", "FNK_DIST"]
                : ["FUNKE_EDITOR", "FUNKE_BUILD_DEVELOPMENT", "FUNKE_WEB", "FNK_RELEASE"];
            var parseOptions = new CSharpParseOptions(LanguageVersion.Latest, preprocessorSymbols: defines);
            var trees = files.Select(path => CSharpSyntaxTree.ParseText(File.ReadAllText(path), parseOptions,
                Path.GetRelativePath(root, path).Replace('\\', '/'), Encoding.UTF8));
            // Distinct identities allow browser Mono to load an edited assembly without a page reload.
            var compilation = CSharpCompilation.Create("FunkeGame_" + Guid.NewGuid().ToString("N"), trees, s_References,
                new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, optimizationLevel: OptimizationLevel.Release,
                    allowUnsafe: true, concurrentBuild: false, nullableContextOptions: NullableContextOptions.Enable));
            using var dll = new MemoryStream();
            // Roslyn's portable-PDB writer waits on a Task even without concurrentBuild;
            // the browser runtime cannot block a thread. Diagnostics still retain source positions.
            var result = compilation.Emit(dll);
            var diagnostics = result.Diagnostics.Where(d => d.Severity >= DiagnosticSeverity.Warning).Select(d => new {
                severity = d.Severity.ToString(), code = d.Id, message = d.GetMessage(),
                file = d.Location.GetLineSpan().Path,
                line = d.Location.GetLineSpan().StartLinePosition.Line + 1,
                column = d.Location.GetLineSpan().StartLinePosition.Character + 1
            }).ToArray();
            if (!result.Success) return JsonSerializer.Serialize(new { success = false, diagnostics });
            Directory.CreateDirectory(Path.GetDirectoryName(output)!);
            byte[]? previous = File.Exists(output) ? File.ReadAllBytes(output) : null;
            File.WriteAllBytes(output, dll.ToArray());
            File.Delete(Path.ChangeExtension(output, ".pdb"));
            string error = release ? "" : Marshal.PtrToStringUTF8(ReloadNative()) ?? "";
            if (error.Length != 0)
            {
                if (previous != null) File.WriteAllBytes(output, previous); else File.Delete(output);
                return JsonSerializer.Serialize(new { success = false, error, diagnostics });
            }
            return JsonSerializer.Serialize(new { success = true, output, diagnostics, files = files.Length });
        }
        catch (Exception error) { return JsonSerializer.Serialize(new { success = false, error = error.ToString() }); }
    }
}
