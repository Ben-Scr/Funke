using System;
using Index;
using Index.UI;

// Full-screen text prompt ("Name your playlist") with Cancel / Confirm (Canvas/InputDialog, built by ui_gen.input_dialog).
public sealed class InputDialog : EntityScript, IBackHandler
{
    public static InputDialog? Instance { get; private set; }
    public bool IsOpen => Entity.IsEnabled;

    private InputField? m_Field;
    private Label m_Title = null!;
    private Label m_Confirm = null!;
    private Action<string>? m_OnConfirm;

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
        m_Field = Ui.Get<InputField>(Entity, "Field");
        m_Title = new Label(Ui.Get<TextRenderer>(Entity, "Title"));
        m_Confirm = new Label(Ui.Get<TextRenderer>(Entity, "Confirm/Label"));
        Ui.Show(Entity, false);
    }

    public override void OnUpdate()
    {
        if (m_Field != null && m_Field.SubmittedThisFrame)
            Confirm();
    }

    public void Show(string title, string initial, string confirmLabel, Action<string> onConfirm, string placeholder = "")
    {
        Ui.Show(Entity, true);
        m_Title.Set(title);
        m_Confirm.Set(confirmLabel);
        m_OnConfirm = onConfirm;
        if (m_Field != null)
        {
            m_Field.PlaceholderText = placeholder;
            m_Field.Text = initial;
            m_Field.IsFocused = true; // opens the soft keyboard on phones
        }
    }

    public void Confirm()
    {
        string value = m_Field?.Text ?? "";
        Action<string>? run = m_OnConfirm;
        Close();
        run?.Invoke(value);
    }

    public void Cancel() => Close();

    public bool HandleBack()
    {
        if (!IsOpen) return false;
        Close();
        return true;
    }

    private void Close()
    {
        if (m_Field != null) m_Field.IsFocused = false;
        m_OnConfirm = null;
        Ui.Show(Entity, false);
    }
}
