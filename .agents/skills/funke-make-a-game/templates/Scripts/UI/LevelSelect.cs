using Funke;

// Builds one card per catalog entry under Grid (a GridLayoutGroup / VerticalLayoutGroup entity).
// The card prefab carries LevelCard; its Button's onClick is bound to LevelCard.Play in the prefab JSON.
public sealed class LevelSelect : EntityScript
{
    public LevelCatalog? Catalog;
    public Entity? CardPrefab;             // "prefab:<guid>"
    public Entity? Grid;

    public override void OnStart()
    {
        if (Catalog is not { } catalog || CardPrefab is not { } prefab || Grid is not { } grid) return;
        for (int i = 0; i < catalog.Levels.Count; i++)
        {
            var level = catalog.Levels[i];
            var card = Instantiate(prefab, grid);
            card.GetComponent<LevelCard>()?.Show(level, LevelProgress.IsUnlocked(catalog, i),
                                                LevelProgress.Get(level.Id));
        }
    }
}
