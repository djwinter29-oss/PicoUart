using PicoUart.Hid;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class HidStreamLifetimeTests
{
    [Fact]
    public void DisposeWaitsForTimedOutTransferToFinishBeforeClosingStream()
    {
        using var started = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        using var disposed = new ManualResetEventSlim();
        var lifetime = new HidStreamLifetime(() => disposed.Set());

        Assert.Throws<TimeoutException>(() => lifetime.Transfer(() =>
        {
            started.Set();
            release.Wait();
            return true;
        }, timeoutMs: 20));

        Assert.True(started.Wait(TimeSpan.FromSeconds(1)));
        lifetime.Dispose();
        Assert.False(disposed.IsSet);

        release.Set();
        Assert.True(disposed.Wait(TimeSpan.FromSeconds(1)));
        lifetime.Dispose();
    }

    [Fact]
    public void DisposePreventsStartingAnotherTransfer()
    {
        var lifetime = new HidStreamLifetime(() => { });
        lifetime.Dispose();

        Assert.Throws<ObjectDisposedException>(() => lifetime.Transfer(() => true, timeoutMs: 10));
    }
}
