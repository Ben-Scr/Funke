using System;
using System.Collections.Generic;
using Funke;
using Xunit;

namespace FunkeScriptCoreTests;

public class AndroidBridgeTests : IDisposable
{
    public void Dispose() => AndroidBridge.Clear();

    [Fact]
    public void SimulatedMessagesReachEveryHandlerOfTheirChannel()
    {
        var received = new List<string>();
        Action<string> first = m => received.Add("a:" + m);
        AndroidBridge.Subscribe("ads", first);
        AndroidBridge.Subscribe("ads", m => received.Add("b:" + m));
        AndroidBridge.Subscribe("billing", m => received.Add("billing:" + m));

        AndroidBridge.Simulate("ads", "loaded");
        Assert.Equal(new[] { "a:loaded", "b:loaded" }, received);

        Assert.True(AndroidBridge.Unsubscribe("ads", first));
        Assert.False(AndroidBridge.Unsubscribe("ads", first));
        AndroidBridge.Simulate("ads", "closed");
        AndroidBridge.Simulate("nobody", "ignored");
        Assert.Equal(new[] { "a:loaded", "b:loaded", "b:closed" }, received);
    }

    [Fact]
    public void AThrowingHandlerDoesNotStopTheOthers()
    {
        int calls = 0;
        AndroidBridge.Subscribe("ads", _ => throw new InvalidOperationException("boom"));
        AndroidBridge.Subscribe("ads", _ => calls++);
        AndroidBridge.Simulate("ads", "x");
        Assert.Equal(1, calls);
    }

    [Fact]
    public void ClearDropsHandlers()
    {
        int calls = 0;
        AndroidBridge.Subscribe("ads", _ => calls++);
        AndroidBridge.Clear();
        AndroidBridge.Simulate("ads", "x");
        Assert.Equal(0, calls);
    }

    [Fact]
    public void RejectsEmptyNames()
    {
        Assert.Throws<ArgumentException>(() => AndroidBridge.Subscribe("", _ => { }));
        Assert.Throws<ArgumentException>(() => AndroidBridge.TryCall("", "m", "", out _));
        Assert.Throws<ArgumentException>(() => AndroidBridge.TryCall("a.B", "", "", out _));
    }
}
