using System;
using System.Collections.Generic;
using Funke;

public sealed class FactoryCrafting : EntityScript
{
    public readonly List<CraftJob> Queue = new();
    public string Error(RecipeDefinition recipe)
    {
        var s = FactorySession.Instance!;
        if (s.Creative) return "";
        if (recipe.CreativeOnly) return "Gather this from a deposit.";
        if (Queue.Count >= 30) return "Crafting queue is full.";
        foreach (var pair in Costs(recipe))
            if (s.Inventory.Count(pair.Key) < pair.Value) return "Missing " + s.FindItem(pair.Key)?.DisplayName;
        return "";
    }
    public static Dictionary<string, int> Costs(RecipeDefinition recipe)
    {
        var costs = new Dictionary<string, int>();
        foreach (var ingredient in recipe.Ingredients)
            if (ingredient.Item != null) costs[ingredient.Item.Id] = costs.GetValueOrDefault(ingredient.Item.Id) + ingredient.Count;
        return costs;
    }
    public bool Enqueue(RecipeDefinition recipe)
    {
        var s = FactorySession.Instance;
        if (s?.Rules == null || !s.Running || recipe.Result == null) return false;
        string error = Error(recipe);
        if (error.Length > 0) { s.Notice = error; return false; }
        if (s.Creative)
        {
            bool added = s.Inventory.Add(recipe.Result, recipe.Amount, s.Rules);
            s.Notice = added ? $"Created {recipe.Amount} {recipe.Result.DisplayName}." : "Inventory full.";
            return added;
        }
        foreach (var pair in Costs(recipe)) s.Inventory.Take(pair.Key, pair.Value);
        Queue.Add(new CraftJob { Recipe = recipe.Id, Remaining = Math.Max(.05f, recipe.Seconds) });
        s.Notice = "Queued " + recipe.Result.DisplayName;
        return true;
    }
    public void Tick(float dt)
    {
        var s = FactorySession.Instance;
        if (s?.Rules == null || !s.Running || Queue.Count == 0) return;
        var job = Queue[0];
        var recipe = s.FindRecipe(job.Recipe);
        if (recipe?.Result == null) return;
        job.Remaining = Math.Max(0, job.Remaining - dt);
        if (job.Remaining > 0) return;
        if (!s.Inventory.Add(recipe.Result, recipe.Amount, s.Rules)) { s.Notice = "Craft ready - make space in inventory."; return; }
        Queue.RemoveAt(0);
        s.Notice = $"Crafted {recipe.Amount} {recipe.Result.DisplayName}.";
    }
    public bool CancelLast()
    {
        var s = FactorySession.Instance;
        if (s == null || Queue.Count == 0) return false;
        var recipe = s.FindRecipe(Queue[^1].Recipe);
        if (recipe == null || !s.Inventory.AddAll(Costs(recipe), s)) { s.Notice = "Make room for the refunded materials."; return false; }
        Queue.RemoveAt(Queue.Count - 1); return true;
    }
}

public sealed class CraftJob
{
    public string Recipe = "";
    public float Remaining;
}
