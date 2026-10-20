using System.Collections.Generic;
using Funke;
using Funke.Coroutines;

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
        var cards = new List<Entity>();
        foreach (var _ in catalog.Levels)
            cards.Add(Instantiate(prefab, grid));

        // Scripts that come with a prefab get their instance a frame or two after Instantiate: wait for them.
        RunCoroutine(async () =>
        {
            for (int f = 0; f < 10 && !cards.TrueForAll(c => c.GetComponent<LevelCard>() != null); f++)
                await new WaitForNextFrame();
            for (int i = 0; i < cards.Count; i++)
            {
                var level = catalog.Levels[i];
                cards[i].GetComponent<LevelCard>()?.Show(level, LevelProgress.IsUnlocked(catalog, i),
                                                         LevelProgress.Get(level.Id));
            }
        });
    }
}
