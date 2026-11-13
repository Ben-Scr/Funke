using System;
using Funke;
using Funke.UI;

public sealed class AgentLoop : EntityScript
{
    public Entity Coin;
    public Entity Hud;
    public int Score;
    public int Updates;
    public int Revision;
    public bool PackageLoaded;
    public float Travel;
    public float Zoom = 1;
    public int PadDown;
    public int PadUp;
    public int PadHeldFrames;
    public float PadPeak;
    public float TriggerPeak;
    public bool ThrowOnUpdate;
    public string Mode = "Playing";
    public int[] Unsupported = new[] { 1, 2 };

    public override void OnStart()
    {
        Revision = 1;
        PackageLoaded = AgentFixture.Constants.PackageRevision == "1.0.0";
        PlayerPrefs.SetString("AgentFixtureRun", Application.PrefsProfile);
        PlayerPrefs.Save();
    }

    public override void OnUpdate()
    {
        ++Updates;
        if (ThrowOnUpdate) throw new InvalidOperationException("intentional agent fixture failure");
        float dx = Input.GetKey(KeyCode.D) ? 2 * Revision * Time.DeltaTime : 0;
        if (Transform != null)
        {
            Transform.Position += new Vector2(dx, 0);
            Travel += dx;
            if (Coin && Coin.Transform2D != null && Math.Abs(Transform.Position.X - Coin.Transform2D.Position.X) < 0.3f)
            {
                Coin.Destroy();
                Score += Revision;
                Mode = "Collected";
            }
        }
        var label = Hud ? Hud.GetComponent<TextRenderer>() : null;
        if (label != null) label.Text = "Score: " + Score;
        Zoom += Input.GetScrollWheelDelta() * 0.1f;
        var camera = Camera2D.Main;
        if (camera != null) camera.Zoom = Zoom;
        for (int i = 0; i < 4; ++i)
        {
            if (!Input.IsGamepadConnected(i)) continue;
            if (Input.GetGamepadButtonDown(GamepadButton.A, i)) ++PadDown;
            if (Input.GetGamepadButtonUp(GamepadButton.A, i)) ++PadUp;
            if (Input.GetGamepadButton(GamepadButton.A, i)) ++PadHeldFrames;
            PadPeak = Math.Max(PadPeak, Input.GetGamepadAxis(GamepadAxis.LeftX, i));
            TriggerPeak = Math.Max(TriggerPeak, Input.GetGamepadTrigger(GamepadAxis.LeftTrigger, i));
        }
    }
}
