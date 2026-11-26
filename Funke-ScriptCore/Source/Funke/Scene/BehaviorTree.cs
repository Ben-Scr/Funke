using System;
using System.Collections.Generic;

namespace Funke.AI;

public enum BehaviorResult { Running, Success, Failure }

/// <summary>Immutable definition. Actions receive the owning tree's strongly typed blackboard.</summary>
public sealed class BehaviorNode<T> where T : class
{
    internal enum Kind { Action, Sequence, Selector, Invert }
    internal Kind Operation { get; }
    internal BehaviorNode<T>[] Children { get; }
    internal Func<T, BehaviorResult>? Evaluate { get; }
    internal Action<T>? OnAbort { get; }
    public string Name { get; }

    private BehaviorNode(string name, Kind kind, BehaviorNode<T>[] children,
        Func<T, BehaviorResult>? evaluate = null, Action<T>? onAbort = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        ArgumentNullException.ThrowIfNull(children);
        if (Array.Exists(children, node => node is null)) throw new ArgumentException("A child is null.", nameof(children));
        Name = name;
        Operation = kind;
        Children = (BehaviorNode<T>[])children.Clone();
        Evaluate = evaluate;
        OnAbort = onAbort;
    }

    public static BehaviorNode<T> Action(string name, Func<T, BehaviorResult> tick, Action<T>? onAbort = null)
        => new(name, Kind.Action, [], tick ?? throw new ArgumentNullException(nameof(tick)), onAbort);
    public static BehaviorNode<T> Condition(string name, Func<T, bool> test)
    {
        ArgumentNullException.ThrowIfNull(test);
        return Action(name, state => test(state) ? BehaviorResult.Success : BehaviorResult.Failure);
    }
    public static BehaviorNode<T> Sequence(string name, params BehaviorNode<T>[] children) => new(name, Kind.Sequence, children);
    public static BehaviorNode<T> Selector(string name, params BehaviorNode<T>[] children) => new(name, Kind.Selector, children);
    public static BehaviorNode<T> Invert(string name, BehaviorNode<T> child) => new(name, Kind.Invert, [child]);
}

/// <summary>Per-agent, engine-thread interpreter. One budget unit evaluates a node or propagates one result.</summary>
public sealed class BehaviorTree<T> where T : class
{
    private readonly BehaviorNode<T> root;
    private readonly Func<T, bool>? canRun;
    private readonly List<(BehaviorNode<T> Node, int Next)> path = new();
    private BehaviorResult? pending;
    private BehaviorNode<T>? runningAction;
    private bool executing;
    public T Blackboard { get; }
    public BehaviorResult? Result { get; private set; }
    public (string Node, BehaviorResult Result)? LastTransition { get; private set; }
    public int ActiveDepth => path.Count;
    public string ActiveNodeName(int depth) => path[depth].Node.Name;

    public BehaviorTree(BehaviorNode<T> root, T blackboard, Func<T, bool>? canRun = null)
    {
        this.root = root ?? throw new ArgumentNullException(nameof(root));
        Blackboard = blackboard ?? throw new ArgumentNullException(nameof(blackboard));
        this.canRun = canRun;
    }

    /// <summary>Abort once, discard pending traversal and allow a fresh start on the next tick.</summary>
    public void Reset()
    {
        if (executing) throw new InvalidOperationException("Cannot reset a tree from its own callback.");
        executing = true;
        try { Clear(); }
        finally { executing = false; }
    }

    private void Clear()
    {
        var previous = runningAction;
        runningAction = null;
        path.Clear();
        pending = null;
        Result = null;
        LastTransition = null;
        previous?.OnAbort?.Invoke(Blackboard);
    }

    public BehaviorResult Tick(int budget)
    {
        if (budget <= 0) throw new ArgumentOutOfRangeException(nameof(budget));
        if (executing) throw new InvalidOperationException("Cannot tick a tree recursively.");
        executing = true;
        try
        {
            if (canRun != null && !canRun(Blackboard))
            {
                Clear();
                Result = BehaviorResult.Failure;
            }
            if (Result is BehaviorResult.Success or BehaviorResult.Failure) return Result.Value;
            if (path.Count == 0) path.Add((root, 0));
            Result = BehaviorResult.Running;
            while (budget-- > 0 && path.Count > 0)
            {
                var (node, next) = path[^1];
                if (node.Operation == BehaviorNode<T>.Kind.Action)
                {
                    BehaviorResult value = node.Evaluate!(Blackboard);
                    if (!Enum.IsDefined(value)) throw new InvalidOperationException("Invalid action result.");
                    if (value == BehaviorResult.Running)
                    {
                        runningAction = node;
                        LastTransition = (node.Name, value);
                        break;
                    }
                    runningAction = null;
                    Finish(node, value);
                    continue;
                }
                if (pending is { } child)
                {
                    pending = null;
                    if (node.Operation == BehaviorNode<T>.Kind.Invert)
                    {
                        Finish(node, child == BehaviorResult.Success ? BehaviorResult.Failure : BehaviorResult.Success);
                        continue;
                    }
                    if (node.Operation == BehaviorNode<T>.Kind.Sequence && child == BehaviorResult.Failure ||
                        node.Operation == BehaviorNode<T>.Kind.Selector && child == BehaviorResult.Success)
                    {
                        Finish(node, child);
                        continue;
                    }
                }
                if (next == node.Children.Length)
                    Finish(node, node.Operation == BehaviorNode<T>.Kind.Selector ? BehaviorResult.Failure : BehaviorResult.Success);
                else
                {
                    path[^1] = (node, next + 1);
                    path.Add((node.Children[next], 0));
                }
            }
            return Result.Value;
        }
        catch
        {
            Clear();
            throw;
        }
        finally { executing = false; }
    }

    private void Finish(BehaviorNode<T> node, BehaviorResult value)
    {
        path.RemoveAt(path.Count - 1);
        LastTransition = (node.Name, value);
        if (path.Count == 0) Result = value;
        else pending = value;
    }
}
