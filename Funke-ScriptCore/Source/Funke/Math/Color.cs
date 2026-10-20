namespace Funke;

public struct Color : System.IEquatable<Color>
{
    public float R = 1.0f, G = 1.0f, B = 1.0f, A = 1.0f;

    public Color(float r, float g, float b)
    {
        R = r; G = g; B = b; A = 1.0f;
    }
    public Color(float r, float g, float b, float a)
    {
        R = r; G = g; B = b; A = a;
    }

    public float X { get => R; set => R = value; }
    public float Y { get => G; set => G = value; }
    public float Z { get => B; set => B = value; }
    public float W { get => A; set => A = value; }

    public Color SetR(float r) => new Color(r, G, B, A);
    public Color SetG(float g) => new Color(R, g, B, A);
    public Color SetB(float b) => new Color(R, G, b, A);
    public Color SetA(float a) => new Color(R, G, B, a);

    public static readonly Color White = new(1.0f, 1.0f, 1.0f);
    public static readonly Color Black = new(0.0f, 0.0f, 0.0f);
    public static readonly Color Red = new(1.0f, 0.0f, 0.0f);
    public static readonly Color Green = new(0.0f, 1.0f, 0.0f);
    public static readonly Color Blue = new(0.0f, 0.0f, 1.0f);
    public static readonly Color Yellow = new(1.0f, 1.0f, 0.0f);
    public static readonly Color Cyan = new(0.0f, 1.0f, 1.0f);
    public static readonly Color Magenta = new(1.0f, 0.0f, 1.0f);
    public static readonly Color Gray = new(0.5f, 0.5f, 0.5f);
    public static readonly Color Clear = new(0.0f, 0.0f, 0.0f, 0.0f);

    // ── Light presets ───────────────────────────────────────────
    public static readonly Color LightGray = new(0.75f, 0.75f, 0.75f);
    public static readonly Color Silver = new(0.83f, 0.83f, 0.83f);
    public static readonly Color Orange = new(1.0f, 0.65f, 0.0f);
    public static readonly Color Gold = new(1.0f, 0.84f, 0.0f);
    public static readonly Color Pink = new(1.0f, 0.75f, 0.8f);
    public static readonly Color SkyBlue = new(0.53f, 0.81f, 0.92f);
    public static readonly Color LightGreen = new(0.56f, 0.93f, 0.56f);
    public static readonly Color Lavender = new(0.9f, 0.9f, 0.98f);
    public static readonly Color Peach = new(1.0f, 0.8f, 0.6f);
    public static readonly Color Cream = new(1.0f, 0.99f, 0.82f);

    // ── Dark presets ────────────────────────────────────────────
    public static readonly Color DarkGray = new(0.25f, 0.25f, 0.25f);
    public static readonly Color DarkRed = new(0.5f, 0.0f, 0.0f);
    public static readonly Color DarkGreen = new(0.0f, 0.39f, 0.0f);
    public static readonly Color DarkBlue = new(0.0f, 0.0f, 0.5f);
    public static readonly Color Navy = new(0.0f, 0.0f, 0.28f);
    public static readonly Color Brown = new(0.4f, 0.26f, 0.13f);
    public static readonly Color Maroon = new(0.5f, 0.0f, 0.25f);
    public static readonly Color Purple = new(0.5f, 0.0f, 0.5f);
    public static readonly Color Indigo = new(0.29f, 0.0f, 0.51f);
    public static readonly Color Teal = new(0.0f, 0.5f, 0.5f);
    public static readonly Color Olive = new(0.42f, 0.42f, 0.12f);

    // ── Operators ───────────────────────────────────────────────
    public static Color operator +(Color a, Color b) => new(a.R + b.R, a.G + b.G, a.B + b.B, a.A + b.A);
    public static Color operator -(Color a, Color b) => new(a.R - b.R, a.G - b.G, a.B - b.B, a.A - b.A);
    public static Color operator *(Color c, float s) => new(c.R * s, c.G * s, c.B * s, c.A * s);
    public static Color operator *(float s, Color c) => new(s * c.R, s * c.G, s * c.B, s * c.A);

    public static bool operator ==(Color a, Color b) => a.Equals(b);
    public static bool operator !=(Color a, Color b) => !a.Equals(b);

    // ── Conversions ─────────────────────────────────────────────
    // Color ↔ Vector4 conversion is defined in Vector4.cs to avoid ambiguity.
    // Use: Vector4 v = (Vector4)color;  or  Color c = (Color)vec4;
    public static explicit operator Vector3(Color c) => new(c.R, c.G, c.B);
    public static explicit operator Vector2(Color c) => new(c.R, c.G);

    // ── Equality ────────────────────────────────────────────────
    public bool Equals(Color other) => R == other.R && G == other.G && B == other.B && A == other.A;
    public override bool Equals(object? obj) => obj is Color other && Equals(other);
    public override int GetHashCode() => System.HashCode.Combine(R, G, B, A);
    public override string ToString() => $"Color({R}, {G}, {B}, {A})";

    /// <summary>Linear interpolation between two colors.</summary>
    public static Color Lerp(Color a, Color b, float t)
    {
        t = Mathf.Clamp01(t);
        return new Color(a.R + (b.R - a.R) * t, a.G + (b.G - a.G) * t,
                         a.B + (b.B - a.B) * t, a.A + (b.A - a.A) * t);
    }

    // ── Hex ─────────────────────────────────────────────────────

    /// <summary>
    /// Color from <c>"#RRGGBB"</c>, <c>"#RRGGBBAA"</c>, <c>"#RGB"</c> or <c>"#RGBA"</c> (the # is optional), as written
    /// in palettes and design sheets. Throws <see cref="System.FormatException"/> for anything else.
    /// </summary>
    public static Color FromHex(string hex)
        => TryFromHex(hex, out Color color) ? color : throw new System.FormatException($"'{hex}' is not a hex color (#RRGGBB, #RRGGBBAA, #RGB or #RGBA).");

    /// <summary><see cref="FromHex"/> that returns false instead of throwing.</summary>
    public static bool TryFromHex(string? hex, out Color color)
    {
        color = White;
        if (hex == null) return false;
        System.ReadOnlySpan<char> s = hex.AsSpan().Trim();
        if (s.Length > 0 && s[0] == '#') s = s[1..];

        bool shortForm = s.Length is 3 or 4;
        if (!shortForm && s.Length is not (6 or 8)) return false;
        int channels = shortForm ? s.Length : s.Length / 2;
        System.Span<float> c = stackalloc float[4] { 1f, 1f, 1f, 1f };
        for (int i = 0; i < channels; i++)
        {
            int value;
            if (shortForm)
            {
                int digit = HexDigit(s[i]);
                if (digit < 0) return false;
                value = digit * 17;
            }
            else
            {
                int hi = HexDigit(s[i * 2]), lo = HexDigit(s[i * 2 + 1]);
                if (hi < 0 || lo < 0) return false;
                value = hi * 16 + lo;
            }
            c[i] = value / 255f;
        }
        color = new Color(c[0], c[1], c[2], c[3]);
        return true;
    }

    /// <summary><c>"#RRGGBB"</c>, or <c>"#RRGGBBAA"</c> with <paramref name="includeAlpha"/>; channels are clamped to 0..1.</summary>
    public string ToHex(bool includeAlpha = false)
    {
        static int Byte(float v) => (int)System.MathF.Round(Mathf.Clamp01(v) * 255f);
        return includeAlpha
            ? $"#{Byte(R):X2}{Byte(G):X2}{Byte(B):X2}{Byte(A):X2}"
            : $"#{Byte(R):X2}{Byte(G):X2}{Byte(B):X2}";
    }

    private static int HexDigit(char c) => c switch
    {
        >= '0' and <= '9' => c - '0',
        >= 'a' and <= 'f' => c - 'a' + 10,
        >= 'A' and <= 'F' => c - 'A' + 10,
        _ => -1,
    };
}
