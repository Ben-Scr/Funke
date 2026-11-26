using Funke;

public sealed class FactoryHud : EntityScript
{
    public TextRenderer? Mode, Queue, Notice, Context;
    private float refresh, noticeTime;
    private string lastNotice="";
    public override void OnUpdate()
    {
        refresh-=Time.UnscaledDeltaTime;
        if(refresh>0) return;
        refresh=.1f;
        var s=FactorySession.Instance;
        if(s?.Rules==null) return;
        if(Mode!=null) Mode.Text=s.Creative ? "CREATIVE" : "FREEPLAY";
        var jobs=s.Crafting?.Queue;
        if(Queue!=null) Queue.Text=jobs is {Count:>0} ? $"{s.FindRecipe(jobs[0].Recipe)?.Result?.DisplayName}  {jobs[0].Remaining:0.0}s  (+{jobs.Count-1})" : "";
        if(s.Notice!=lastNotice) { lastNotice=s.Notice; noticeTime=6; }
        noticeTime-=.1f;
        if(Notice!=null) Notice.Text=noticeTime>0 ? s.Notice : "";
        if(Context==null) return;
        var b=FactoryBuilder.Instance;
        if(b?.HoveringWorld==true && s.Nodes.TryGetValue((b.HoverX,b.HoverY),out var node))
            Context.Text=$"{node.Definition?.DisplayName} / {node.Status}";
        else if(b?.HoveringWorld==true && s.Deposits.TryGetValue((b.HoverX,b.HoverY),out var deposit))
            Context.Text=$"{deposit.Item?.DisplayName} / {deposit.Remaining} remaining / Hold F to gather";
        else Context.Text=b?.Selected?.Description??"";
    }
}
