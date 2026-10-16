using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Funke.ScriptCompiler;

// Native entry points. This type must not touch Roslyn: Roslyn is only resolvable after the project's SDK
// is known, so everything Roslyn-typed lives behind RoslynBridge's NoInlining calls.
public static unsafe class CompilerHost
{
    static readonly object s_Lock = new();
    static readonly Dictionary<string, ProjectState> s_Projects = new(StringComparer.OrdinalIgnoreCase);
    static string? s_RoslynDirectory;

    internal static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    // Returns a UTF-8 CompileResponse JSON the caller releases with FreeResult. Never throws.
    [UnmanagedCallersOnly]
    public static byte* Compile(byte* requestUtf8)
    {
        CompileResponse response;
        var stopwatch = Stopwatch.StartNew();
        try
        {
            var request = JsonSerializer.Deserialize<CompileRequest>(Marshal.PtrToStringUTF8((nint)requestUtf8) ?? "", JsonOptions)
                ?? throw new FallbackException("empty compile request");
            lock (s_Lock)
            {
                response = CompileLocked(request);
            }
        }
        catch (FallbackException e)
        {
            response = new CompileResponse { Status = "fallback", Message = e.Message };
        }
        catch (Exception e)
        {
            response = new CompileResponse { Status = "fallback", Message = $"{e.GetType().Name}: {e.Message}" };
        }
        response.ElapsedMs = stopwatch.Elapsed.TotalMilliseconds;
        return ToNativeUtf8(JsonSerializer.Serialize(response, JsonOptions));
    }

    [UnmanagedCallersOnly]
    public static void FreeResult(byte* result) => NativeMemory.Free(result);

    static CompileResponse CompileLocked(CompileRequest request)
    {
        string projectPath = Path.GetFullPath(request.Project);
        string fingerprint = request.Configuration + "\n" + string.Join("\n", request.Properties);
        s_Projects.TryGetValue(projectPath, out ProjectState? state);

        bool evaluated = false;
        string? staleReason = state == null ? null
            : state.Fingerprint != fingerprint ? "build settings changed"
            : state.Evaluation.FindStaleReason();
        if (state == null || staleReason != null)
        {
            var evaluation = ProjectEvaluation.Run(projectPath, request);
            state = new ProjectState(fingerprint, evaluation);
            s_Projects[projectPath] = state;
            evaluated = true;
            // Project-file edits can add copy-local dependencies or targets only MSBuild runs.
            if (staleReason != null && staleReason.StartsWith(ProjectEvaluation.ProjectFilesChanged, StringComparison.Ordinal))
            {
                throw new FallbackException(staleReason);
            }
        }

        if (!PathsEqual(state.Evaluation.TargetPath, request.OutputPath))
        {
            throw new FallbackException($"MSBuild output '{state.Evaluation.TargetPath}' differs from the editor's '{request.OutputPath}'");
        }
        if (state.Evaluation.FindMissingDependency() is string missing)
        {
            throw new FallbackException($"dependency not deployed yet: {missing}");
        }

        EnsureRoslyn(state.Evaluation.RoslynDirectory);
        state.Session ??= RoslynBridge.CreateSession(state.Evaluation);
        CompileResponse response = RoslynBridge.Compile(state.Session, state.Evaluation.TargetPath);
        response.Evaluated = evaluated;
        return response;
    }

    // Roslyn binds once per process; a second project on another SDK goes through dotnet build instead.
    static void EnsureRoslyn(string directory)
    {
        if (s_RoslynDirectory != null)
        {
            if (!PathsEqual(s_RoslynDirectory, directory))
            {
                throw new FallbackException($"compiler already bound to '{s_RoslynDirectory}', project uses '{directory}'");
            }
            return;
        }
        if (!File.Exists(Path.Combine(directory, "Microsoft.CodeAnalysis.CSharp.dll")))
        {
            throw new FallbackException($"Roslyn not found in '{directory}'");
        }

        s_RoslynDirectory = directory;
        var context = AssemblyLoadContext.GetLoadContext(typeof(CompilerHost).Assembly) ?? AssemblyLoadContext.Default;
        context.Resolving += (ctx, name) =>
        {
            string candidate = Path.Combine(directory, name.Name + ".dll");
            return File.Exists(candidate) ? ctx.LoadFromAssemblyPath(candidate) : null;
        };
    }

    internal static bool PathsEqual(string a, string b) =>
        string.Equals(Path.GetFullPath(a).TrimEnd('\\', '/'), Path.GetFullPath(b).TrimEnd('\\', '/'), StringComparison.OrdinalIgnoreCase);

    static byte* ToNativeUtf8(string text)
    {
        int length = Encoding.UTF8.GetByteCount(text);
        byte* buffer = (byte*)NativeMemory.Alloc((nuint)length + 1);
        fixed (char* chars = text)
        {
            Encoding.UTF8.GetBytes(chars, text.Length, buffer, length);
        }
        buffer[length] = 0;
        return buffer;
    }

    sealed class ProjectState(string fingerprint, ProjectEvaluation evaluation)
    {
        public string Fingerprint { get; } = fingerprint;
        public ProjectEvaluation Evaluation { get; } = evaluation;
        // Typed object so loading ProjectState never loads Roslyn.
        public object? Session { get; set; }
    }
}

// Calls that need Roslyn types; NoInlining keeps them from being compiled into CompilerHost before EnsureRoslyn.
static class RoslynBridge
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static object CreateSession(ProjectEvaluation evaluation) => new RoslynSession(evaluation);

    [MethodImpl(MethodImplOptions.NoInlining)]
    public static CompileResponse Compile(object session, string outputPath) => ((RoslynSession)session).Compile(outputPath);
}

sealed class FallbackException(string message) : Exception(message);

sealed class CompileRequest
{
    public string Project { get; set; } = "";
    public string Configuration { get; set; } = "Release";
    // Verbatim MSBuild switches the editor would pass to dotnet build (e.g. -p:DefineConstants=A%3BB).
    public List<string> Properties { get; set; } = [];
    public string OutputPath { get; set; } = "";
    public string Dotnet { get; set; } = "dotnet";
}

sealed class CompileResponse
{
    // compiled | failed | fallback
    public string Status { get; set; } = "fallback";
    public string? Message { get; set; }
    public string? Diagnostics { get; set; }
    public int Warnings { get; set; }
    public double ElapsedMs { get; set; }
    public bool Evaluated { get; set; }
}
