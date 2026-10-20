# Terrain Renderer 2D

**Terrain Renderer 2D** draws ground along a line of points: a **fill** texture below the surface and an **edge** texture along it. Use it for hills in driving games, roads, cliffs and platforms that are not made of tiles. It only draws; give the ground a **Polygon Collider 2D** (concave outlines are fine, see [Physics 2D](Physics-2D.md)) or build the collider from the same points.

Each segment between two points is drawn as one quad sheared vertically, so neighbouring segments share their edges exactly and the edge texture follows every slope without gaps.

## Settings

| Setting | Meaning |
|---|---|
| **Points** | The surface from left to right, in the entity's frame (moved, rotated and scaled by its Transform 2D). Set by a generator in the scene file (`points`) or from C# with `SetPoints`. Points must go left to right; a segment that doesn't is skipped. |
| **Fill Texture** | Texture below the surface. Give it **Wrap Repeat** in its import settings so it tiles. |
| **Fill Tile Size** | Width and height one repeat of the fill texture covers. |
| **Fill Mapping** | **World**: the texture is fixed in the entity's frame, its rows stay horizontal under the hills. **Draped**: the texture's top row follows the surface. |
| **Fill Depth** | How far the fill reaches below the surface. |
| **Edge Texture** | Strip along the surface (grass, curb, snow). Also Wrap Repeat. |
| **Edge Thickness**, **Edge Offset** | Height of the strip and of its top above the surface (negative sinks it into the fill). |
| **Edge Tile Length** | Surface length one repeat of the edge texture covers (measured along the slope). |
| **Edge Texture Offset** | Where along its texture the edge starts, in repeats. Set it to the end of the previous piece (`offset + length / tileLength`) so the pattern runs on without a seam across pieces built one after another. |
| **Fill/Edge Color** | Tint, as on a Sprite Renderer. |
| **Sorting Layer**, **Sorting Order**, **Edge Sorting Order** | Draw order against sprites; the edge has its own order so it can lie over the fill. |

Only the segments inside the camera view are drawn, so a long terrain costs little.

## C#

```csharp
var ground = GetComponent<TerrainRenderer2D>();
ground.SetPoints(surface);                  // Vector2[] in the entity's frame
ground.FillTexture = DirtTexture;           // public Texture? field wired in the scene
ground.FillTileSize = new Vector2(16f, 8f);
ground.EdgeOffset = 0.25f;
Vector2[] points = ground.GetPoints();
```

Scene key `TerrainRenderer2D`: `points` (flat `[x0, y0, x1, y1, …]`), `fillTexture` and `edgeTexture` (GUID strings), `fillColor` / `edgeColor` (`[r, g, b, a]`), `fillTileX`, `fillTileY`, `fillMapping` (0 World, 1 Draped), `fillDepth`, `edgeThickness`, `edgeOffset`, `edgeTileLength`, `edgeTextureOffset` (only when not 0), `sortLayer`, `sortOrder`, `edgeSortOrder`.
