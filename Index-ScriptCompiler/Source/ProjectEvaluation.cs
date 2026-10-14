using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace Index.ScriptCompiler;

// What dotnet build would hand csc, captured once from MSBuild (targets run, compiler skipped) and reused
// for every edit until a project file changes or .cs files are added/removed.
sealed class ProjectEvaluation
{
    public const string ProjectFilesChanged = "project files changed";

    static readonly string[] s_SkippedDirectories = ["bin", "obj", ".git", ".vs", ".idea", ".index", "node_modules"];

    public required string ProjectDirectory { get; init; }
    public required string TargetPath { get; init; }
    public required string RoslynDirectory { get; init; }
    public required IReadOnlyList<string> CscArguments { get; init; }
    public required IReadOnlyList<(string Source, string Destination)> CopyLocal { get; init; }
    public required Dictionary<string, DateTime> ProjectFiles { get; init; }
    public required HashSet<string> SourceFiles { get; init; }

    public static ProjectEvaluation Run(string projectPath, CompileRequest request)
    {
        string projectDirectory = Path.GetDirectoryName(projectPath)!;
        // Snapshot before MSBuild runs so an edit made during evaluation still marks it stale.
        var projectFiles = SnapshotProjectFiles(projectDirectory);
        var sourceFiles = DiscoverSourceFiles(projectDirectory);

        var start = new ProcessStartInfo(request.Dotnet)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
            WorkingDirectory = projectDirectory,
            StandardOutputEncoding = Encoding.UTF8,
        };
        foreach (string arg in new[]
        {
            "msbuild", projectPath, "-restore", "-nologo", "-v:q", "-t:Compile",
            "-p:Configuration=" + request.Configuration,
            // Run every target up to csc and report its arguments; NonExistentFile defeats CoreCompile's up-to-date check.
            "-p:ProvideCommandLineArgs=true",
            "-p:SkipCompilerExecution=true",
            @"-p:NonExistentFile=__NonExistentSubDir__\__NonExistentFile__",
        }.Concat(request.Properties).Concat(new[]
        {
            "-getItem:CscCommandLineArgs",
            "-getItem:ReferenceCopyLocalPaths",
            "-getProperty:RoslynTargetsPath",
            "-getProperty:TargetPath",
        }))
        {
            start.ArgumentList.Add(arg);
        }

        using var process = Process.Start(start) ?? throw new FallbackException("could not start dotnet msbuild");
        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        if (!process.WaitForExit(TimeSpan.FromMinutes(5)))
        {
            try { process.Kill(entireProcessTree: true); } catch { }
            throw new FallbackException("MSBuild evaluation timed out");
        }
        string output = stdout.Result;
        if (process.ExitCode != 0)
        {
            throw new FallbackException("MSBuild evaluation failed:\n" + Tail(output + stderr.Result, 4000));
        }

        using var json = ParseJson(output);
        JsonElement root = json.RootElement;
        JsonElement properties = root.GetProperty("Properties");
        JsonElement items = root.GetProperty("Items");

        var cscArguments = items.TryGetProperty("CscCommandLineArgs", out var csc)
            ? csc.EnumerateArray().Select(i => i.GetProperty("Identity").GetString() ?? "").ToList()
            : [];
        if (cscArguments.Count == 0)
        {
            throw new FallbackException("MSBuild reported no compiler arguments");
        }

        string targetPath = Path.GetFullPath(properties.GetProperty("TargetPath").GetString() ?? "", projectDirectory);
        string outputDirectory = Path.GetDirectoryName(targetPath)!;
        var copyLocal = new List<(string, string)>();
        if (items.TryGetProperty("ReferenceCopyLocalPaths", out var references))
        {
            foreach (JsonElement item in references.EnumerateArray())
            {
                string source = Metadata(item, "FullPath") ?? Path.GetFullPath(Metadata(item, "Identity") ?? "", projectDirectory);
                string destination = Path.Combine(outputDirectory, Metadata(item, "DestinationSubDirectory") ?? "", Path.GetFileName(source));
                copyLocal.Add((source, destination));
            }
        }

        string roslynTargets = properties.GetProperty("RoslynTargetsPath").GetString() ?? "";
        return new ProjectEvaluation
        {
            ProjectDirectory = projectDirectory,
            TargetPath = targetPath,
            RoslynDirectory = Path.Combine(Path.GetFullPath(roslynTargets), "bincore"),
            CscArguments = cscArguments,
            CopyLocal = copyLocal,
            ProjectFiles = projectFiles,
            SourceFiles = sourceFiles,
        };
    }

    public string? FindStaleReason()
    {
        var current = SnapshotProjectFiles(ProjectDirectory);
        foreach (var (path, stamp) in current)
        {
            if (!ProjectFiles.TryGetValue(path, out DateTime previous) || previous != stamp)
            {
                return $"{ProjectFilesChanged}: {Path.GetFileName(path)}";
            }
        }
        if (current.Count != ProjectFiles.Count)
        {
            return $"{ProjectFilesChanged}: a project file was removed";
        }
        return DiscoverSourceFiles(ProjectDirectory).SetEquals(SourceFiles) ? null : "source files added or removed";
    }

    // MSBuild copies these next to the output; a compile that skips that step must not run without them.
    public string? FindMissingDependency()
    {
        foreach (var (source, destination) in CopyLocal)
        {
            if (!File.Exists(destination) || File.GetLastWriteTimeUtc(source) > File.GetLastWriteTimeUtc(destination))
            {
                return Path.GetFileName(destination);
            }
        }
        return null;
    }

    static Dictionary<string, DateTime> SnapshotProjectFiles(string projectDirectory)
    {
        var files = new Dictionary<string, DateTime>(StringComparer.OrdinalIgnoreCase);
        void Add(string directory)
        {
            if (!Directory.Exists(directory)) return;
            foreach (string file in Directory.EnumerateFiles(directory))
            {
                string extension = Path.GetExtension(file);
                if (extension.Equals(".csproj", StringComparison.OrdinalIgnoreCase)
                    || extension.Equals(".props", StringComparison.OrdinalIgnoreCase)
                    || extension.Equals(".targets", StringComparison.OrdinalIgnoreCase)
                    || Path.GetFileName(file).Equals("global.json", StringComparison.OrdinalIgnoreCase))
                {
                    files[file] = File.GetLastWriteTimeUtc(file);
                }
            }
        }
        Add(projectDirectory);
        Add(Path.Combine(projectDirectory, "Packages"));
        return files;
    }

    static HashSet<string> DiscoverSourceFiles(string projectDirectory)
    {
        var files = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var pending = new Stack<string>();
        pending.Push(projectDirectory);
        while (pending.Count > 0)
        {
            string directory = pending.Pop();
            try
            {
                foreach (string file in Directory.EnumerateFiles(directory, "*.cs"))
                {
                    files.Add(file);
                }
                foreach (string child in Directory.EnumerateDirectories(directory))
                {
                    if (!s_SkippedDirectories.Contains(Path.GetFileName(child), StringComparer.OrdinalIgnoreCase))
                    {
                        pending.Push(child);
                    }
                }
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
        return files;
    }

    static string? Metadata(JsonElement item, string name) =>
        item.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.String && value.GetString() is { Length: > 0 } text
            ? text
            : null;

    // -getItem prints JSON, but restore can still put a line of text in front of it.
    static JsonDocument ParseJson(string output)
    {
        int start = output.IndexOf('{');
        if (start < 0)
        {
            throw new FallbackException("MSBuild evaluation returned no JSON:\n" + Tail(output, 2000));
        }
        return JsonDocument.Parse(output.AsMemory(start));
    }

    static string Tail(string text, int length) => text.Length <= length ? text : text[^length..];
}
