// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using PicoUart.Dashboard;
using PicoUart.Wire;
using System.Text.Json;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class DashboardTests
{
    [Fact]
    public void StatusKeepsActionResultsSeparateAndDerivesOverflowCountsFromDiagnostics()
    {
        var snapshot = new DashboardSnapshot();
        Assert.All(snapshot.OverflowCounts, count => Assert.Null(count));
        snapshot.ChannelDiagnostics[2] = new(2, 1, 0, 0, 17, 0, 0, 0, 0, 0, 0);
        snapshot.ApplyAction(new(2, 2, 0));
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(snapshot, Protocol.JsonOptions));
        Assert.False(json.RootElement.TryGetProperty("control", out _));
        Assert.Equal(17u, json.RootElement.GetProperty("overflow_counts")[2].GetUInt32());
        Assert.Equal((byte)2, snapshot.Control?.Action);
    }

    [Theory]
    [InlineData("closed", 0, false)]
    [InlineData("starting", 1, true)]
    [InlineData("ready", 2, true)]
    [InlineData("configuring", 3, true)]
    [InlineData("error", 4, true)]
    public void ChannelStatusExposesSharedLifecycleAndInputFields(string state, byte code, bool open)
    {
        var snapshot = new DashboardSnapshot();
        snapshot.Apply(Sample(12, 750, 100) with { State = state });
        var channel = snapshot.Channels[0];
        Assert.Equal(code, channel.ChannelState);
        Assert.Equal(open, channel.CdcOpen);
        Assert.Equal((ushort)12, channel.Sequence);
        Assert.Equal((ushort)750, channel.IntervalMs);
        snapshot.ApplyFeature(Sample(13, 500, 200) with { BaudRate = 0, DataBits = 0, StopBits = 0, Parity = null });
        Assert.Equal(0u, channel.BaudRate);
        Assert.Null(channel.DataBits);
        Assert.Null(channel.StopBits);
        Assert.Equal((ushort)12, channel.Sequence);
        Assert.Equal((ushort)750, channel.IntervalMs);
    }

    [Fact]
    public void InputSnapshotSetsCadenceAndTraffic()
    {
        var snapshot = new DashboardSnapshot();
        snapshot.Apply(Sample(sequence: 1, interval: 500, uartTx: 100));
        snapshot.Apply(Sample(sequence: 2, interval: 750, uartTx: 850));
        Assert.True(snapshot.Connected);
        Assert.Equal(750, snapshot.RefreshIntervalMs);
        Assert.Equal(750ul, snapshot.Channels[0].ObservedIntervalMs);
        Assert.Equal(850d, snapshot.Channels[0].Totals.UartTx);
        Assert.Equal(1000d, snapshot.Channels[0].Traffic.UartTx);
    }

    [Fact]
    public void InvalidOrGappedInputMarksContinuityIncomplete()
    {
        var snapshot = new DashboardSnapshot();
        snapshot.Apply(Sample(1, 500, 100));
        snapshot.Apply(Sample(3, 500, 200));
        snapshot.Apply(Sample(4, ushort.MaxValue, 300));
        Assert.True(snapshot.Channels[0].TrafficIncomplete);
        Assert.True(snapshot.Channels[0].TrafficGaps >= 1);
    }

    [Fact]
    public void ActionAndSaveStatusReflectReport20Result()
    {
        var snapshot = new DashboardSnapshot();
        snapshot.ApplyAction(new ActionResult(4, 2, 0));
        Assert.Equal((byte)4, snapshot.Control?.Action);
        Assert.Equal((byte)2, snapshot.ConfigurationSave?.Status);
        Assert.Equal((byte)0, snapshot.ConfigurationSave?.ErrorReason);
    }

    [Fact]
    public void MarkStaleSetsUnknownStateAfterTimeout()
    {
        var snapshot = new DashboardSnapshot();
        snapshot.Apply(Sample(1, 500, 100));
        snapshot.Channels[0].LastInputTimestamp -= 4L * System.Diagnostics.Stopwatch.Frequency;
        snapshot.MarkStale();
        Assert.Equal("unknown", snapshot.Channels[0].State);
        Assert.True(snapshot.Channels[0].TrafficIncomplete);
    }

    private static ChannelStatus Sample(ushort sequence, ushort interval, uint uartTx) =>
        new(0, "ready", 1, 115200, 8, 1, "none", sequence, interval, uartTx, 0, 0, 0);
}
