using System;
using Funke;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public class ApplicationEventCompatibilityTests
{
#pragma warning disable CS0618 // Compile the API used by already published packages.
    [Fact]
    public void PausedSharesSubscriptionsWithOnPaused() => VerifySharedSubscriptions(
        handler => Application.Paused += handler, handler => Application.Paused -= handler,
        handler => Application.OnPaused += handler, handler => Application.OnPaused -= handler,
        Application.RaiseApplicationPaused);

    [Fact]
    public void StartedSharesSubscriptionsWithOnStart() => VerifySharedSubscriptions(
        handler => Application.Started += handler, handler => Application.Started -= handler,
        handler => Application.OnStart += handler, handler => Application.OnStart -= handler,
        Application.RaiseApplicationStart);

    [Fact]
    public void QuittingSharesSubscriptionsWithOnQuit() => VerifySharedSubscriptions(
        handler => Application.Quitting += handler, handler => Application.Quitting -= handler,
        handler => Application.OnQuit += handler, handler => Application.OnQuit -= handler,
        Application.RaiseApplicationQuit);

    [Fact]
    public void PlayModeExitedSharesSubscriptionsWithOnPlayModeExited() => VerifySharedSubscriptions(
        handler => Application.PlayModeExited += handler, handler => Application.PlayModeExited -= handler,
        handler => Application.OnPlayModeExited += handler, handler => Application.OnPlayModeExited -= handler,
        Application.RaisePlayModeExited);
#pragma warning restore CS0618

    private static void VerifySharedSubscriptions(Action<Action> subscribe, Action<Action> unsubscribe,
        Action<Action> subscribeLegacy, Action<Action> unsubscribeLegacy, Action raise)
    {
        int currentCalls = 0;
        int legacyCalls = 0;
        Action currentHandler = () => currentCalls++;
        Action legacyHandler = () => legacyCalls++;
        try
        {
            subscribe(currentHandler);
            subscribeLegacy(legacyHandler);
            raise();
            Assert.Equal(1, currentCalls);
            Assert.Equal(1, legacyCalls);

            unsubscribeLegacy(currentHandler);
            unsubscribe(legacyHandler);
            raise();
            Assert.Equal(1, currentCalls);
            Assert.Equal(1, legacyCalls);

            subscribe(legacyHandler);
            subscribeLegacy(legacyHandler);
            raise();
            Assert.Equal(3, legacyCalls);
            unsubscribeLegacy(legacyHandler);
            raise();
            Assert.Equal(4, legacyCalls);
            unsubscribe(legacyHandler);
            raise();
            Assert.Equal(4, legacyCalls);
        }
        finally
        {
            unsubscribe(currentHandler);
            unsubscribeLegacy(currentHandler);
            unsubscribe(legacyHandler);
            unsubscribeLegacy(legacyHandler);
        }
    }
}
