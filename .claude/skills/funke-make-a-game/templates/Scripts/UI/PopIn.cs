using Funke;
using Funke.Coroutines;
using Funke.UI;

// Panels pop in when enabled: scale 0.86 -> 1 with a small overshoot, in unscaled time (works while paused).
// With the Funke.Tween package installed, `rect.ScaleTo(...).SetEase(Ease.OutBack)` does the same.
public sealed class PopIn : EntityScript
{
    public float Duration = 0.2f;
    public float StartScale = 0.86f;

    public override void OnEnable()
    {
        if (GetComponent<RectTransform>() is not { } rect) return;
        rect.Scale = new Vector2(StartScale, StartScale);
        RunCoroutine(async () =>
        {
            for (float t = 0f; t < Duration; t += Time.UnscaledDeltaTime)
            {
                float s = StartScale + (1f - StartScale) * EaseOutBack(t / Duration);
                rect.Scale = new Vector2(s, s);
                await new WaitForNextFrame();
            }
            rect.Scale = new Vector2(1f, 1f);
        });
    }

    static float EaseOutBack(float x)
    {
        const float c1 = 1.70158f, c3 = c1 + 1f;
        float u = x - 1f;
        return 1f + c3 * u * u * u + c1 * u * u;
    }
}
