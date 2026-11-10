using System.IO.Compression;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.Json.Serialization;

// Funke-PackageTool: Lightweight CLI for package operations.
// Called by the engine via _popen(). All output is JSON to stdout, errors to stderr.
// Stateless — every invocation is independent.

var handler = new HttpClientHandler { AllowAutoRedirect = true };
using var http = new HttpClient(handler);
http.DefaultRequestHeaders.UserAgent.ParseAdd("Funke-PackageTool/1.0");
http.Timeout = TimeSpan.FromSeconds(15);

var jsonOpts = new JsonSerializerOptions
{
    PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    WriteIndented = false
};

if (args.Length == 0)
{
    Console.Error.WriteLine("Usage: Funke-PackageTool <command> [args]");
    Console.Error.WriteLine("Commands: nuget-search, nuget-versions, github-index, github-download, unzip, zip, registry-fetch, registry-download");
    return 1;
}

try
{
    return args[0] switch
    {
        "nuget-search" => await NuGetSearch(args),
        "nuget-versions" => await NuGetVersions(args),
        "github-index" => await GitHubIndex(args),
        "github-download" => await GitHubDownload(args),
        "thumbnail-download" => await ThumbnailDownload(args),
        "unzip" => Unzip(args),
        "zip" => Zip(args),
        "registry-fetch" => await RegistryFetch(args),
        "registry-download" => await RegistryDownload(args),
        _ => Error($"Unknown command: {args[0]}")
    };
}
catch (HttpRequestException ex)
{
    Console.Error.WriteLine($"Network error: {ex.Message}");
    Console.WriteLine("[]");
    return 1;
}
catch (TaskCanceledException)
{
    Console.Error.WriteLine("Request timed out");
    Console.WriteLine("[]");
    return 1;
}
catch (Exception ex)
{
    Console.Error.WriteLine($"Error: {ex.Message}");
    return 1;
}


// ── NuGet v3 Search ──────────────────────────────────────────────

async Task<int> NuGetSearch(string[] args)
{
    if (args.Length < 2) return Error("Usage: nuget-search <query> [--take N]");

    string query = args[1];
    int take = 20;

    for (int i = 2; i < args.Length - 1; i++)
    {
        if (args[i] == "--take" && int.TryParse(args[i + 1], out int t))
            take = t;
    }

    string url = $"https://azuresearch-usnc.nuget.org/query?q={Uri.EscapeDataString(query)}&take={take}&prerelease=false&semVerLevel=2.0.0";
    var response = await http.GetFromJsonAsync<NuGetSearchResponse>(url);

    if (response?.Data == null)
    {
        Console.WriteLine("[]");
        return 0;
    }

    var results = response.Data.Select(p => new
    {
        id = p.Id,
        version = p.Version,
        description = Truncate(p.Description, 200),
        authors = string.Join(", ", p.Authors ?? []),
        downloads = p.TotalDownloads,
        verified = p.Verified
    });

    Console.WriteLine(JsonSerializer.Serialize(results, jsonOpts));
    return 0;
}


// ── NuGet v3 Versions ────────────────────────────────────────────

async Task<int> NuGetVersions(string[] args)
{
    if (args.Length < 2) return Error("Usage: nuget-versions <packageId>");

    string packageId = args[1].ToLowerInvariant();
    string url = $"https://api.nuget.org/v3-flatcontainer/{packageId}/index.json";
    var response = await http.GetFromJsonAsync<NuGetVersionsResponse>(url);

    if (response?.Versions == null)
    {
        Console.WriteLine("[]");
        return 0;
    }

    // Return newest first
    var versions = response.Versions.AsEnumerable().Reverse().Take(30);
    Console.WriteLine(JsonSerializer.Serialize(versions, jsonOpts));
    return 0;
}


// ── GitHub Funke Fetch ───────────────────────────────────────────

async Task<int> GitHubIndex(string[] args)
{
    if (args.Length < 2) return Error("Usage: github-index <url>");

    string url = args[1];
    string json = await http.GetStringAsync(url);
    Console.WriteLine(json);
    return 0;
}


// ── GitHub File Download ─────────────────────────────────────────
//
// Streams the response body to disk in 64 KB chunks and emits a JSON
// progress line per ~100 ms so callers can show a real progress bar.
// Uses a dedicated HttpClient with an infinite timeout — the top-level
// `http` client has a 15 s timeout that would kill any non-trivial
// download. Errors propagate as exceptions and are reported by the
// outer try/catch.

async Task<int> ThumbnailDownload(string[] args)
{
    if (args.Length != 3 || !Uri.TryCreate(args[1], UriKind.Absolute, out var uri)
        || (uri.Scheme != "https" && uri.Scheme != "http")) return Error("Invalid thumbnail URL.");
    const int limit = 8 * 1024 * 1024;
    using var cancel = new CancellationTokenSource(TimeSpan.FromSeconds(8));
    using var response = await http.GetAsync(uri, HttpCompletionOption.ResponseHeadersRead, cancel.Token);
    response.EnsureSuccessStatusCode();
    if (response.Content.Headers.ContentLength > limit) return Error("Thumbnail exceeds 8 MiB.");
    bool success = false;
    try
    {
        await using var input = await response.Content.ReadAsStreamAsync(cancel.Token);
        await using var output = File.Create(args[2]);
        var buffer = new byte[16384];
        int total = 0, read;
        while ((read = await input.ReadAsync(buffer, cancel.Token)) != 0)
        {
            total += read;
            if (total > limit) return Error("Thumbnail exceeds 8 MiB.");
            await output.WriteAsync(buffer.AsMemory(0, read), cancel.Token);
        }
        success = true;
        return 0;
    }
    finally { if (!success) File.Delete(args[2]); }
}

async Task<int> GitHubDownload(string[] args)
{
    if (args.Length < 3) return Error("Usage: github-download <url> <outputPath> [expectedSha256]");

    string url = args[1];
    string outputPath = args[2];
    string expectedSha = args.Length >= 4 ? args[3].Trim().ToLowerInvariant() : "";
    if (!string.IsNullOrEmpty(expectedSha) && !IsSha256Hex(expectedSha))
        return Error("Expected SHA-256 must be exactly 64 hexadecimal characters.");

    string? dir = Path.GetDirectoryName(outputPath);
    if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
    string temporary = outputPath + "." + Guid.NewGuid().ToString("N") + ".tmp";
    try
    {
        long copied = await DownloadBounded(url, temporary);
        if (!string.IsNullOrEmpty(expectedSha))
        {
            string actualSha = await ComputeSha256(temporary);
            if (actualSha != expectedSha) return Error($"SHA-256 mismatch (expected {expectedSha}, got {actualSha})");
        }
        File.Move(temporary, outputPath, overwrite: true);
        Console.WriteLine(JsonSerializer.Serialize(new { @event = "complete", success = true, path = outputPath, size = copied }, jsonOpts));
        return 0;
    }
    finally { if (File.Exists(temporary)) File.Delete(temporary); }
}


// ── Local Zip Packing ────────────────────────────────────────────
//
// Packs <projectDir> into <outZip>. Files land at the zip root (no
// wrapper folder) — that's the shape the launcher's asset-library
// importer expects to find `project.json` at. Excludes the
// build/IDE noise that PublishProject.py also strips: `bin/`, `obj/`,
// `.vs/`, `.vscode/`, `__pycache__/`, `.idea/`, plus `*.user`, `*.suo`,
// `.DS_Store`, `Thumbs.db`.
//
// Refuses to overwrite an existing output zip — callers stage to a
// unique temp name and rename on success.

int Zip(string[] args)
{
    if (args.Length < 3) return Error("Usage: zip <projectDir> <outZip>");

    string projectDir = args[1];
    string outZip = args[2];

    if (!Directory.Exists(projectDir))
        return Error($"Project dir not found: {projectDir}");
    if (File.Exists(outZip))
        return Error($"Output zip already exists: {outZip}");

    var excludedDirs = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    {
        "bin", "obj", ".vs", ".vscode", "__pycache__", ".idea",
    };
    string[] excludedGlobs = { "*.user", "*.suo", ".DS_Store", "Thumbs.db" };

    static bool MatchesGlob(string name, string[] globs)
    {
        foreach (var g in globs)
        {
            // Trivial fnmatch for `*.ext` and exact-match patterns.
            if (g.StartsWith("*."))
            {
                if (name.EndsWith(g.Substring(1), StringComparison.OrdinalIgnoreCase))
                    return true;
            }
            else if (string.Equals(g, name, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }
        return false;
    }

    string? outDir = Path.GetDirectoryName(outZip);
    if (!string.IsNullOrEmpty(outDir)) Directory.CreateDirectory(outDir);

    int fileCount = 0;
    using (var zip = ZipFile.Open(outZip, ZipArchiveMode.Create))
    {
        var rootInfo = new DirectoryInfo(projectDir);
        // BFS so we can prune excluded directories without recursing.
        var queue = new Queue<DirectoryInfo>();
        queue.Enqueue(rootInfo);
        while (queue.Count > 0)
        {
            var current = queue.Dequeue();
            foreach (var sub in current.EnumerateDirectories())
            {
                if (!excludedDirs.Contains(sub.Name)) queue.Enqueue(sub);
            }
            foreach (var file in current.EnumerateFiles())
            {
                if (MatchesGlob(file.Name, excludedGlobs)) continue;

                string rel = Path.GetRelativePath(rootInfo.FullName, file.FullName)
                    .Replace('\\', '/');
                var entry = zip.CreateEntry(rel, CompressionLevel.Optimal);
                using var src = file.OpenRead();
                using var dst = entry.Open();
                src.CopyTo(dst);
                fileCount++;
            }
        }
    }

    long size = new FileInfo(outZip).Length;
    Console.WriteLine(JsonSerializer.Serialize(new
    {
        @event = "complete",
        success = true,
        outZip,
        size,
        fileCount,
    }, jsonOpts));
    return 0;
}


// ── Local Zip Extraction ─────────────────────────────────────────
//
// Extracts <archive> into <outDir>. <outDir> is created if missing.
// Refuses to overwrite existing files (the asset-library worker stages
// into a fresh dir per download, so overwrite would mask a bug rather
// than be useful). System.IO.Compression is in-box on .NET 10.

int Unzip(string[] args)
{
    if (args.Length < 3) return Error("Usage: unzip <archive> <outDir>");

    string archive = args[1];
    string outDir = args[2];

    if (!File.Exists(archive))
        return Error($"Archive not found: {archive}");

    outDir = Path.GetFullPath(outDir);
    RejectLinkedPath(outDir);
    if (Directory.Exists(outDir) && Directory.EnumerateFileSystemEntries(outDir).Any())
        return Error("Extraction destination must be absent or empty.");
    string parent = Path.GetDirectoryName(outDir) ?? throw new InvalidDataException("Invalid extraction destination.");
    Directory.CreateDirectory(parent);
    string stage = Path.Combine(parent, ".extract-" + Guid.NewGuid().ToString("N") + ".tmp");
    try
    {
        ExtractArchiveBounded(archive, stage);
        RejectLinkedPath(outDir);
        if (Directory.Exists(outDir)) Directory.Delete(outDir, recursive: false);
        Directory.Move(stage, outDir);
        Console.WriteLine(JsonSerializer.Serialize(new { @event = "complete", success = true, outDir }, jsonOpts));
        return 0;
    }
    finally { if (Directory.Exists(stage)) Directory.Delete(stage, recursive: true); }
}


// ── Funke Registry Fetch ─────────────────────────────────────────
//
// Downloads the registry JSON (a small file — the global Funke package
// index) to `outputJsonPath`. Writing to disk lets the engine parse it
// with its own Json.hpp and cache it for offline reads, rather than
// having to capture stdout. Output is atomic: stage to a sibling .tmp
// then rename, so a half-written file never appears in the cache.

async Task<int> RegistryFetch(string[] args)
{
    if (args.Length < 3) return Error("Usage: registry-fetch <registryUrl> <outputJsonPath>");

    string url = args[1];
    string outputPath = args[2];

    string? dir = Path.GetDirectoryName(outputPath);
    if (!string.IsNullOrEmpty(dir))
        Directory.CreateDirectory(dir);

    // Handle network / status-code errors inline so we don't fall through to
    // the outer catch (which prints "[]" — a no-results sentinel for the NuGet
    // commands that would be noise here).
    try
    {
        string json = await http.GetStringAsync(url);

        string tempPath = outputPath + ".tmp";
        await File.WriteAllTextAsync(tempPath, json);

        if (File.Exists(outputPath)) File.Delete(outputPath);
        File.Move(tempPath, outputPath);

        Console.WriteLine(JsonSerializer.Serialize(new
        {
            @event = "complete",
            success = true,
            path = outputPath,
            bytes = json.Length
        }, jsonOpts));
        return 0;
    }
    catch (HttpRequestException ex)
    {
        Console.Error.WriteLine($"Registry fetch failed: {ex.Message}");
        return 1;
    }
    catch (TaskCanceledException)
    {
        Console.Error.WriteLine("Registry fetch timed out.");
        return 1;
    }
}


// ── Funke Registry Download ──────────────────────────────────────
//
// Downloads a package zip, verifies its SHA-256 against `expectedSha256`
// (lowercase hex) and extracts it into `<destPackagesDir>/<topFolder>/`.
// `<destPackagesDir>` is conventionally `<project>/Packages` — the zip's
// single top-level folder becomes the installed package directory.
//
// Failure semantics mirror Localization::LanguageDownloader: never let
// a partial or unverified file land in the destination. Temp files are
// cleaned up in every failure path.
//
// The zip MUST contain exactly one top-level folder, and that folder
// MUST contain `funke-package.lua` at its root.

async Task<int> RegistryDownload(string[] args)
{
    if (args.Length < 4) return Error("Usage: registry-download <zipUrl> <expectedSha256> <destPackagesDir>");

    string zipUrl = args[1];
    string expectedSha = args[2].Trim().ToLowerInvariant();
    string destPackagesDir = args[3];
    if (!IsSha256Hex(expectedSha))
        return Error("Expected SHA-256 must be exactly 64 hexadecimal characters.");

    RejectLinkedPath(destPackagesDir);
    Directory.CreateDirectory(destPackagesDir);

    string token = Guid.NewGuid().ToString("N");
    string tempZip = Path.Combine(destPackagesDir, $".registry-{token}.zip.tmp");
    string tempExtract = Path.Combine(destPackagesDir, $".registry-{token}.extract.tmp");
    // Captured by Cleanup so a post-Move failure rolls back the install
    // and the next attempt doesn't hit "Package already installed."
    string? installedDirToRollback = null;

    void Cleanup()
    {
        try { if (File.Exists(tempZip)) File.Delete(tempZip); } catch { }
        try { if (Directory.Exists(tempExtract)) Directory.Delete(tempExtract, recursive: true); } catch { }
        try {
            if (installedDirToRollback != null && Directory.Exists(installedDirToRollback))
                Directory.Delete(installedDirToRollback, recursive: true);
        } catch { }
    }

    try
    {
        await DownloadBounded(zipUrl, tempZip);
        string actualSha = await ComputeSha256(tempZip);
        if (actualSha != expectedSha)
            return Error($"SHA-256 mismatch (expected {expectedSha}, got {actualSha})");
        ExtractArchiveBounded(tempZip, tempExtract);

        // 4. Locate the single top-level folder and verify it has funke-package.lua.
        string[] topDirs = Directory.GetDirectories(tempExtract);
        string[] topFiles = Directory.GetFiles(tempExtract);
        if (topDirs.Length != 1 || topFiles.Length != 0)
        {
            Cleanup();
            Console.Error.WriteLine("Invalid package zip: expected exactly one top-level folder containing the package");
            return 1;
        }

        string topDir = topDirs[0];
        string packageFolderName = Path.GetFileName(topDir);
        if (!IsSafePathComponent(packageFolderName))
        {
            Cleanup();
            Console.Error.WriteLine($"Invalid package zip: unsafe package folder name ({packageFolderName})");
            return 1;
        }
        string manifestPath = Path.Combine(topDir, "funke-package.lua");
        if (!File.Exists(manifestPath))
        {
            Cleanup();
            Console.Error.WriteLine($"Invalid package zip: funke-package.lua not found at {packageFolderName}/");
            return 1;
        }

        // 5. Move the extracted folder into <destPackagesDir>/<packageFolderName>/.
        string destPackageDir = Path.Combine(destPackagesDir, packageFolderName);
        if (Directory.Exists(destPackageDir))
        {
            Cleanup();
            Console.Error.WriteLine($"Package already installed: {packageFolderName}. Uninstall first to reinstall.");
            return 1;
        }

        Directory.Move(topDir, destPackageDir);
        installedDirToRollback = destPackageDir; // arm rollback in case of later failure

        // Cleanup the now-empty extract dir + temp zip.
        try { Directory.Delete(tempExtract, recursive: true); } catch { }
        try { File.Delete(tempZip); } catch { }

        // 6. Parse name + version from the manifest (best-effort regex; the
        // engine re-parses with its own loader after install). The manifest
        // was just moved out of tempExtract, so re-anchor the path to the
        // new install location before reading.
        string installedManifestPath = Path.Combine(destPackageDir, "funke-package.lua");
        string manifestText = await File.ReadAllTextAsync(installedManifestPath);
        string name = ExtractLuaField(manifestText, "name") ?? packageFolderName;
        string version = ExtractLuaField(manifestText, "version") ?? "";

        // Install succeeded — disarm the rollback so Cleanup() leaves it alone.
        installedDirToRollback = null;

        Console.WriteLine(JsonSerializer.Serialize(new
        {
            @event = "complete",
            success = true,
            name,
            version,
            installPath = destPackageDir
        }, jsonOpts));
        return 0;
    }
    catch (HttpRequestException ex)
    {
        Cleanup();
        Console.Error.WriteLine($"Registry download failed: {ex.Message}");
        return 1;
    }
    catch (TaskCanceledException)
    {
        Cleanup();
        Console.Error.WriteLine("Registry download timed out.");
        return 1;
    }
    catch (Exception ex)
    {
        Cleanup();
        Console.Error.WriteLine($"Registry download error: {ex.Message}");
        return 1;
    }
    finally { Cleanup(); }
}

async Task<long> DownloadBounded(string url, string destination)
{
    const long maxBytes = 512L * 1024L * 1024L;
    using var totalDeadline = new CancellationTokenSource(TimeSpan.FromMinutes(15));
    using var idleDeadline = CancellationTokenSource.CreateLinkedTokenSource(totalDeadline.Token);
    using var client = new HttpClient { Timeout = Timeout.InfiniteTimeSpan };
    client.DefaultRequestHeaders.UserAgent.ParseAdd("Funke-PackageTool/1.0");
    idleDeadline.CancelAfter(TimeSpan.FromSeconds(30));
    using var response = await client.GetAsync(url, HttpCompletionOption.ResponseHeadersRead, idleDeadline.Token);
    response.EnsureSuccessStatusCode();
    long? total = response.Content.Headers.ContentLength;
    if (total > maxBytes) throw new InvalidDataException($"Download exceeds the {maxBytes} byte limit.");
    await using var input = await response.Content.ReadAsStreamAsync(idleDeadline.Token);
    await using var output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None);
    var buffer = new byte[64 * 1024];
    long copied = 0;
    var lastEmit = DateTime.UtcNow - TimeSpan.FromSeconds(1);
    while (true)
    {
        idleDeadline.CancelAfter(TimeSpan.FromSeconds(30));
        int read = await input.ReadAsync(buffer, idleDeadline.Token);
        if (read == 0) break;
        if (read > maxBytes - copied) throw new InvalidDataException($"Download exceeds the {maxBytes} byte limit.");
        await output.WriteAsync(buffer.AsMemory(0, read), idleDeadline.Token);
        copied += read;
        if ((DateTime.UtcNow - lastEmit).TotalMilliseconds >= 100)
        {
            Console.WriteLine(JsonSerializer.Serialize(new {
                @event = "progress", stage = "download", bytes = copied, total,
                progress = total > 0 ? (double?)copied / total.Value : null
            }, jsonOpts));
            lastEmit = DateTime.UtcNow;
        }
    }
    if (total.HasValue && copied != total.Value) throw new InvalidDataException("Incomplete download.");
    return copied;
}

void RejectLinkedPath(string path)
{
    for (var current = new DirectoryInfo(Path.GetFullPath(path)); current != null; current = current.Parent)
        if (current.LinkTarget != null || (current.Exists && (current.Attributes & FileAttributes.ReparsePoint) != 0))
            throw new InvalidDataException($"Linked extraction destination is not supported: {current.FullName}");
}

void ExtractArchiveBounded(string archivePath, string destination)
{
    const long maxBytes = 2L * 1024L * 1024L * 1024L;
    const int maxEntries = 10000;
    using var archive = ZipFile.OpenRead(archivePath);
    if (archive.Entries.Count > maxEntries) throw new InvalidDataException($"Archive exceeds {maxEntries} entries.");
    var targets = new HashSet<string>(OperatingSystem.IsWindows() ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal);
    var entries = new List<(ZipArchiveEntry Entry, string Relative, bool Directory)>();
    long declared = 0;
    foreach (var entry in archive.Entries)
    {
        string relative = entry.FullName.Replace('\\', '/');
        bool directory = relative.EndsWith('/');
        string[] segments = relative.TrimEnd('/').Split('/');
        int unixType = (entry.ExternalAttributes >> 16) & 0xF000;
        if ((unixType != 0 && unixType != 0x8000 && unixType != 0x4000)
            || (entry.ExternalAttributes & (int)FileAttributes.ReparsePoint) != 0)
            throw new InvalidDataException($"Archive link or special file is not supported: {entry.FullName}");
        foreach (string segment in segments)
        {
            string basename = segment.Split('.')[0].ToUpperInvariant();
            bool reserved = basename is "CON" or "PRN" or "AUX" or "NUL"
                || (basename.Length == 4 && (basename.StartsWith("COM") || basename.StartsWith("LPT"))
                    && basename[3] >= '1' && basename[3] <= '9');
            if (string.IsNullOrEmpty(segment) || segment is "." or ".." || segment.EndsWith('.') || segment.EndsWith(' ')
                || segment.Any(c => c < 32 || "<>:\"|?*".Contains(c)) || reserved)
                throw new InvalidDataException($"Unsafe archive path: {entry.FullName}");
        }
        string normalized = string.Join('/', segments);
        if (!targets.Add(normalized)) throw new InvalidDataException($"Duplicate archive path: {entry.FullName}");
        if (entry.Length < 0 || entry.Length > maxBytes - declared || (directory && entry.Length != 0))
            throw new InvalidDataException($"Archive exceeds {maxBytes} extracted bytes or has invalid directory data.");
        declared += entry.Length;
        entries.Add((entry, normalized, directory));
    }
    RejectLinkedPath(destination);
    Directory.CreateDirectory(destination);
    var buffer = new byte[64 * 1024];
    var crcTable = new uint[256];
    for (uint index = 0; index < crcTable.Length; ++index)
    {
        uint value = index;
        for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((value & 1) != 0 ? 0xEDB88320u : 0u);
        crcTable[index] = value;
    }
    long extracted = 0;
    foreach (var item in entries)
    {
        string target = Path.Combine(destination, item.Relative.Replace('/', Path.DirectorySeparatorChar));
        if (item.Directory) { Directory.CreateDirectory(target); continue; }
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        using var input = item.Entry.Open();
        using var output = new FileStream(target, FileMode.CreateNew, FileAccess.Write, FileShare.None);
        long written = 0;
        uint crc = uint.MaxValue;
        int read;
        while ((read = input.Read(buffer)) != 0)
        {
            if (read > maxBytes - extracted || read > item.Entry.Length - written)
                throw new InvalidDataException("Archive data exceeds its declared size or extraction limit.");
            output.Write(buffer, 0, read);
            for (int index = 0; index < read; ++index) crc = (crc >> 8) ^ crcTable[(crc ^ buffer[index]) & 255];
            extracted += read; written += read;
        }
        if (written != item.Entry.Length) throw new InvalidDataException("Truncated archive entry.");
        if (~crc != item.Entry.Crc32) throw new InvalidDataException($"Archive checksum mismatch: {item.Entry.FullName}");
    }
}

bool IsSha256Hex(string value)
{
    return value.Length == 64 && value.All(Uri.IsHexDigit);
}

bool IsSafePathComponent(string value)
{
    if (string.IsNullOrWhiteSpace(value) || value == "." || value == ".."
        || value.Contains("..", StringComparison.Ordinal))
        return false;

    return value.IndexOfAny(Path.GetInvalidFileNameChars()) < 0
        && !value.Contains('/')
        && !value.Contains('\\')
        && !Path.IsPathRooted(value);
}

async Task<string> ComputeSha256(string path)
{
    using var sha = System.Security.Cryptography.SHA256.Create();
    await using var input = File.OpenRead(path);
    byte[] hash = await sha.ComputeHashAsync(input);
    return Convert.ToHexString(hash).ToLowerInvariant();
}

// Pulls a top-level string value from `funke-package.lua` for use in the
// install-success payload. Loose pattern match — the engine's loader is
// authoritative once the package is on disk.
string? ExtractLuaField(string lua, string field)
{
    var pattern = new System.Text.RegularExpressions.Regex(
        $@"\b{System.Text.RegularExpressions.Regex.Escape(field)}\s*=\s*""([^""\\]*(?:\\.[^""\\]*)*)""",
        System.Text.RegularExpressions.RegexOptions.None);
    var match = pattern.Match(lua);
    return match.Success ? match.Groups[1].Value : null;
}


// ── Helpers ──────────────────────────────────────────────────────

int Error(string msg)
{
    Console.Error.WriteLine(msg);
    return 1;
}

string Truncate(string? s, int max) =>
    s == null ? "" : s.Length <= max ? s : s[..max] + "...";


// ── NuGet API response models ────────────────────────────────────

class NuGetSearchResponse
{
    [JsonPropertyName("data")]
    public List<NuGetPackageResult>? Data { get; set; }
}

class NuGetPackageResult
{
    [JsonPropertyName("id")]
    public string Id { get; set; } = "";

    [JsonPropertyName("version")]
    public string Version { get; set; } = "";

    [JsonPropertyName("description")]
    public string? Description { get; set; }

    [JsonPropertyName("authors")]
    public List<string>? Authors { get; set; }

    [JsonPropertyName("totalDownloads")]
    public long TotalDownloads { get; set; }

    [JsonPropertyName("verified")]
    public bool Verified { get; set; }
}

class NuGetVersionsResponse
{
    [JsonPropertyName("versions")]
    public List<string>? Versions { get; set; }
}
