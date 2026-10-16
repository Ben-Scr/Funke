using System;
using System.Collections.Generic;
using Funke;
using Funke.UI;

// Example app data: notes saved with SaveData (JSON in the app's data folder).
public sealed class Note
{
    public string Text { get; set; } = "";
    public DateTime Created { get; set; } = DateTime.Now;
}

public static class NoteStore
{
    private const string k_Slot = "notes";
    private static List<Note>? s_Notes;

    public static event Action? Changed;

    public static List<Note> All
    {
        get
        {
            if (s_Notes == null)
                s_Notes = SaveData.TryLoad(k_Slot, out List<Note>? saved) && saved != null ? saved : new List<Note>();
            return s_Notes;
        }
    }

    public static void Add(string text)
    {
        if (string.IsNullOrWhiteSpace(text)) return;
        All.Insert(0, new Note { Text = text.Trim() });
        Save();
    }

    public static void Remove(Note note)
    {
        All.Remove(note);
        Save();
    }

    public static void Clear()
    {
        All.Clear();
        Save();
    }

    private static void Save()
    {
        SaveData.Save(k_Slot, All);
        Changed?.Invoke();
    }
}

// The list on the Notes tab: VirtualList over Note, rows built by gen_example.note_row.
public sealed class NoteList : VirtualList<Note>
{
    private sealed class Parts
    {
        public Label Title = null!;
        public Label Subtitle = null!;
    }

    protected override object? CreateRow(Entity row) => new Parts
    {
        Title = new Label(Ui.Get<TextRenderer>(row, "Title")),
        Subtitle = new Label(Ui.Get<TextRenderer>(row, "Subtitle")),
    };

    protected override void BindRow(Row row, Note item, int index, bool changed)
    {
        if (!changed) return;
        var parts = (Parts)row.State!;
        parts.Title.Set(Ui.Ellipsize(item.Text, 40));
        parts.Subtitle.Set(item.Created.ToString("dd.MM.yyyy HH:mm"));
    }

    protected override void OnItemClicked(Note item, int index) => Toast.Show(Ui.Ellipsize(item.Text, 40));

    public void OnMoreClicked(int slot)
    {
        if (!TryGetSlot(slot, out Note note, out _)) return;
        ActionSheet.Instance?.Show(Ui.Ellipsize(note.Text, 40), note.Created.ToString("dd.MM.yyyy"), new List<ActionSheet.Option>
        {
            new(null, "Notiz löschen", () => { NoteStore.Remove(note); Toast.Show("Notiz gelöscht"); }),
        });
    }
}

// Notes tab: title, "+" to add, the list and an empty-state hint.
public sealed class NotesPage : EntityScript
{
    private NoteList? m_List;
    private Entity? m_Empty;

    public override void OnStart()
    {
        m_List = Ui.Child(Entity, "List")?.GetComponent<NoteList>();
        m_Empty = Ui.Child(Entity, "Empty");
        NoteStore.Changed += Refresh;
        Refresh();
    }

    public override void OnDestroy() => NoteStore.Changed -= Refresh;

    private void Refresh()
    {
        m_List?.SetItems(NoteStore.All, resetScroll: false);
        Ui.Show(m_Empty, NoteStore.All.Count == 0);
    }

    public void OnAdd() => InputDialog.Instance?.Show("Neue Notiz", "", "Speichern", text =>
    {
        NoteStore.Add(text);
        Toast.Show("Notiz gespeichert");
    }, "Was gibt's?");
}

// Settings tab: a switch and a destructive action.
public sealed class SettingsPage : EntityScript
{
    private Image? m_Switch;
    private RectTransform? m_Knob;
    private bool m_On;

    public override void OnStart()
    {
        m_Switch = Ui.Get<Image>(Entity, "Reminder/Switch");
        m_Knob = Ui.Get<RectTransform>(Entity, "Reminder/Switch/Knob");
        m_On = PlayerPrefs.GetBool("notes.reminder", true);
        Apply();
    }

    public void OnToggleReminder()
    {
        m_On = !m_On;
        PlayerPrefs.SetBool("notes.reminder", m_On);
        Apply();
    }

    public void OnClearAll() => ActionSheet.Instance?.Show("Alle Notizen löschen?", "Das kann nicht rückgängig gemacht werden.", new List<ActionSheet.Option>
    {
        new(null, "Ja, alle löschen", () => { NoteStore.Clear(); Toast.Show("Alle Notizen gelöscht"); }),
        new(null, "Abbrechen", () => { }),
    });

    private void Apply()
    {
        if (m_Switch != null) m_Switch.Color = m_On ? Ui.Accent : Ui.Rgb(0x6A, 0x6A, 0x6A);
        if (m_Knob != null) m_Knob.AnchoredPosition = new Vector2(m_On ? 22 : -22, 0);
    }
}
