using System;
using System.Collections.Generic;

public sealed class FactoryInventory
{
    public Dictionary<string, int> Items = new();
    public int Count(string id) => Items.GetValueOrDefault(id);
    public int UsedSlots(FactoryRules rules)
    {
        long slots = 0;
        foreach (var item in rules.Items) slots += ((long)Count(item.Id) + Math.Max(1, item.StackSize) - 1) / Math.Max(1, item.StackSize);
        return (int)Math.Min(int.MaxValue,slots);
    }
    public bool Add(ItemDefinition item, int amount, FactoryRules rules)
    {
        if (amount < 0 || amount > int.MaxValue - Count(item.Id)) return false;
        int before = Count(item.Id);
        Items[item.Id] = before + amount;
        if (UsedSlots(rules) <= rules.InventorySlots) return true;
        Items[item.Id] = before;
        return false;
    }
    public bool Take(string id, int amount)
    {
        if (amount < 0 || Count(id) < amount) return false;
        Items[id] = Count(id) - amount;
        return true;
    }
    public bool AddAll(Dictionary<string, int> items, FactorySession session)
    {
        var before = new Dictionary<string, int>(Items);
        foreach (var pair in items)
            if (session.FindItem(pair.Key) is not { } item || !Add(item, pair.Value, session.Rules!))
            { Items = before; return false; }
        return true;
    }
}
