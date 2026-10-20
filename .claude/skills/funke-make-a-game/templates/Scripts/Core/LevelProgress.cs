using System.Collections.Generic;
using Funke;

// Per-level results in one SaveData slot. Read from disk on every call (menus only), so there is no
// static cache that could go stale between editor Play sessions or after "prefs.clear".
public static class LevelProgress
{
    const string k_Slot = "progress";

    public sealed class LevelResult
    {
        public int Stars;
        public float BestTime;          // 0 = no time recorded
        public int BestScore;
    }

    public sealed class Data
    {
        public Dictionary<string, LevelResult> Levels = new();
    }

    static Data LoadData() => SaveData.Load(k_Slot, new Data());

    public static LevelResult? Get(string levelId) =>
        LoadData().Levels.TryGetValue(levelId, out var r) ? r : null;

    public static bool IsCompleted(string levelId) => Get(levelId) != null;

    // The first level is always open; every other one opens when the previous one is completed.
    public static bool IsUnlocked(LevelCatalog catalog, int index) =>
        index <= 0 || (index < catalog.Levels.Count && IsCompleted(catalog.Levels[index - 1].Id));

    // Keeps the best values; returns true when anything improved.
    public static bool Complete(string levelId, int stars, float time = 0f, int score = 0)
    {
        var data = LoadData();
        bool first = !data.Levels.TryGetValue(levelId, out var r);
        r ??= new LevelResult();
        bool fasterTime = time > 0f && (r.BestTime <= 0f || time < r.BestTime);
        bool better = first || stars > r.Stars || fasterTime || score > r.BestScore;
        r.Stars = System.Math.Max(r.Stars, stars);
        if (fasterTime) r.BestTime = time;
        r.BestScore = System.Math.Max(r.BestScore, score);
        data.Levels[levelId] = r;
        SaveData.Save(k_Slot, data);
        Log.Info($"[LevelProgress] {levelId} completed: stars={stars} improved={better}");
        return better;
    }

    public static void Reset() => SaveData.Delete(k_Slot);
}
