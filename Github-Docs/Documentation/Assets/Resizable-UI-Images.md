# Resizable UI images

Use a sliced Image for a panel or button whose corners should keep their shape as its rectangle grows. Use a tiled Image for a repeating pattern. Both modes work on a texture or a named sprite in an atlas.

| Image type | What resizing does |
| --- | --- |
| **Simple** | Stretches the entire sprite over the rectangle. |
| **Sliced** | Keeps the four corners, stretches each edge along one axis and stretches the center along both. If opposite borders exceed the rectangle, they shrink proportionally. |
| **Tiled** | Keeps the corners and repeats the edges and center. The final tile is cropped to fit. With zero borders the entire sprite repeats. |

## Author a panel

1. Put the panel texture in `Assets/`. For an atlas, select the sprite in the **Sprite Editor** and set its four border widths in source pixels. The order is **left, bottom, right, top**. Click **Apply** to save the slice metadata.
2. Assign the texture to a UI **Image**. Select the sprite by name for an atlas, or leave the name empty to use the whole texture.
3. Choose **Sliced** or **Tiled**. Leave the component’s four border values at zero to use the asset metadata. Set a component border to override the metadata for that Image.
4. Resize the **RectTransform**. **Border Scale** controls the UI units per source pixel for the border and, in Tiled mode, the tile size. Try a rectangle smaller than the combined borders to see how the corners meet.

To share a border across all uses of a standalone texture, run this from the project directory. The second command sets a border on one named atlas sprite. Keep the resulting `.meta` with the texture in version control.

```powershell
funke sprite border Assets/Textures/Panel.png --border 16
funke sprite border Assets/Textures/Ui.png --slice Ui_3 --border 8,8,12,8
```

## Set the mode from C#

Attach this script to an entity with an Image and an assigned texture. It uses the border from the texture or named sprite.

```csharp
using Funke;
using Funke.UI;

public class PanelFrame : EntityScript
{
    public override void OnStart()
    {
        var image = GetComponent<Image>();
        if (image == null)
            return;

        image.Type = ImageType.Sliced;
        image.Border = new Vector4(0, 0, 0, 0);
        image.BorderScale = 1;
    }
}
```

For command options and sprite slicing, see the [CLI sprite commands](../Editor/Command-Line.md#sprite-sheets). For panels attached to scene entities, see [world-space UI](../Engine-Core/World-Space-UI.md).
