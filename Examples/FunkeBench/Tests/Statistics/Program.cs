using System;
using System.Globalization;
using System.IO;
using System.Linq;
using Funke;

int assertions = 0;
void Check(bool condition, string message)
{
    assertions++;
    if (!condition) throw new Exception(message);
}
void Equal(double expected, double actual, string message) => Check(Math.Abs(expected - actual) < 1e-9, message);
void Reject(Action action, string message)
{
    try { action(); }
    catch (ArgumentException) { assertions++; return; }
    throw new Exception(message);
}
BenchmarkResult Result(BenchmarkKind kind, params double[] samples) => new(kind,
    new BenchmarkSettings { Count = 100 }, samples, 2, 0.3, 0.7, "frame, including \"UI\"", 1440, 900, false, 0);

var unordered = new[] { 8.0, 2.0, 4.0, 6.0 };
var even = Result(BenchmarkKind.Move, unordered);
Equal(5, even.AverageMs, "Mean must use every sample.");
Equal(5, even.MedianMs, "Even median must average the middle pair.");
Equal(2, even.MinMs, "Minimum.");
Equal(8, even.MaxMs, "Maximum.");
Equal(8, even.P95Ms, "Small sample P95 nearest rank.");
Equal(20000, even.EntitiesPerSecond, "Operation throughput uses entity count.");
Equal(0, even.FPS, "Operation results must not expose FPS.");
Check(unordered.SequenceEqual(new[] { 8.0, 2.0, 4.0, 6.0 }), "Result must not reorder the caller's samples.");

var render = Result(BenchmarkKind.Render2D, 10, 30, 20);
Equal(20, render.MedianMs, "Odd median.");
Equal(50, render.FPS, "FPS is reciprocal of mean frame duration.");
Equal(0.06, render.CapturedSeconds, "Render duration is the sum of captured frames.");
Equal(0, render.EntitiesPerSecond, "Render results must not report operation throughput.");
Equal(19, Result(BenchmarkKind.Create, Enumerable.Range(1, 20).Select(i => (double)i).ToArray()).P95Ms, "P95 nearest-rank boundary.");
Equal(7, Result(BenchmarkKind.Destroy, 7).MedianMs, "Single sample median.");
Equal(0, Result(BenchmarkKind.Render3D, 0).FPS, "Zero duration must avoid division by zero.");
Reject(() => Result(BenchmarkKind.Create), "Empty results must be rejected.");
foreach (double invalid in new[] { -1.0, double.NaN, double.PositiveInfinity, double.NegativeInfinity })
    Reject(() => Result(BenchmarkKind.Create, invalid), "Reject invalid sample duration.");
Reject(() => Result(BenchmarkKind.Create, double.MaxValue, double.MaxValue), "Reject duration overflow.");
Reject(() => Result((BenchmarkKind)99, 1), "Reject unknown benchmark kinds.");
var animatedOperation = new BenchmarkResult(BenchmarkKind.Create, new BenchmarkSettings { Animate = true },
    new[] { 1.0 }, 0, 0, 0, "test", 1, 1, false, 0);
Check(!animatedOperation.Animate, "Operations must not claim to animate render entities.");

var settings = new BenchmarkSettings();
var snapshot = settings.Snapshot();
settings.Count = 7;
Check(snapshot.Count == 10000, "Snapshot must be independent of live controls.");
Reject(() => new BenchmarkSettings { Count = 0 }.Snapshot(), "Reject zero entities.");
Reject(() => new BenchmarkSettings { Count = BenchmarkSettings.MaximumEntities + 1 }.Snapshot(), "Enforce entity cap.");
Check(new BenchmarkSettings { Count = BenchmarkSettings.MaximumEntities }.Snapshot().Count == 100000, "Allow the UI's maximum count.");
Reject(() => new BenchmarkSettings { Iterations = 0 }.Snapshot(), "Reject zero samples.");
Reject(() => new BenchmarkSettings { DurationSeconds = double.NaN }.Snapshot(), "Reject NaN capture length.");
Reject(() => new BenchmarkSettings { WarmupSeconds = double.PositiveInfinity }.Snapshot(), "Reject infinite warmup.");
Reject(() => new BenchmarkSettings { PlaybackMode = (EntityCommandBufferPlaybackMode)99 }.Snapshot(), "Reject unknown playback mode.");
Check(new BenchmarkSettings { PlaybackMode = EntityCommandBufferPlaybackMode.Sequential }.Snapshot().PlaybackMode == EntityCommandBufferPlaybackMode.Sequential,
    "Preserve explicit native playback independently of recording jobs.");
Reject(() => new BenchmarkResult(BenchmarkKind.Create, new BenchmarkSettings(), new[] { 1.0 }, double.NaN, 0, 0, "test", 1, 1, false, 0),
    "Reject invalid setup time.");

string directory = Path.Combine(Path.GetTempPath(), "FunkeBench-statistics-" + Guid.NewGuid().ToString("N"));
string? exportedPath = null;
string? repeatedPath = null;
CultureInfo previous = CultureInfo.CurrentCulture;
try
{
    CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("de-DE");
    exportedPath = BenchmarkExport.WriteCsv(directory, new[] { render });
    string csv = File.ReadAllText(exportedPath);
    Check(csv.Contains("\"0.3\""), "CSV numbers must use invariant decimal points.");
    Check(csv.Contains("\"Test, \"\"renderer\"\"\""), "CSV must quote embedded commas and quotes.");
    Check(csv.Contains("\"frame, including \"\"UI\"\"\""), "CSV timing scope quoting.");
    Check(csv.Split('\n', StringSplitOptions.RemoveEmptyEntries).Length == 2, "CSV header and one result row.");
    repeatedPath = BenchmarkExport.WriteCsv(directory, new[] { even });
    Check(repeatedPath != exportedPath, "Repeated exports must have distinct names.");
    Check(File.ReadAllText(exportedPath) == csv, "Repeated export must preserve previous results.");
}
finally
{
    CultureInfo.CurrentCulture = previous;
    if (exportedPath != null) File.Delete(exportedPath);
    if (repeatedPath != null) File.Delete(repeatedPath);
    if (Directory.Exists(directory)) Directory.Delete(directory);
}
Console.WriteLine($"PASS: {assertions} statistics, settings, and CSV assertions.");
