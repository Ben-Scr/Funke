# Launcher and Editor Icons

Launcher and Editor use transparent, theme-tinted glyphs. General icons live at `Funke-Runtime/FunkeAssets/Textures/Editor/General/<name>/<name>_<size>.png`, in **16, 32, 64, and 128 pixel** sizes. Both icon loaders select from these sizes. Keep every supported size available in staged builds.

| Glyph | Use |
|---|---|
| `reset` | Open circular arrow, intentionally shared by Refresh and Reset. Launcher project and Asset Library refresh actions use it. |
| `copy` | Overlapping documents; only the exposed rear contour is drawn, leaving the interior transparent. Used for Copy Path, Duplicate, and Copy Message. |
| `undo`, `redo` | Mirrored bent arrows. Toasts request a texture for their displayed size and DPI and tint it with the current theme's text color and fade alpha. |
| `templates` | Partitioned template page, distinct from Copy; used for the Launcher Templates sidebar entry. |
| `post_processing` | Four-point spark for the Editor View's effect preview toggle. |

Monochrome glyph artwork is generated with the built-in ImageGen tool. Originals, exact prompts and the asset list live in `scripts/assets/sources/editor-imagegen/`. Export the selected originals with `python scripts/assets/export_editor_imagegen_icons.py --write`. This only standardizes padding, normalizes RGB to white for runtime tinting and resamples the original alpha silhouette; it does not draw icons. The legacy generator preserves every glyph listed in the ImageGen manifest. Existing colored file, component, status and OS/platform icons stay unchanged.

Window titles and menus use `EditorWindowIcons::AssetName` and the texture provider installed by `ImGuiContextLayer`; icon labels, window IDs and saved docking remain stable. Menu icons stay before their labels. Theme choices use `theme_light`, `theme_dark`, `monitor` and `pen`, all tinted with the current text color.

Review all four sizes on light and dark backgrounds, then inspect the actual controls at normal and increased UI scale. Check visible contours, hover/active/disabled tint where applicable, tooltips, and unchanged click targets. The Templates sidebar keeps a separate active highlight; the post-processing tooltip names its On/Off state. A gallery alone does not verify actions or in-app contrast.
