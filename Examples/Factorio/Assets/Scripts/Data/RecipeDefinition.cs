using System.Collections.Generic;
using Funke;

[CreateDataAsset("Factory/Recipe")]
public sealed class RecipeDefinition : DataAsset
{
    public string Id = "";
    public ItemDefinition? Result;
    public int Amount = 1;
    public float Seconds = 1;
    public bool CreativeOnly;
    public List<Ingredient> Ingredients = new();
}

public sealed class Ingredient
{
    public ItemDefinition? Item;
    public int Count = 1;
}
