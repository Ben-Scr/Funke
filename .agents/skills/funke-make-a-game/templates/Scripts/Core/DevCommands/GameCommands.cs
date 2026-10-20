using Funke;

// Test hooks for every game: console, --exec, input-script `command` steps and `funke editor exec`.
// Add per-feature command classes next to this one (player.tp, game.win, economy.add, ...).
public static class GameCommands
{
    [DevCommand("level.load", Description = "Load a level by catalog index (0-based) or scene name")]
    static string LoadLevel(string level)
    {
        if (int.TryParse(level, out int index))
        {
            foreach (var catalog in AssetManager.FindAll<LevelCatalog>())
                if (index >= 0 && index < catalog.Levels.Count && catalog.Levels[index].Scene is { } scene)
                {
                    SceneManager.LoadScene(scene.Guid);
                    return $"loading {catalog.Levels[index].Id}";
                }
            return $"no level {index} in a LevelCatalog";
        }
        SceneManager.LoadScene(level);
        return $"loading {level}";
    }

    [DevCommand("progress.unlockall", Description = "Mark every catalog level as completed (1 star)")]
    static string UnlockAll()
    {
        int n = 0;
        foreach (var catalog in AssetManager.FindAll<LevelCatalog>())
            foreach (var l in catalog.Levels) { LevelProgress.Complete(l.Id, 1); n++; }
        return $"{n} levels unlocked";
    }

    [DevCommand("progress.reset", Description = "Delete all level results")]
    static string ResetProgress() { LevelProgress.Reset(); return "progress reset"; }

    [DevCommand("game.state", Description = "One-line state for test logs")]
    static string State()
    {
        var scene = SceneManager.GetActiveScene()?.Name ?? "-";
        return $"scene={scene} timescale={Time.TimeScale.ToString(System.Globalization.CultureInfo.InvariantCulture)} muted={GameSettings.Muted}";
    }
}
