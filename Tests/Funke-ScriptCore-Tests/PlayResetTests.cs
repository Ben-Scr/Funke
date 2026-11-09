using System;
using System.Linq;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

public class PlayResetTests
{
    public static class ValidReset
    {
        public static string Order = "";
        public static int Unregistered = 42;
        [ResetOnPlay] public static void B() => Order += "B";
        [ResetOnPlay] public static void A() => Order += "A";
    }
    public class InvalidReset
    {
        [ResetOnPlay] public void Invalid() { }
    }
    public static class FailingReset
    {
        [ResetOnPlay] public static void Failure() => throw new Exception("Reset failure");
    }

    [Fact]
    public void ResetsUseStableOrderAndLeaveUnregisteredStateAlone()
    {
        ValidReset.Order = "";
        var methods = ScriptInstanceManager.CollectPlayResetMethods(typeof(ValidReset).Assembly)
            .Where(m => m.DeclaringType == typeof(ValidReset)).ToArray();
        ScriptInstanceManager.RunPlayResetMethods(methods);
        Assert.Equal("AB", ValidReset.Order);
        Assert.Equal(42, ValidReset.Unregistered);
        Assert.Throws<InvalidOperationException>(() => ScriptInstanceManager.RunPlayResetMethods(
            new[] { methods[0], typeof(InvalidReset).GetMethod("Invalid")! }));
        Assert.Equal("AB", ValidReset.Order);
        var failure = Assert.Throws<InvalidOperationException>(() => ScriptInstanceManager.RunPlayResetMethods(
            new[] { typeof(FailingReset).GetMethod("Failure")! }));
        Assert.Contains("FailingReset.Failure", failure.Message);
    }
}
