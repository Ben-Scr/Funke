namespace Documentation5 {
#line 66 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Engine-Core/Animation.md"
using Funke;

public class Footsteps : EntityScript
{
    public Audio? Step;

    // Event: Function "OnFootstep", Argument "left"
    public void OnFootstep(string foot) => Step?.PlayOneShot();
}
}
