using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;
using Funke.ScriptCompiler;

if (args.Length != 2)
{
    Console.Error.WriteLine("CompilerBenchmark <BoltScripts fixture directory> <Funke-ScriptCore.dll>");
    return 2;
}

string fixture = Path.GetFullPath(args[0]);
string core = Path.GetFullPath(args[1]);
string work = Directory.CreateTempSubdirectory("FunkeBoltComparison").FullName;
try
{
    string project = Path.Combine(work, "BenchmarkScripts.csproj");
    string source = Path.Combine(work, "Move.cs");
    File.Copy(Path.Combine(fixture, "BenchmarkScripts.csproj"), project);
    string original = File.ReadAllText(Path.Combine(fixture, "Move.cs"));
    File.WriteAllText(source, original);

    var restore = new ProcessStartInfo("dotnet") { UseShellExecute = false };
    restore.ArgumentList.Add("restore");
    restore.ArgumentList.Add(project);
    using (var process = Process.Start(restore)!)
    {
        process.WaitForExit();
        if (process.ExitCode != 0) return process.ExitCode;
    }

    double[] edits = new double[8];
    double cold = 0;
    for (int i = 0; i <= edits.Length; ++i)
    {
        if (i > 0) File.WriteAllText(source, original.Replace("1.0f", (i + 1) + ".0f"));
        string request = JsonSerializer.Serialize(new {
            project, configuration = "Release", properties = new[] { "-p:FunkeScriptCore=" + core },
            outputPath = Path.Combine(work, "bin", "Release", "net10.0", "BenchmarkScripts.dll"),
            changedPaths = new[] { source }
        });
        double elapsed = Compile(request);
        if (i == 0) cold = elapsed;
        else edits[i - 1] = elapsed;
    }
    Array.Sort(edits);
    Console.WriteLine(FormattableString.Invariant($"ENTITYBUILD cold_ms={cold:F4} edit_ms={(edits[3] + edits[4]) / 2:F4}"));
    return 0;
}
finally
{
    Directory.Delete(work, recursive: true);
}

static unsafe double Compile(string request)
{
    delegate* unmanaged<byte*, byte*> compile = &CompilerHost.Compile;
    delegate* unmanaged<byte*, void> release = &CompilerHost.FreeResult;
    nint input = Marshal.StringToCoTaskMemUTF8(request);
    byte* result = null;
    try
    {
        var timer = Stopwatch.StartNew();
        result = compile((byte*)input);
        timer.Stop();
        using var json = JsonDocument.Parse(Marshal.PtrToStringUTF8((nint)result)!);
        var root = json.RootElement;
        if (root.GetProperty("status").GetString() != "compiled" || root.GetProperty("fullBuild").GetBoolean())
            throw new InvalidOperationException(root.ToString());
        return timer.Elapsed.TotalMilliseconds;
    }
    finally
    {
        if (result != null) release(result);
        Marshal.FreeCoTaskMem(input);
    }
}

