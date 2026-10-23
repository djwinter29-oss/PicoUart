using PicoUart.Dashboard;
using PicoUart.Wire;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class DashboardTests
{
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
