# Imagegen icon concepts

Three alternatives per asset. Each folder includes the generated master and transparent PNG exports at 16, 32, 64, 128, and 256 pixels. Masters are scaled to fit the square without stretching.

- Folder: 01 open pocket; 02 closed folder with label panel; 03 open folder with visible sheet.
- C# file: 01 code brackets; 02 braces; 03 C#.
- Scene file: 01 clapperboard; 02 landscape; 03 stage.

These are candidate assets, not installed in the editor. The actual regular-folder asset is under Editor/General/open_folder; C# and scene assets are under Editor/FileIcons. Their existing files have been preserved.

Image generation: built-in imagegen, using isolated flat 2D editor icon prompts on transparent backgrounds; amber folders, purple documents for C#, blue documents for scenes. C# variants were regenerated in purple to avoid transparency artifacts.

