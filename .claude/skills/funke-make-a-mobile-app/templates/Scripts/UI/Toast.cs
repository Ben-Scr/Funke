using Funke;
using Funke.UI;

// Short confirmation pill above the tab bar that fades out on its own (Canvas/Toast, built by ui_gen.toast).
// It has no Interactable, so taps go straight through it.
public sealed class Toast : EntityScript
{
    private const float k_HoldSeconds = 2.2f;
    private const float k_FadeSeconds = 0.3f;

    private static Toast? s_Instance;
    private Image? m_Background;
    private TextRenderer? m_Text;
    private Label m_Label = null!;
    private float m_Age = float.MaxValue;

    public override void OnAwake() => s_Instance = this;

    public override void OnDestroy()
    {
        if (s_Instance == this) s_Instance = null;
    }

    public override void OnStart()
    {
        m_Background = Entity.GetComponent<Image>();
        m_Text = Ui.Get<TextRenderer>(Entity, "Label");
        m_Label = new Label(m_Text);
        SetAlpha(0);
    }

    public static void Show(string message)
    {
        if (s_Instance == null)
            return;
        s_Instance.m_Label.Set(message);
        s_Instance.m_Age = 0;
    }

    public override void OnUpdate()
    {
        if (m_Age > k_HoldSeconds + k_FadeSeconds)
            return;
        m_Age += Time.DeltaTime;
        float alpha = m_Age < k_HoldSeconds ? 1 : 1 - (m_Age - k_HoldSeconds) / k_FadeSeconds;
        SetAlpha(System.MathF.Max(0, alpha));
    }

    private void SetAlpha(float alpha)
    {
        if (m_Background != null) m_Background.Color = new Vector4(1, 1, 1, alpha);
        if (m_Text != null) m_Text.Color = new Vector4(0.07f, 0.07f, 0.07f, alpha);
    }
}
