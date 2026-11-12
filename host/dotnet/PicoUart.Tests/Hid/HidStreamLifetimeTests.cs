// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
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
    public async Task CompletedCleanupDoesNotRemoveAnotherPendingDeviceCleanup()
    {
        using var started = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        var lifetime = new HidStreamLifetime(() => { }, "cleanup-test-path");
        var completed = new HidStreamLifetime(() => { });
        Assert.Throws<TimeoutException>(() => lifetime.Transfer(() =>
        {
            started.Set();
            release.Wait();
            return true;
        }, timeoutMs: 20));
        Assert.True(started.Wait(TimeSpan.FromSeconds(1)));
        lifetime.Dispose();
        try
        {
            completed.TrackDeviceCleanup("cleanup-test-path");
            completed.Dispose();
            await completed.Cleanup;

            var error = Assert.Throws<IOException>(
                () => HidStreamLifetime.EnsureDeviceCleanupComplete("cleanup-test-path"));
            Assert.Contains("still pending", error.Message);

            release.Set();
            await lifetime.Cleanup.WaitAsync(TimeSpan.FromSeconds(1));
            HidStreamLifetime.EnsureDeviceCleanupComplete("cleanup-test-path");
        }
        finally
        {
            release.Set();
            await lifetime.Cleanup.WaitAsync(TimeSpan.FromSeconds(1));
        }
    }

    [Fact]
    public void DisposePreventsStartingAnotherTransfer()
    {
        var lifetime = new HidStreamLifetime(() => { });
        lifetime.Dispose();

        Assert.Throws<ObjectDisposedException>(() => lifetime.Transfer(() => true, timeoutMs: 10));
    }

    [Fact]
    public async Task FailedNativeCleanupKeepsDeviceReopeningBlocked()
    {
        var path = $"failed-cleanup-{Guid.NewGuid():N}";
        var lifetime = new HidStreamLifetime(() => throw new IOException("native cleanup failed"), path);
        lifetime.TrackDeviceCleanup(path);

        Assert.Throws<IOException>(() => lifetime.Dispose());
        await Assert.ThrowsAsync<IOException>(() => lifetime.Cleanup);

        var error = Assert.Throws<IOException>(() => HidStreamLifetime.EnsureDeviceCleanupComplete(path));
        Assert.Equal("native cleanup failed", error.Message);
    }
}
