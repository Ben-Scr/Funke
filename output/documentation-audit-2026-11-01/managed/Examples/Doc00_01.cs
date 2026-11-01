namespace Documentation0 {
#line 63 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Getting-Started.md"
using Funke;

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
}
