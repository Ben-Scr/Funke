namespace Documentation10 {
#line 133 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Scripting/Scripting.md"
using Funke;

public class Enemy : EntityScript
{
    [Header("Stats")]
    [ShowInEditor("Move Speed")] public float Speed = 3.0f;
    [ClampValue(0f, 100f)]       public float Health = 100f;

    [Space(8f)]
    [ToolTip("Seconds between attacks")]
    public float AttackCooldown = 1.5f;

    [EnabledIf("UseCustomColor", true)] public Color Tint = Color.White;
    public bool UseCustomColor = false;

    [HideFromEditor] public int internalCounter;  // public but hidden
}
}
