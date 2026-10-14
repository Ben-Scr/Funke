using System;
using System.Collections.Generic;
using Index;
using Index.UI;

// A scrolling list over any number of items with a fixed pool of row entities (Content/Row0..RowN authored in the
// scene). Rows are recycled for whatever is in view, so 10 000 items cost one screen of entities. An optional
// header (at the top of Content, HeaderHeight tall) scrolls away with the list.
//
// Subclass per item type, e.g. `public sealed class NoteList : VirtualList<Note>`, and put the subclass on the
// ScrollRect viewport. Rows' buttons call OnRowClicked(slot) on it.
public abstract class VirtualList<T> : EntityScript
{
    public float RowHeight = 140;
    public float HeaderHeight;
    public float BottomPadding = 40;

    protected sealed class Row
    {
        public Entity Entity = null!;
        public RectTransform Rect = null!;
        public Image? Background;
        public Interactable? Interact;
        public int Index = -2;
        public bool Highlight;
        /// <summary>Per-row cache for the subclass (labels, images) built in CreateRow.</summary>
        public object? State;
    }

    private Row[] m_Rows = Array.Empty<Row>();
    private RectTransform? m_Content;
    private bool m_Dirty = true;

    protected IReadOnlyList<T> Items { get; private set; } = Array.Empty<T>();

    /// <summary>Look up the row's parts once (e.g. new Label(Ui.Get&lt;TextRenderer&gt;(row, "Title"))).</summary>
    protected abstract object? CreateRow(Entity row);

    /// <summary>Fill a row. `changed` is true when the row now shows a different item than last frame.</summary>
    protected abstract void BindRow(Row row, T item, int index, bool changed);

    protected virtual void OnItemClicked(T item, int index) { }

    public override void OnStart()
    {
        Entity? content = Ui.Child(Entity, "Content");
        m_Content = content?.GetComponent<RectTransform>();
        var rows = new List<Row>();
        foreach (Entity e in content?.GetChildren() ?? Array.Empty<Entity>())
        {
            if (!e.Name.StartsWith("Row"))
                continue;
            var row = new Row
            {
                Entity = e,
                Rect = e.GetComponent<RectTransform>()!,
                Background = e.GetComponent<Image>(),
                Interact = e.GetComponent<Interactable>(),
            };
            row.State = CreateRow(e);
            rows.Add(row);
        }
        m_Rows = rows.ToArray();
    }

    public void SetItems(IReadOnlyList<T> items, bool resetScroll = true)
    {
        Items = items;
        m_Dirty = true;
        if (resetScroll && m_Content != null)
            m_Content.AnchoredPosition = new Vector2(m_Content.AnchoredPosition.X, 0);
    }

    /// <summary>Re-binds every visible row (after the items changed in place).</summary>
    public void Refresh() => m_Dirty = true;

    public override void OnUpdate()
    {
        if (m_Content == null)
            return;
        if (m_Dirty)
        {
            m_Dirty = false;
            m_Content.SizeDelta = new Vector2(m_Content.SizeDelta.X, HeaderHeight + Items.Count * RowHeight + BottomPadding);
            foreach (Row row in m_Rows) row.Index = -2;
        }

        float scrolled = m_Content.AnchoredPosition.Y - HeaderHeight;
        int first = Math.Max(0, (int)MathF.Floor(scrolled / RowHeight) - 1);
        for (int slot = 0; slot < m_Rows.Length; ++slot)
        {
            Row row = m_Rows[slot];
            int index = first + slot;
            bool visible = index < Items.Count;
            Ui.Show(row.Entity, visible);
            if (!visible)
            {
                row.Index = -1;
                continue;
            }
            bool changed = row.Index != index;
            if (changed)
            {
                row.Index = index;
                row.Rect.AnchoredPosition = new Vector2(0, -HeaderHeight - index * RowHeight);
            }
            BindRow(row, Items[index], index, changed);

            bool pressed = row.Interact?.IsPressed == true;
            if (pressed != row.Highlight && row.Background != null)
            {
                row.Highlight = pressed;
                row.Background.Color = pressed ? Ui.Pressed : Ui.Clear;
            }
        }
    }

    public void OnRowClicked(int slot)
    {
        if (slot >= 0 && slot < m_Rows.Length && m_Rows[slot].Index >= 0 && m_Rows[slot].Index < Items.Count)
            OnItemClicked(Items[m_Rows[slot].Index], m_Rows[slot].Index);
    }

    /// <summary>The item shown in a pool slot, for secondary row buttons (e.g. a "..." button passing its slot).</summary>
    protected bool TryGetSlot(int slot, out T item, out int index)
    {
        index = slot >= 0 && slot < m_Rows.Length ? m_Rows[slot].Index : -1;
        item = index >= 0 && index < Items.Count ? Items[index] : default!;
        return index >= 0 && index < Items.Count;
    }
}
