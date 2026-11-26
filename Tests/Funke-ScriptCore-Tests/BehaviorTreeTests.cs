using Funke.AI;
using Xunit;

namespace FunkeScriptCoreTests;

public class BehaviorTreeTests
{
    private sealed class Actor
    {
        public bool Valid = true;
        public bool InSight;
        public int Position, Aborts;
    }

    [Fact]
    public void TwoActorsShareDefinitionAndBudgetDoesNotRestartTheTraversal()
    {
        var route = BehaviorNode<Actor>.Selector("route",
            BehaviorNode<Actor>.Sequence("chase",
                BehaviorNode<Actor>.Condition("visible", actor => actor.InSight),
                BehaviorNode<Actor>.Action("approach", actor => { actor.Position++; return BehaviorResult.Success; })),
            BehaviorNode<Actor>.Action("patrol", actor => { actor.Position--; return BehaviorResult.Success; }));
        var chaser = new BehaviorTree<Actor>(route, new Actor { InSight = true });
        var patrol = new BehaviorTree<Actor>(route, new Actor());
        Assert.Equal(BehaviorResult.Running, chaser.Tick(1));
        Assert.Equal(2, chaser.ActiveDepth);
        Assert.Equal("chase", chaser.ActiveNodeName(1));
        Assert.Equal(0, chaser.Blackboard.Position);
        for (int i = 0; i < 15 && chaser.Result == BehaviorResult.Running; i++) chaser.Tick(1);
        Assert.Equal(BehaviorResult.Success, chaser.Result);
        Assert.Equal(1, chaser.Blackboard.Position);
        Assert.Equal(BehaviorResult.Success, patrol.Tick(20));
        Assert.Equal(-1, patrol.Blackboard.Position);
        Assert.Equal(("route", BehaviorResult.Success), patrol.LastTransition!.Value);
        Assert.Equal(BehaviorResult.Success, patrol.Tick(1));
        Assert.Equal(-1, patrol.Blackboard.Position);
    }

    [Fact]
    public void InvalidationAndExplicitResetAbortOnlyTheActiveActionOnce()
    {
        var actor = new Actor();
        var node = BehaviorNode<Actor>.Action("walk", a => { a.Position++; return BehaviorResult.Running; }, a => a.Aborts++);
        var tree = new BehaviorTree<Actor>(BehaviorNode<Actor>.Invert("invert", node), actor, a => a.Valid);
        Assert.Equal(BehaviorResult.Running, tree.Tick(1000));
        Assert.Equal(1, actor.Position);
        actor.Valid = false;
        Assert.Equal(BehaviorResult.Failure, tree.Tick(1));
        Assert.Equal(0, tree.ActiveDepth);
        tree.Reset();
        Assert.Equal(1, actor.Aborts);
        actor.Valid = true;
        Assert.Equal(BehaviorResult.Running, tree.Tick(3));
        tree.Reset();
        tree.Reset();
        Assert.Equal(2, actor.Aborts);
        var inverse = BehaviorNode<Actor>.Invert("absent", BehaviorNode<Actor>.Condition("visible", a => a.InSight));
        Assert.Equal(BehaviorResult.Success, new BehaviorTree<Actor>(inverse, actor).Tick(3));
    }
}
