// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Reflection;
using System.Runtime.CompilerServices;
using PicoUart.Hid;
using PicoUart.Wire;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class PicoUartHidTests
{
    [Fact]
    public async Task ReadStatusReturnsWhenClientLockExceedsOperationBudget()
    {
        var client = (PicoUartHid)RuntimeHelpers.GetUninitializedObject(typeof(PicoUartHid));
        var gate = new object();
        typeof(PicoUartHid).GetField("gate", BindingFlags.Instance | BindingFlags.NonPublic)!
            .SetValue(client, gate);
        using var attempted = new ManualResetEventSlim();
        Task<ChannelStatus?> read;
        lock (gate)
        {
            read = Task.Factory.StartNew(() =>
            {
                attempted.Set();
                return client.ReadStatus(timeoutMs: 250, operationTimeoutMs: 20);
            }, CancellationToken.None, TaskCreationOptions.LongRunning, TaskScheduler.Default);
            Assert.True(attempted.Wait(TimeSpan.FromSeconds(1)));
            Thread.Sleep(100);
            Assert.True(read.IsCompleted);
        }

        Assert.Null(await read);
    }
}
