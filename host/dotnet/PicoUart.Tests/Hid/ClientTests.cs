// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using PicoUart.Hid;
using Xunit;

namespace PicoUart.Tests.Hid;

[Trait("Category", "Unit")]
public sealed class ClientTests
{
    [Fact]
    public async Task ReadStatusReturnsWhenClientLockExceedsOperationBudget()
    {
        using var stream = new ScriptedHidStream();
        using var client = stream.CreateClient();
        using var started = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        stream.InputRead = () =>
        {
            started.Set();
            if (!release.Wait(TimeSpan.FromSeconds(3))) throw new TimeoutException("probe was not released");
            throw new TimeoutException("no simulated report");
        };
        var occupyingRead = Task.Run(() => client.ReadStatus());
        try
        {
            Assert.True(started.Wait(TimeSpan.FromSeconds(3)));
            var boundedRead = Task.Run(() => client.ReadStatus(timeoutMs: 250, operationTimeoutMs: 20));
            Assert.Null(await boundedRead.WaitAsync(TimeSpan.FromSeconds(1)));
        }
        finally
        {
            release.Set();
            await occupyingRead;
        }
    }
}
