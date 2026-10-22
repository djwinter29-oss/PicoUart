using System.Diagnostics;
using System.Text.Json;
using PicoUart.Dashboard;
using PicoUart.Wire;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class DashboardTests
{
    [Theory]
    [InlineData(0u, 500)]
    [InlineData(100u, 500)]
    [InlineData(500u, 500)]
    [InlineData(750u, 750)]
    [InlineData(1000u, 1000)]
    [InlineData(5000u, 1000)]
    [InlineData(uint.MaxValue, 500)]
    public void RefreshCadenceUsesOnlyAcceptedInputIntervalsWithinBounds(uint interval, int expected)
    {
        var snapshot = new DashboardSnapshot();
        Assert.Equal(500, snapshot.RefreshIntervalMs);
        snapshot.ApplyFeature(Sample(1) with { IntervalMs = 900 });
        Assert.Equal(500, snapshot.RefreshIntervalMs);
        snapshot.Apply(Sample(1) with { IntervalMs = interval });
        Assert.Equal(expected, snapshot.RefreshIntervalMs);
        snapshot.Apply(Sample(1) with { IntervalMs = 600 });
        Assert.Equal(expected, snapshot.RefreshIntervalMs);
        snapshot.Apply(Sample(2) with { IntervalMs = 600 });
        Assert.Equal(600, snapshot.RefreshIntervalMs);
        snapshot.Apply(Sample(3) with { IntervalMs = 0 });
        snapshot.Apply(Sample(4) with { IntervalMs = uint.MaxValue });
        Assert.Equal(600, snapshot.RefreshIntervalMs);
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(snapshot, Protocol.JsonOptions));
        Assert.Equal(600, json.RootElement.GetProperty("refresh_interval_ms").GetInt32());
    }

    [Fact]
    public void FirstInputOnlyEstablishesContinuityAndFeaturesNeverConsumeDeltas()
    {
        var snapshot = new DashboardSnapshot();
        snapshot.ApplyFeature(Sample(1));
        Assert.Null(snapshot.Channels[0].Sequence);
        Assert.Null(snapshot.Channels[0].UpdatedAt);
        snapshot.Apply(Sample(2));
        Assert.Equal(150000, snapshot.Channels[0].Totals.UartTx);
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
    public void InvalidIntervalsDoNotCreateRateWindows(int field)
    {
        var snapshot = new DashboardSnapshot();
        snapshot.Apply(Sample(1));
        var sample = Sample(2) with { IntervalMs = field == 0 ? 0u : uint.MaxValue };
        snapshot.Apply(sample);
        Assert.True(snapshot.Channels[0].TrafficIncomplete);
        Assert.Equal(1, snapshot.Channels[0].TrafficGaps);
        Assert.Equal(0ul, snapshot.Channels[0].ObservedIntervalMs);
        Assert.Equal(sample.ControllerTxBytes, snapshot.Channels[0].Totals.UartTx);
    }

    [Fact]
    public void DuplicateDoesNotProduceAnExactRateAndWireCounterWrapIsModulo32()
    {
        var snapshot = new DashboardSnapshot();
        snapshot.Apply(Sample(1) with { ControllerTxBytes = uint.MaxValue - 15 });
        snapshot.Apply(Sample(2) with { ControllerTxBytes = 32 });
        Assert.Equal(32, snapshot.Channels[0].Totals.UartTx);
        Assert.Equal(48, snapshot.Channels[0].Traffic.UartTx);
        Assert.Equal(500ul, snapshot.Channels[0].ObservedIntervalMs);
        snapshot.Apply(Sample(2));
        Assert.True(snapshot.Channels[0].TrafficIncomplete);
        Assert.Equal(32, snapshot.Channels[0].Totals.UartTx);
        Assert.Equal(500ul, snapshot.Channels[0].ObservedIntervalMs);
        Assert.Equal((ushort)2, snapshot.Channels[0].Sequence);
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

    [Theory]
    [InlineData(false, 1)]
    [InlineData(false, 40000)]
    [InlineData(true, 1)]
    [InlineData(true, 40000)]
    public void StaleStreamRebaselinesEvenForDuplicateOrOldHalfRangeSequence(bool markStaleFirst, ushort sequence)
    {
        var snapshot = new DashboardSnapshot();
        snapshot.Apply(Sample(1));
        snapshot.Apply(Sample(1, 1));
        snapshot.Channels[0].LastInputTimestamp = Stopwatch.GetTimestamp() - 4 * Stopwatch.Frequency;
        if (markStaleFirst) snapshot.MarkStale();
        snapshot.Apply(Sample(sequence));
        var channel = snapshot.Channels[0];
        Assert.Equal(sequence, channel.Sequence);
        Assert.Equal(150000, channel.Totals.UartTx);
        Assert.Equal(0ul, channel.ObservedIntervalMs);
        Assert.Equal(0, channel.Traffic.UartTx);
        Assert.True(channel.TrafficIncomplete);
        Assert.Equal(1, channel.TrafficGaps);
        Assert.False(snapshot.Channels[1].TrafficIncomplete);
        snapshot.Apply(Sample((ushort)(sequence + 1)));
        Assert.Equal(150000, channel.Totals.UartTx);
        Assert.Equal(500ul, channel.ObservedIntervalMs);
        Assert.Equal(1, channel.TrafficGaps);
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

    [Theory]
    [InlineData(0)]
    [InlineData(2)]
    public void IdleOrLedControlResultEnablesSaveAndClearsPreviousSaveError(byte command)
    {
        var snapshot = new DashboardSnapshot
        {
            ConfigurationSave = new(3, 1),
            ConfigurationSaveError = "previous save rejected",
            ConfigurationError = "previous read failed"
        };
        var configuration = new ConfigurationStatus("defaults", "erased", null, 0);
        snapshot.ApplyControl(new(command, 0, command == 0 ? (byte)0 : (byte)2,
            command == 0 ? 0u : 1u, null, configuration, 0), ProtocolTests.Identity());
        Assert.Equal(new ConfigurationSaveStatus(0, null), snapshot.ConfigurationSave);
        Assert.Null(snapshot.ConfigurationSaveError);
        Assert.Null(snapshot.ConfigurationError);
        Assert.Equal(configuration, snapshot.Configuration);
    }

    [Fact]
    public void SaveCapabilityUnavailabilityIsExplicitAndSaveResultsKeepNewStatus()
    {
        var snapshot = new DashboardSnapshot();
        var control = new ControlResult(5, 1, 6, 1, 1, new("defaults", "erased", null, 0), 8);
        snapshot.ApplyControl(control, ProtocolTests.Identity());
        Assert.Equal(new ConfigurationSaveStatus(6, 1), snapshot.ConfigurationSave);
        Assert.Null(snapshot.ConfigurationSaveError);
        snapshot.ApplyControl(control with { Command = 0, Status = 0, RequestId = 0, RequestedProfile = null },
            ProtocolTests.Identity() with { Capabilities = 3 });
        Assert.Equal(new ConfigurationSaveStatus(0, null), snapshot.ConfigurationSave);
        Assert.Equal("configuration save unavailable", snapshot.ConfigurationSaveError);
    }

    private static ChannelStatus Sample(ushort sequence, int id = 0) =>
        Protocol.ParseChannel((byte)(10 + id), ProtocolTests.ChannelPayload()) with
        {
            Sequence = sequence, IntervalMs = 500, CdcTxBytes = 100, CdcRxBytes = 200
        };
}
