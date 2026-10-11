# Your First Game

This walkthrough takes a new project from the Launcher to a finished Windows build in about 15 minutes. It assumes you have never used Index before.

## 1. Install the prerequisites

| You need | For | Download |
|---|---|---|
| Windows 10/11 x64 with a D3D12- or Vulkan-capable GPU | running the editor | — |
| **.NET 10 SDK (x64)** | compiling C# scripts | [dotnet.microsoft.com](https://dotnet.microsoft.com/download/dotnet/10.0) |
| MSVC Build Tools (v143) | only native C++ scripts and packages | [visualstudio.microsoft.com](https://visualstudio.microsoft.com/visual-cpp-build-tools/) |

The Launcher checks for these on start and shows a **Download** button next to anything that is missing.

## 2. Create a project

1. Start **Index-Launcher.exe** from the release folder.
2. Click **New Project**, enter a name, pick a folder, and confirm.
3. The Launcher creates the project, compiles the starter scripts, and lists it under **Projects**. Double-click it to open the editor.

The new project already contains a small playable example in `Assets/Scenes/SampleScene.scene`:

- **Camera**: what the player sees.
- **Player**: a blue square with a `Rigidbody2D`, a `BoxCollider2D` and the `PlayerController` script.
- **Coin**: a yellow square whose `CircleCollider2D` has **Sensor** enabled, plus the `Coin` script.

## 3. Press Play

Click **Play** in the toolbar. Move with **WASD**, the arrow keys or a gamepad's left stick, and touch the coin. The Console prints how many coins you have collected across all sessions; the count is stored with `PlayerPrefs`, so it survives restarts. Press **Stop** to return to editing; nothing that happened during play is kept.

## 4. Read the two scripts

Open `Assets/Scripts/PlayerController.cs` (double-click it in the Asset Browser to open your code editor):

```csharp
public class PlayerController : EntityScript
{
    public float Speed = 5.0f;

    private Rigidbody2D? m_Body;

    public override void OnStart() => m_Body = GetComponent<Rigidbody2D>();

    public override void OnFixedUpdate()
    {
        if (m_Body != null)
            m_Body.LinearVelocity = Input.GetAxis() * Speed;
    }
}
```

- `Speed` is a public field, so it appears in the **Inspector** when you select the Player. Change it there and press Play again.
- Movement is set in `OnFixedUpdate` because it drives physics.

`Assets/Scripts/Coin.cs` reacts in `OnTriggerEnter2D`, the callback for **sensor** colliders, and destroys its own entity.

Save a script and the editor recompiles and reloads it automatically.

## 5. Make it yours

Some small next steps, each using one new feature:

1. **Add more coins.** Select the Coin, press **Ctrl+D** to duplicate it, and drag the copies around the Viewport. **Ctrl+Z** undoes any step.
2. **Turn the coin into a prefab.** Drag the Coin from the Hierarchy into the Asset Browser. Changes to the prefab then apply to every instance.
3. **Add art.** Drop a `.png` into `Assets/`, then drag it onto the Player's **Sprite Renderer › Texture** slot. For a sprite sheet, slice it in the **Sprite Editor** and add a **Sprite Animator 2D** listing the slice names to animate it.
4. **Add walls.** Create an entity with a `BoxCollider2D` (no Rigidbody needed for static walls). The player bumps into it because the wall's collider is not a sensor.
5. **Keep score on screen.** Right-click the Hierarchy, choose **Create › UI › Text**, and update its text from the Coin script.

## 6. Build the game

1. Open **Project Settings** and set the executable name, company and version. The company and name also decide where saves live: `%LOCALAPPDATA%\<Company>\<Name>`.
2. Make sure **SampleScene** is in **Build Scenes** and set as the startup scene.
3. Open the **Build** panel, choose the **Release** profile, pick an empty output folder outside the project, and click **Build**.

The output folder is your game. To run it, players need the [.NET 10 Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) and the [Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe); a game started without .NET shows a message with a download link. See [Building and Shipping](Editor/Building-and-Shipping.md) for the full release checklist.

## Where to go next

- [Editor](Editor/Editor.md): panels, shortcuts, undo, crash recovery.
- [Scripting](Scripting/Scripting.md): lifecycle, input, physics, saving data.
- [Scenes](Engine-Core/Scenes.md) and [ECS](Engine-Core/ECS.md): how a game is organised.
- [Known Limitations](Known-Limitations.md): what the beta does not do yet.
