namespace Funke;

/// <summary>Pixels from each screen edge that the device covers (notch, cutout, rounded corners, home indicator).</summary>
public readonly record struct SafeAreaInsets(float Left, float Top, float Right, float Bottom)
{
    /// <summary>True when nothing covers the screen, as on desktop and Web.</summary>
    public bool IsZero => Left <= 0f && Top <= 0f && Right <= 0f && Bottom <= 0f;
}
