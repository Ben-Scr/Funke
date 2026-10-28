using System.Collections.Concurrent;
using System.Collections.Immutable;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Diagnostics;
using Microsoft.CodeAnalysis.Emit;
using Microsoft.CodeAnalysis.Text;

namespace Funke.ScriptCompiler;

// One evaluated project: the parsed csc command line plus caches that make a one-file edit cheap
// (metadata references, unchanged syntax trees, the incremental generator driver).
sealed class RoslynSession
{
    const int k_MaxReportedDiagnostics = 200;

    sealed class ReferenceCacheEntry
    {
        public DateTime Stamp;
        public long Length;
        public PortableExecutableReference? Reference;
    }

    static readonly ConcurrentDictionary<string, ReferenceCacheEntry> s_References =
        new(OperatingSystem.IsWindows() ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal);
    static readonly GeneratorAssemblyLoader s_GeneratorLoader = new();

    readonly CSharpCommandLineArguments m_Arguments;
    readonly ImmutableArray<MetadataReference> m_References;
    readonly CSharpCompilationOptions m_Options;
    readonly Dictionary<string, (DateTime Stamp, long Length, SyntaxTree Tree)> m_Trees = new(StringComparer.OrdinalIgnoreCase);
    GeneratorDriver? m_Driver;

    public RoslynSession(ProjectEvaluation evaluation)
    {
        // The items are pre-quoted for a response file; re-split them the way csc reads one.
        var commandLine = CommandLineParser.SplitCommandLineIntoArguments(string.Join(" ", evaluation.CscArguments), removeHashComments: false);
        m_Arguments = CSharpCommandLineParser.Default.Parse(commandLine, evaluation.ProjectDirectory, sdkDirectory: null);
        var errors = m_Arguments.Errors.Where(d => d.Severity == DiagnosticSeverity.Error).ToList();
        if (errors.Count > 0)
        {
            throw new FallbackException("unsupported compiler arguments: " + string.Join("; ", errors.Select(e => e.GetMessage())));
        }

        m_References = m_Arguments.MetadataReferences.Select(r =>
        {
            string path = Path.GetFullPath(r.Reference, m_Arguments.BaseDirectory!);
            var reference = GetReference(path);
            return (MetadataReference)reference.WithProperties(r.Properties);
        }).ToImmutableArray();

        var config = new ConfigOptions(m_Arguments.AnalyzerConfigPaths);
        m_Options = m_Arguments.CompilationOptions
            .WithSourceReferenceResolver(new SourceFileResolver(ImmutableArray<string>.Empty, m_Arguments.BaseDirectory, m_Arguments.PathMap))
            .WithXmlReferenceResolver(new XmlFileResolver(m_Arguments.BaseDirectory))
            .WithAssemblyIdentityComparer(DesktopAssemblyIdentityComparer.Default)
            .WithStrongNameProvider(new DesktopStrongNameProvider(m_Arguments.KeyFileSearchPaths))
            .WithSyntaxTreeOptionsProvider(new TreeOptionsProvider(config));

        // Only generators run: they change the emitted code. Analyzers just add diagnostics, which the full build reports.
        var analyzerPaths = m_Arguments.AnalyzerReferences.Select(a => Path.GetFullPath(a.FilePath, m_Arguments.BaseDirectory!)).ToList();
        // Like csc: register every analyzer first so generators can resolve each other (Interop.* share SourceGeneration).
        analyzerPaths.ForEach(s_GeneratorLoader.AddDependencyLocation);
        var generators = new List<ISourceGenerator>();
        foreach (string analyzerPath in analyzerPaths)
        {
            var reference = new AnalyzerFileReference(analyzerPath, s_GeneratorLoader);
            string? loadError = null;
            reference.AnalyzerLoadFailed += (_, e) => loadError ??= e.Message;
            generators.AddRange(reference.GetGenerators(LanguageNames.CSharp));
            if (loadError != null)
            {
                throw new FallbackException($"could not load analyzer '{Path.GetFileName(analyzerPath)}': {loadError}");
            }
        }
        if (generators.Count > 0)
        {
            var additionalTexts = m_Arguments.AdditionalFiles.Select(f => (AdditionalText)new FileAdditionalText(f.Path)).ToImmutableArray();
            m_Driver = CSharpGeneratorDriver.Create(generators, additionalTexts, m_Arguments.ParseOptions,
                new GeneratorOptionsProvider(config), new GeneratorDriverOptions(IncrementalGeneratorOutputKind.None));
        }
    }

    static PortableExecutableReference GetReference(string path)
    {
        ReferenceCacheEntry entry = s_References.GetOrAdd(path, static _ => new ReferenceCacheEntry());
        lock (entry)
        {
            var info = new FileInfo(path);
            if (entry.Reference == null || entry.Stamp != info.LastWriteTimeUtc || entry.Length != info.Length)
            {
                var reference = MetadataReference.CreateFromFile(path);
                entry.Stamp = info.LastWriteTimeUtc;
                entry.Length = info.Length;
                // Existing sessions retain their own reference until their compilation is released.
                entry.Reference = reference;
            }
            return entry.Reference;
        }
    }

    public CompileResponse Compile(string outputPath)
    {
        var trees = new List<SyntaxTree>(m_Arguments.SourceFiles.Length);
        foreach (CommandLineSourceFile source in m_Arguments.SourceFiles)
        {
            trees.Add(GetTree(source.Path));
        }

        Compilation compilation = CSharpCompilation.Create(m_Arguments.CompilationName, trees, m_References, m_Options);
        var diagnostics = new List<Diagnostic>();
        if (m_Driver != null)
        {
            m_Driver = m_Driver.RunGeneratorsAndUpdateCompilation(compilation, out compilation, out var generatorDiagnostics);
            diagnostics.AddRange(generatorDiagnostics);
        }

        using var peStream = new MemoryStream();
        using var pdbStream = new MemoryStream();
        using var xmlStream = new MemoryStream();
        using var win32Resources = OpenWin32Resources(compilation);
        using var sourceLink = m_Arguments.SourceLink != null ? File.OpenRead(m_Arguments.SourceLink) : null;
        string pdbPath = Path.ChangeExtension(outputPath, ".pdb");
        EmitResult result = compilation.Emit(
            peStream,
            m_Arguments.EmitPdb ? pdbStream : null,
            m_Arguments.DocumentationPath != null ? xmlStream : null,
            win32Resources,
            m_Arguments.ManifestResources,
            m_Arguments.EmitOptions.WithPdbFilePath(pdbPath),
            sourceLinkStream: sourceLink,
            embeddedTexts: GetEmbeddedTexts());
        diagnostics.AddRange(result.Diagnostics);

        var errors = diagnostics.Where(d => d.Severity == DiagnosticSeverity.Error).ToList();
        int warnings = diagnostics.Count(d => d.Severity == DiagnosticSeverity.Warning);
        if (!result.Success || errors.Count > 0)
        {
            return new CompileResponse
            {
                Status = "failed",
                Message = $"{errors.Count} error(s)",
                Diagnostics = string.Join("\n", errors.Take(k_MaxReportedDiagnostics).Select(d => d.ToString())),
                Warnings = warnings,
            };
        }

        Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
        WriteAtomically(outputPath, peStream);
        if (m_Arguments.EmitPdb)
        {
            WriteAtomically(pdbPath, pdbStream);
        }
        if (m_Arguments.DocumentationPath != null)
        {
            WriteAtomically(Path.Combine(Path.GetDirectoryName(outputPath)!, Path.GetFileName(m_Arguments.DocumentationPath)), xmlStream);
        }
        return new CompileResponse { Status = "compiled", Warnings = warnings };
    }

    SyntaxTree GetTree(string path)
    {
        var info = new FileInfo(path);
        if (!info.Exists)
        {
            throw new FallbackException($"source file disappeared: {path}");
        }
        if (m_Trees.TryGetValue(path, out var cached) && cached.Stamp == info.LastWriteTimeUtc && cached.Length == info.Length)
        {
            return cached.Tree;
        }
        SourceText text;
        using (var stream = OpenShared(path))
        {
            text = SourceText.From(stream, m_Arguments.Encoding, m_Arguments.ChecksumAlgorithm, throwIfBinaryDetected: true, canBeEmbedded: true);
        }
        SyntaxTree tree = CSharpSyntaxTree.ParseText(text, m_Arguments.ParseOptions, path);
        m_Trees[path] = (info.LastWriteTimeUtc, info.Length, tree);
        return tree;
    }

    IEnumerable<EmbeddedText>? GetEmbeddedTexts()
    {
        if (m_Arguments.EmbeddedFiles.IsDefaultOrEmpty)
        {
            return null;
        }
        return m_Arguments.EmbeddedFiles.Select(f => m_Trees.TryGetValue(f.Path, out var cached)
            ? EmbeddedText.FromSource(f.Path, cached.Tree.GetText())
            : EmbeddedText.FromStream(f.Path, OpenShared(f.Path), m_Arguments.ChecksumAlgorithm)).ToList();
    }

    Stream OpenWin32Resources(Compilation compilation)
    {
        if (m_Arguments.Win32ResourceFile != null)
        {
            return OpenShared(m_Arguments.Win32ResourceFile);
        }
        using var manifest = m_Arguments.Win32Manifest != null ? OpenShared(m_Arguments.Win32Manifest) : null;
        using var icon = m_Arguments.Win32Icon != null ? OpenShared(m_Arguments.Win32Icon) : null;
        return compilation.CreateDefaultWin32Resources(versionResource: true, m_Arguments.NoWin32Manifest, manifest, icon);
    }

    static FileStream OpenShared(string path) => new(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);

    // The editor reads the assembly from bytes, so replacing it is safe; the rename keeps readers from seeing half a file.
    static void WriteAtomically(string path, MemoryStream content)
    {
        string temporary = path + ".tmp";
        using (var file = new FileStream(temporary, FileMode.Create, FileAccess.Write, FileShare.None))
        {
            content.Position = 0;
            content.CopyTo(file);
        }
        File.Move(temporary, path, overwrite: true);
    }
}

// .editorconfig / .globalconfig values, the same data csc feeds to generators and per-tree severities.
sealed class ConfigOptions
{
    readonly AnalyzerConfigSet? m_Set;
    readonly ConcurrentDictionary<string, AnalyzerConfigOptionsResult> m_PerPath = new(StringComparer.OrdinalIgnoreCase);

    public ConfigOptions(ImmutableArray<string> paths)
    {
        if (paths.IsDefaultOrEmpty) return;
        m_Set = AnalyzerConfigSet.Create(paths.Select(p => AnalyzerConfig.Parse(File.ReadAllText(p), p)).ToList());
        Global = m_Set.GlobalConfigOptions;
    }

    public AnalyzerConfigOptionsResult Global { get; }

    public AnalyzerConfigOptionsResult For(string path) =>
        m_Set == null ? default : m_PerPath.GetOrAdd(path, m_Set.GetOptionsForSourcePath);
}

sealed class TreeOptionsProvider(ConfigOptions config) : SyntaxTreeOptionsProvider
{
    public override GeneratedKind IsGenerated(SyntaxTree tree, CancellationToken cancellationToken)
    {
        var options = config.For(tree.FilePath).AnalyzerOptions;
        if (options != null && options.TryGetValue("generated_code", out string? value) && bool.TryParse(value, out bool generated))
        {
            return generated ? GeneratedKind.MarkedGenerated : GeneratedKind.NotGenerated;
        }
        return GeneratedKind.Unknown;
    }

    public override bool TryGetDiagnosticValue(SyntaxTree tree, string diagnosticId, CancellationToken cancellationToken, out ReportDiagnostic severity)
    {
        var options = config.For(tree.FilePath).TreeOptions;
        if (options != null && options.TryGetValue(diagnosticId, out severity)) return true;
        severity = ReportDiagnostic.Default;
        return false;
    }

    public override bool TryGetGlobalDiagnosticValue(string diagnosticId, CancellationToken cancellationToken, out ReportDiagnostic severity)
    {
        var options = config.Global.TreeOptions;
        if (options != null && options.TryGetValue(diagnosticId, out severity)) return true;
        severity = ReportDiagnostic.Default;
        return false;
    }
}

sealed class GeneratorOptionsProvider(ConfigOptions config) : AnalyzerConfigOptionsProvider
{
    public override AnalyzerConfigOptions GlobalOptions { get; } = new DictionaryOptions(config.Global.AnalyzerOptions);
    public override AnalyzerConfigOptions GetOptions(SyntaxTree tree) => new DictionaryOptions(config.For(tree.FilePath).AnalyzerOptions);
    public override AnalyzerConfigOptions GetOptions(AdditionalText textFile) => new DictionaryOptions(config.For(textFile.Path).AnalyzerOptions);

    sealed class DictionaryOptions(ImmutableDictionary<string, string>? values) : AnalyzerConfigOptions
    {
        readonly ImmutableDictionary<string, string> m_Values = (values ?? ImmutableDictionary<string, string>.Empty).WithComparers(KeyComparer);
        public override bool TryGetValue(string key, out string value) => m_Values.TryGetValue(key, out value!);
        public override IEnumerable<string> Keys => m_Values.Keys;
    }
}

sealed class FileAdditionalText(string path) : AdditionalText
{
    public override string Path => path;
    public override SourceText? GetText(CancellationToken cancellationToken = default) =>
        File.Exists(path) ? SourceText.From(File.ReadAllText(path), Encoding.UTF8) : null;
}

// Generators must bind to the Roslyn this assembly already loaded, never a second copy.
sealed class GeneratorAssemblyLoader : IAnalyzerAssemblyLoader
{
    readonly Context m_Context = new();

    public void AddDependencyLocation(string fullPath) => m_Context.Add(fullPath);
    public Assembly LoadFromPath(string fullPath) => m_Context.Load(fullPath);

    sealed class Context() : AssemblyLoadContext("FunkeScriptCompilerGenerators")
    {
        readonly ConcurrentDictionary<string, string> m_PathsByName = new(StringComparer.OrdinalIgnoreCase);
        readonly ConcurrentDictionary<string, Assembly> m_Loaded = new(StringComparer.OrdinalIgnoreCase);

        public void Add(string fullPath) => m_PathsByName.TryAdd(System.IO.Path.GetFileNameWithoutExtension(fullPath), fullPath);

        public Assembly Load(string fullPath)
        {
            Add(fullPath);
            return m_Loaded.GetOrAdd(fullPath, LoadFromAssemblyPath);
        }

        protected override Assembly? Load(AssemblyName name)
        {
            foreach (Assembly compiler in new[] { typeof(Compilation).Assembly, typeof(CSharpCompilation).Assembly })
            {
                if (string.Equals(compiler.GetName().Name, name.Name, StringComparison.OrdinalIgnoreCase)) return compiler;
            }
            if (name.Name != null && m_PathsByName.TryGetValue(name.Name, out string? path))
            {
                return m_Loaded.GetOrAdd(path, LoadFromAssemblyPath);
            }
            // Framework assemblies come from the default context; anything else Roslyn's own context resolves.
            return name.Name != null && name.Name.StartsWith("Microsoft.CodeAnalysis", StringComparison.OrdinalIgnoreCase)
                ? AssemblyLoadContext.GetLoadContext(typeof(Compilation).Assembly)?.LoadFromAssemblyName(name)
                : null;
        }
    }
}
