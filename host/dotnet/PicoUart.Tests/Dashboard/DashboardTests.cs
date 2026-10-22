using System.Diagnostics;
using System.Text.Json;
using PicoUart.Dashboard;
using PicoUart.Wire;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class DashboardTests
{
    [Fact]
    public void FirstInputOnlyEstablishesContinuityAndFeaturesNeverConsumeDeltas()
    {
        var snapshot = new DashboardSnapshot();
        snapshot.ApplyFeature(Sample(1));
        Assert.Null(snapshot.Channels[0].Sequence);
        Assert.Null(snapshot.Channels[0].UpdatedAt);
        snapshot.Apply(Sample(2));
        Assert.Equal(0, snapshot.Channels[0].Totals.UartTx);
        Assert.Equal(0ul, snapshot.Channels[0].ObservedIntervalMs);
        snapshot.ApplyFeature(Sample(2) with { ControllerTxBytes = 99999, IntervalMs = 800 });
        snapshot.Apply(Sample(3) with { IntervalMs = 700 });
        Assert.Equal(150000, snapshot.Channels[0].Totals.UartTx);
        Assert.Equal(700ul, snapshot.Channels[0].ObservedIntervalMs);
        Assert.False(snapshot.Channels[0].TrafficIncomplete);
    }

    [Fact]
    public void IndependentChannelsHandleSequenceWrapAndGaps()
    {
        var snapshot = new DashboardSnapshot();
        snapshot.Apply(Sample(65535));
        snapshot.Apply(Sample(10, 1));
        snapshot.Apply(Sample(0));
        snapshot.Apply(Sample(12, 1));
        Assert.False(snapshot.Channels[0].TrafficIncomplete);
        Assert.True(snapshot.Channels[1].TrafficIncomplete);
        Assert.Equal(1, snapshot.Channels[1].TrafficGaps);
        snapshot.Apply(Sample(13, 1));
        Assert.True(snapshot.Channels[1].TrafficIncomplete);
        Assert.Equal(1, snapshot.Channels[1].TrafficGaps);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(3)]
    [InlineData(4)]
    [InlineData(5)]
    public void InvalidIntervalsOrSaturatedDeltasAreLowerBoundsNotRateWindows(int field)
    {
        var snapshot = new DashboardSnapshot();
        snapshot.Apply(Sample(1));
        var sample = field switch
        {
            0 => Sample(2) with { IntervalMs = 0 },
            1 => Sample(2) with { IntervalMs = uint.MaxValue },
            2 => Sample(2) with { ControllerTxBytes = uint.MaxValue },
            3 => Sample(2) with { ControllerRxBytes = uint.MaxValue },
            4 => Sample(2) with { CdcTxBytes = uint.MaxValue },
            _ => Sample(2) with { CdcRxBytes = uint.MaxValue }
        };
        snapshot.Apply(sample);
        Assert.True(snapshot.Channels[0].TrafficIncomplete);
        Assert.Equal(1, snapshot.Channels[0].TrafficGaps);
        Assert.Equal(0ul, snapshot.Channels[0].ObservedIntervalMs);
        Assert.Equal(sample.ControllerTxBytes, snapshot.Channels[0].Totals.UartTx);
    }

    [Fact]
    public void DuplicateDoesNotProduceAnExactRateAndTotalsUseWideCounters()
    {
        var snapshot = new DashboardSnapshot();
        snapshot.Apply(Sample(1));
        snapshot.Apply(Sample(2) with { ControllerTxBytes = 3000000000 });
        snapshot.Apply(Sample(3) with { ControllerTxBytes = 3000000000 });
        Assert.Equal(6000000000, snapshot.Channels[0].Totals.UartTx);
        Assert.Equal(1000ul, snapshot.Channels[0].ObservedIntervalMs);
        snapshot.Apply(Sample(3));
        Assert.True(snapshot.Channels[0].TrafficIncomplete);
        Assert.Equal(6000000000, snapshot.Channels[0].Totals.UartTx);
        Assert.Equal(1000ul, snapshot.Channels[0].ObservedIntervalMs);
        snapshot.Apply(Sample(2));
        Assert.Equal(6000000000, snapshot.Channels[0].Totals.UartTx);
        Assert.Equal((ushort)3, snapshot.Channels[0].Sequence);
    }

    [Fact]
    public void StaleChannelMarksContinuityUncertainEvenIfSequenceMatches()
    {
        var snapshot = new DashboardSnapshot();
        snapshot.Apply(Sample(1));
        snapshot.Apply(Sample(1, 1));
        snapshot.Channels[0].LastInputTimestamp = Stopwatch.GetTimestamp() - 4 * Stopwatch.Frequency;
        snapshot.MarkStale();
        Assert.Equal("stale", snapshot.Channels[0].State);
        Assert.Equal(1, snapshot.Channels[0].TrafficGaps);
        snapshot.ApplyFeature(Sample(1));
        Assert.Equal((ushort)1, snapshot.Channels[0].Sequence);
        snapshot.MarkStale();
        snapshot.Apply(Sample(2));
        Assert.True(snapshot.Channels[0].TrafficIncomplete);
        Assert.Equal(1, snapshot.Channels[0].TrafficGaps);
        Assert.False(snapshot.Channels[1].TrafficIncomplete);
    }

    [Fact]
    public void JsonExposesRequestedSharedSchemaWithoutInventedFaultTimesOrBoardSequence()
    {
        var snapshot = new DashboardSnapshot { Identity = ProtocolTests.Identity() };
        snapshot.Apply(Sample(1));
        snapshot.Apply(Sample(2));
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(snapshot, Protocol.JsonOptions));
        var root = json.RootElement;
        Assert.False(root.TryGetProperty("sequence", out _));
        Assert.Equal(7, root.GetProperty("identity").GetProperty("capabilities").GetInt32());
        Assert.Equal(31, root.GetProperty("identity").GetProperty("clock_profile_mask").GetInt32());
        var channel = root.GetProperty("channels")[0];
        Assert.Equal(2, channel.GetProperty("sequence").GetInt32());
        Assert.Equal(500ul, channel.GetProperty("observed_interval_ms").GetUInt64());
        Assert.NotEqual(default, channel.GetProperty("updated_at").GetDateTimeOffset());
        Assert.Equal(150000, channel.GetProperty("totals").GetProperty("uart_tx").GetInt64());
        var diag = root.GetProperty("channel_diagnostics")[0];
        Assert.Equal(uint.MaxValue, diag.GetProperty("last_fault_uptime_s").GetUInt32());
        Assert.Equal(new[] { "count" }, diag.GetProperty("faults").GetProperty("rx_error")
            .EnumerateObject().Select(property => property.Name));
    }

    [Fact]
    public void ControlsFailClearlyWithoutAnOpenConnection()
    {
        using var service = new DashboardService(null, null);
        Assert.Throws<InvalidOperationException>(() => service.Action("toggle-led"));
        Assert.Throws<InvalidOperationException>(() => service.Action("reset"));
        Assert.Throws<InvalidOperationException>(() => service.SaveConfiguration(0, false));
    }

    [Fact]
    public void InitialSnapshotHasNoInventedMetadataOrChannelDiagnostics()
    {
        var snapshot = new DashboardSnapshot();
        Assert.Null(snapshot.Identity);
        Assert.Null(snapshot.Board);
        Assert.All(snapshot.ChannelDiagnostics, Assert.Null);
        Assert.All(snapshot.Channels, channel =>
        {
            Assert.Null(channel.UpdatedAt);
            Assert.Null(channel.Sequence);
            Assert.Null(channel.Health);
        });
    }

    private static ChannelStatus Sample(ushort sequence, int id = 0) =>
        Protocol.ParseChannel((byte)(10 + id), ProtocolTests.ChannelPayload()) with
        {
            Sequence = sequence, IntervalMs = 500, CdcTxBytes = 100, CdcRxBytes = 200
        };
}
