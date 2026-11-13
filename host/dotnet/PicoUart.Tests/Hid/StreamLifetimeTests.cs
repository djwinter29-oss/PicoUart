// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using PicoUart.Hid;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class StreamLifetimeTests
{
    [Fact]
    public async Task DisposeWaitsForTimedOutTransferToFinishBeforeClosingStream()
    {
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var release = new ManualResetEventSlim();
        using var disposed = new ManualResetEventSlim();
        var lifetime = new StreamLifetime(() => disposed.Set());

        try
        {
            Assert.Throws<TimeoutException>(() => lifetime.Transfer(() =>
            {
                started.SetResult();
                release.Wait();
                return true;
            }, timeoutMs: 20));

            await started.Task.WaitAsync(TimeSpan.FromSeconds(10));
            lifetime.Dispose();
            Assert.False(disposed.IsSet);
        }
        finally
        {
            release.Set();
            lifetime.Dispose();
            await lifetime.Cleanup.WaitAsync(TimeSpan.FromSeconds(10));
        }
        Assert.True(disposed.IsSet);
    }

    [Fact]
    public async Task CompletedCleanupDoesNotRemoveAnotherPendingDeviceCleanup()
    {
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var release = new ManualResetEventSlim();
        var lifetime = new StreamLifetime(() => { }, "cleanup-test-path");
        var completed = new StreamLifetime(() => { });
        try
        {
            Assert.Throws<TimeoutException>(() => lifetime.Transfer(() =>
            {
                started.SetResult();
                release.Wait();
                return true;
            }, timeoutMs: 20));
            await started.Task.WaitAsync(TimeSpan.FromSeconds(10));
            lifetime.Dispose();

            completed.TrackDeviceCleanup("cleanup-test-path");
            completed.Dispose();
            await completed.Cleanup;

            var error = Assert.Throws<IOException>(
                () => StreamLifetime.EnsureDeviceCleanupComplete("cleanup-test-path"));
            Assert.Contains("still pending", error.Message);

            release.Set();
            await lifetime.Cleanup.WaitAsync(TimeSpan.FromSeconds(10));
            StreamLifetime.EnsureDeviceCleanupComplete("cleanup-test-path");
        }
        finally
        {
            release.Set();
            lifetime.Dispose();
            await lifetime.Cleanup.WaitAsync(TimeSpan.FromSeconds(10));
        }
    }

    [Fact]
    public void DisposePreventsStartingAnotherTransfer()
    {
        var lifetime = new StreamLifetime(() => { });
        lifetime.Dispose();

        Assert.Throws<ObjectDisposedException>(() => lifetime.Transfer(() => true, timeoutMs: 10));
    }

    [Fact]
    public async Task FailedNativeCleanupKeepsDeviceReopeningBlocked()
    {
        var path = $"failed-cleanup-{Guid.NewGuid():N}";
        var lifetime = new StreamLifetime(() => throw new IOException("native cleanup failed"), path);
        lifetime.TrackDeviceCleanup(path);

        Assert.Throws<IOException>(() => lifetime.Dispose());
        await Assert.ThrowsAsync<IOException>(() => lifetime.Cleanup);

        var error = Assert.Throws<IOException>(() => StreamLifetime.EnsureDeviceCleanupComplete(path));
        Assert.Equal("native cleanup failed", error.Message);
    }
}
