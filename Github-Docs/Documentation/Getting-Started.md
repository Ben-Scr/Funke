# Your First Game

This walkthrough takes a new project from the Launcher to a finished desktop build in about 15 minutes. It assumes you have never used Index before. Screens and names are the same on Windows, Linux and macOS; executables just have no `.exe` outside Windows.

## 1. Install the prerequisites

| You need | For | Download |
|---|---|---|
| Windows 10/11 x64 with a D3D12- or Vulkan-capable GPU, a 64-bit Linux with a Vulkan driver, or macOS 11+ | running the editor | — |
| **.NET 10 SDK** for your CPU | compiling C# scripts | [dotnet.microsoft.com](https://dotnet.microsoft.com/download/dotnet/10.0) |
| A C++ toolchain: MSVC Build Tools (v143) on Windows, GCC/Clang + make on Linux, Xcode Command Line Tools on macOS | only native C++ scripts and packages | [visualstudio.microsoft.com](https://visualstudio.microsoft.com/visual-cpp-build-tools/) |

The Launcher checks for these on start and shows a **Download** button next to anything that is missing.

## 2. Create a project

1. Start **Index-Launcher.exe** from the release folder.
2. Click **New Project**, choose **Empty 2D**, enter a name, pick a folder, and confirm.
3. The Launcher creates the project, compiles its scripts once, and lists it under **Projects**. Double-click it to open the editor.

New projects start empty: `Assets/Scenes/SampleScene.scene` holds only a **Camera**, and `Assets/Scripts/GameScript.cs` is an empty script. The next steps build a small game in it: a player you move into a coin.

## 3. Build the scene

1. Right-click the **Hierarchy** and choose **Create › 2D Entity › Physics › Dynamic Body**. Rename it to **Player**. In the Inspector set the **Rigidbody 2D**'s **Gravity Scale** to `0` and enable **Freeze Rotation**, so it does not fall or spin.
2. Choose **Create › 2D Entity › Sprite › Circle** and rename it to **Coin**. Move it to the right of the player, then **Add Component › Circle Collider 2D** and enable **Sensor**: the player passes through it, and the coin is told about the touch.

## 4. Write two scripts

In the **Asset Browser**, open `Assets/Scripts`, right-click and choose **Create › EntityScript**. Name it `PlayerController` and double-click it to open your code editor:

```csharp
using Index;

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

- `Speed` is a public field, so it appears in the **Inspector**. Change it there and press Play again.
- Movement is set in `OnFixedUpdate` because it drives physics.

Create a second EntityScript named `Coin`:

```csharp
using Index;

public class Coin : EntityScript
{
    public override void OnTriggerEnter2D(Collision2D other)
    {
        if (other.Other.Name != "Player")
            return;

        // PlayerPrefs survives restarts.
        int collected = PlayerPrefs.GetInt("CoinsCollected") + 1;
        PlayerPrefs.SetInt("CoinsCollected", collected);
        Log.Info($"Coin collected! Total across all sessions: {collected}");

        Entity.Destroy();
    }
}
```

`OnTriggerEnter2D` is the callback for **sensor** colliders. Save a script and the editor recompiles and reloads it automatically.

Attach the scripts: select the Player, **Add Component**, pick `PlayerController`; do the same for the Coin with `Coin`.

### Press Play

Click **Play** in the toolbar. Move with **WASD**, the arrow keys or a gamepad's left stick, and touch the coin. The Console prints how many coins you have collected across all sessions. Press **Stop** to return to editing; nothing that happened during play is kept.

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
