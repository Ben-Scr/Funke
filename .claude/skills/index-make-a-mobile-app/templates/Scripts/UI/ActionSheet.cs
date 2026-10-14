using System;
using System.Collections.Generic;
using Index;
using Index.UI;

// Bottom sheet with a title, subtitle and up to six option rows (Canvas/ActionSheet, built by ui_gen.action_sheet).
public sealed class ActionSheet : EntityScript, IBackHandler
{
    public readonly record struct Option(Texture? Icon, string Label, Action Run);

    private const float k_RowHeight = 132;
    private const int k_MaxOptions = 6;

    public static ActionSheet? Instance { get; private set; }
    public bool IsOpen => Entity.IsEnabled;

    private RectTransform? m_Panel;
    private Label m_Title = null!;
    private Label m_Subtitle = null!;
    private readonly Entity?[] m_Rows = new Entity?[k_MaxOptions];
    private readonly Image?[] m_Icons = new Image?[k_MaxOptions];
    private readonly Label[] m_Labels = new Label[k_MaxOptions];
    private readonly List<Option> m_Options = new();

    public override void OnAwake()
    {
        Instance = this;
        Router.RegisterBackHandler(this);
    }

    public override void OnDestroy()
    {
        Router.UnregisterBackHandler(this);
        if (Instance == this) Instance = null;
    }

    public override void OnStart()
    {
        m_Panel = Ui.Get<RectTransform>(Entity, "Panel");
        m_Title = new Label(Ui.Get<TextRenderer>(Entity, "Panel/Title"));
        m_Subtitle = new Label(Ui.Get<TextRenderer>(Entity, "Panel/Subtitle"));
        for (int i = 0; i < k_MaxOptions; ++i)
        {
            m_Rows[i] = Ui.Child(Entity, $"Panel/Option{i}");
            m_Icons[i] = Ui.Get<Image>(m_Rows[i], "Icon");
            m_Labels[i] = new Label(Ui.Get<TextRenderer>(m_Rows[i], "Label"));
        }
        Ui.Show(Entity, false);
    }

    public void Show(string title, string subtitle, IReadOnlyList<Option> options)
    {
        // Enable the sheet before its rows: enabling a parent restores the children's authored (disabled) state.
        Ui.Show(Entity, true);
        m_Options.Clear();
        for (int i = 0; i < options.Count && i < k_MaxOptions; ++i) m_Options.Add(options[i]);
        m_Title.Set(Ui.Ellipsize(title, 40));
        m_Subtitle.Set(Ui.Ellipsize(subtitle, 50));
        for (int i = 0; i < k_MaxOptions; ++i)
        {
            bool used = i < m_Options.Count;
            Ui.Show(m_Rows[i], used);
            if (!used) continue;
            m_Labels[i].Set(m_Options[i].Label);
            if (m_Icons[i] != null && m_Options[i].Icon != null) m_Icons[i]!.Texture = m_Options[i].Icon;
        }
        // The panel stands on the bottom edge and grows upwards with its options.
        if (m_Panel != null)
            m_Panel.SizeDelta = new Vector2(m_Panel.SizeDelta.X, 196 + m_Options.Count * k_RowHeight + 80);
    }

    public void Close() => Ui.Show(Entity, false);

    public bool HandleBack()
    {
        if (!IsOpen) return false;
        Close();
        return true;
    }

    public void OnOption(int index)
    {
        if (index < 0 || index >= m_Options.Count)
            return;
        Action run = m_Options[index].Run;
        Close();
        run();
    }
}
