using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Reflection;
using System.Linq;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Threading;
using Funke;
using Funke.Coroutines;

namespace Funke.Interop;

/// <summary>
/// Resolves Funke-ScriptCore from the default context to prevent duplicate type loading.
/// </summary>
internal class ScriptAssemblyLoadContext : AssemblyLoadContext
{
    private readonly Assembly _coreAssembly;
    private readonly AssemblyDependencyResolver? _resolver;
    private readonly string? _userAssemblyDir;

    // Hosts without hot reload (Mono players) turn this off and the game loads into the default context:
    // Mono only uses AOT code for non-collectible assemblies.
    public static bool Collectible =>
        !AppContext.TryGetSwitch("Funke.Scripting.CollectibleUserAssembly", out bool collectible) || collectible;

    private static string? s_DefaultProbeDir;

    // The game's own dependencies (NuGet DLLs next to it) are not on the TPA list; the default context
    // only raises Resolving for names it could not bind itself, so framework loads never get here.
    public static void ProbeFromDefaultContext(string userAssemblyPath)
    {
        if (s_DefaultProbeDir == null)
        {
            Default.Resolving += (_, name) =>
            {
                string candidate = System.IO.Path.Combine(s_DefaultProbeDir!, name.Name + ".dll");
                return System.IO.File.Exists(candidate) ? Default.LoadFromAssemblyPath(candidate) : null;
            };
        }
        s_DefaultProbeDir = System.IO.Path.GetDirectoryName(userAssemblyPath);
    }

    public ScriptAssemblyLoadContext(Assembly coreAssembly, string? userAssemblyPath = null)
        : base("FunkeUserScripts", isCollectible: true)
    {
        _coreAssembly = coreAssembly;
        if (userAssemblyPath != null)
        {
            _userAssemblyDir = System.IO.Path.GetDirectoryName(
                System.IO.Path.GetFullPath(userAssemblyPath));

            try { _resolver = new AssemblyDependencyResolver(userAssemblyPath); }
            catch (Exception ex)
            {
                Log.Warn($"[ScriptLoader] AssemblyDependencyResolver failed: {ex.Message}");
                _resolver = null;
            }
        }

        // Register Resolving event as a last-resort fallback.
        // This fires when Load() returns null AND the Default context also fails.
        this.Resolving += OnResolving;
    }

    protected override Assembly? Load(AssemblyName name)
    {
        if (name.Name == _coreAssembly.GetName().Name)
            return _coreAssembly;

        // Try .deps.json resolver (resolves NuGet packages from cache or local copy)
        if (_resolver != null)
        {
            string? resolvedPath = _resolver.ResolveAssemblyToPath(name);
            if (resolvedPath != null)
                return LoadDependency(resolvedPath);
        }

        // Probe the user assembly's output directory
        Assembly? probed = ProbeDirectory(_userAssemblyDir, name.Name);
        if (probed != null) return probed;

        return null;
    }

    private Assembly? OnResolving(AssemblyLoadContext context, AssemblyName name)
    {
        // Last-resort: re-probe directory and NuGet cache
        Log.Warn($"[ScriptLoader] Resolving fallback for: {name.Name}");

        Assembly? probed = ProbeDirectory(_userAssemblyDir, name.Name);
        if (probed != null) return probed;

        // Check NuGet global packages cache
        if (name.Name != null && name.Version != null)
        {
            string? nugetDir = System.Environment.GetEnvironmentVariable("NUGET_PACKAGES");
            if (string.IsNullOrEmpty(nugetDir))
                nugetDir = System.IO.Path.Combine(
                    System.Environment.GetFolderPath(System.Environment.SpecialFolder.UserProfile),
                    ".nuget", "packages");

            string packageDir = System.IO.Path.Combine(
                nugetDir, name.Name.ToLowerInvariant(), name.Version.ToString());

            if (System.IO.Directory.Exists(packageDir))
            {
                string[] tfms = { "net10.0", "net9.0", "net8.0", "net7.0", "net6.0", "netstandard2.1", "netstandard2.0" };
                foreach (string tfm in tfms)
                {
                    string dllPath = System.IO.Path.Combine(packageDir, "lib", tfm, name.Name + ".dll");
                    if (System.IO.File.Exists(dllPath))
                        return LoadDependency(dllPath);
                }
            }
        }

        Log.Error($"[ScriptLoader] Failed to resolve: {name.FullName}");
        return null;
    }

    private Assembly? ProbeDirectory(string? dir, string? assemblyName)
    {
        if (dir == null || assemblyName == null) return null;
        string candidate = System.IO.Path.Combine(dir, assemblyName + ".dll");
        if (System.IO.File.Exists(candidate))
            return LoadDependency(candidate);
        return null;
    }

    private Assembly LoadDependency(string path)
    {
        using var assemblyStream = new System.IO.MemoryStream(System.IO.File.ReadAllBytes(path));
        using var symbolsStream = ScriptInstanceManager.ReadAssemblySymbols(path);
        return LoadFromStream(assemblyStream, symbolsStream);
    }
}
