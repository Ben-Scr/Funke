using System.Collections.Generic;
using Funke;
using Funke.UI;

// Navigation for a single-scene app: tab pages under Canvas/Pages/<Tab>, pages pushed over them (details,
// settings), and the Android back button (arrives as Escape). Tab buttons live at Canvas/Nav/<Tab>/{Icon,Label}.
public sealed class Router : EntityScript
{
    public static Router? Instance { get; private set; }

    /// <summary>Comma-separated tab page names, in tab-bar order.</summary>
    public string Tabs = "Home";
    /// <summary>Comma-separated pages that start hidden and are only shown via Push.</summary>
    public string StackPages = "";

    private static readonly List<IBackHandler> s_BackHandlers = new();

    private readonly List<Entity?> m_Tabs = new();
    private readonly List<Image?> m_TabIcons = new();
    private readonly List<Label> m_TabLabels = new();
    private readonly Stack<Entity> m_Stack = new();
    private int m_Tab;

    public int CurrentTab => m_Tab;

    public static void RegisterBackHandler(IBackHandler handler) { if (!s_BackHandlers.Contains(handler)) s_BackHandlers.Add(handler); }
    public static void UnregisterBackHandler(IBackHandler handler) => s_BackHandlers.Remove(handler);

    public override void OnAwake() => Instance = this;

    public override void OnDestroy()
    {
        if (Instance == this) Instance = null;
    }

    public override void OnStart()
    {
        foreach (string name in Tabs.Split(','))
        {
            string tab = name.Trim();
            m_Tabs.Add(Ui.Child(Entity, $"Pages/{tab}"));
            m_TabIcons.Add(Ui.Get<Image>(Entity, $"Nav/{tab}/Icon"));
            m_TabLabels.Add(new Label(Ui.Get<TextRenderer>(Entity, $"Nav/{tab}/Label")));
        }
        // Every page starts enabled in the scene so its scripts run OnStart; hide the stacked ones now.
        foreach (string name in StackPages.Split(','))
            if (name.Trim().Length > 0)
                Ui.Show(Ui.Child(Entity, $"Pages/{name.Trim()}"), false);
        SelectTab(0);
    }

    public override void OnUpdate()
    {
        if (Input.GetKeyDown(KeyCode.Escape))
            Back();
    }

    public void SelectTab(int tab)
    {
        while (m_Stack.Count > 0) Ui.Show(m_Stack.Pop(), false);
        m_Tab = tab;
        for (int i = 0; i < m_Tabs.Count; ++i)
        {
            bool on = i == tab;
            Ui.Show(m_Tabs[i], on);
            m_TabLabels[i].SetColor(on ? Ui.White : Ui.Muted);
            if (m_TabIcons[i] is { } icon) icon.Color = on ? Ui.White : Ui.Muted;
        }
    }

    /// <summary>Shows a page over the current tab. The covered page is disabled so it can't catch taps.</summary>
    public void Push(Entity? page)
    {
        if (page == null || (m_Stack.Count > 0 && m_Stack.Peek().ID == page.ID))
            return;
        Ui.Show(m_Stack.Count > 0 ? m_Stack.Peek() : m_Tabs[m_Tab], false);
        m_Stack.Push(page);
        Ui.Show(page, true);
    }

    public void Push(string pageName) => Push(Ui.Child(Entity, $"Pages/{pageName}"));

    public void Pop()
    {
        if (m_Stack.Count == 0)
            return;
        Ui.Show(m_Stack.Pop(), false);
        Ui.Show(m_Stack.Count > 0 ? m_Stack.Peek() : m_Tabs[m_Tab], true);
    }

    /// <summary>Closes the top-most thing: an overlay, then a pushed page, then returns to the first tab.</summary>
    public void Back()
    {
        for (int i = s_BackHandlers.Count - 1; i >= 0; --i)
            if (s_BackHandlers[i].HandleBack())
                return;
        if (m_Stack.Count > 0) { Pop(); return; }
        if (m_Tab != 0) SelectTab(0);
    }
}
