using System.Runtime.InteropServices;
using Index.Components;

namespace Index.Native;

// PREFIX VIEW: mirrors the POD bytes of C++ SpriteRendererComponent before SpriteName (verified by ComponentTypes<T> size guard). Write TextureAssetId to swap sprites — TextureHandle is set by the engine after UUID changes and MUST NOT be written by scripts.
[StructLayout(LayoutKind.Sequential)]
public struct NativeSpriteRenderer : IComponent
{
    // WARNING: `default(NativeSpriteRenderer)` zero-inits Color to (0,0,0,0). Use ecb.CreateEntityWith<..., NativeSpriteRenderer>() so C++ member-initializers fire and the sprite renders white.
    public NativeSpriteRenderer() { }

    public short SortingOrder;
    public byte  SortingLayer;
    private byte _pad;

    public TextureHandle TextureHandle;
    public ulong         TextureAssetId;
    // Mirror of SpriteRendererComponent.hpp `Color{1.0f, 1.0f, 1.0f, 1.0f}` —
    // matches the C++ default so freshly-created sprites are white-tinted
    // rather than transparent black.
    public Color         Color = new(1f, 1f, 1f, 1f);
    // Mirror of SpriteRendererComponent.hpp `FilterMode{ Filter::Default }`.
    // Default = use the texture's own (import) filter rather than overriding it.
    // Writing this field directly only mutates the per-entity setting; to
    // actually rebuild the texture sampler use the managed SpriteRenderer
    // wrapper's FilterMode property (which routes through the inspector setter).
    public TextureFilter FilterMode = TextureFilter.Default;
    // .material GUID with a Sprite2D shader; 0 = standard sprite shader. Written directly, an edited material file is not re-read.
    public ulong Material;
    // > 0: native size (slice pixels / PixelsPerUnit, times the transform scale), placed on the slice pivot.
    public float PixelsPerUnit;
    public bool FlipX;
    public bool FlipY;

    internal const string NativeName = "Sprite Renderer";
}
