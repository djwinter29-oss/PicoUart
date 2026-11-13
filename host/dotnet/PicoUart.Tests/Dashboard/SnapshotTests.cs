// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Text.Json;
using PicoUart.Dashboard;
using PicoUart.Protocol;
using Xunit;

namespace PicoUart.Tests.Dashboard;

[Trait("Category", "Unit")]
public sealed class SnapshotTests
{
    [Fact]
    public void StatusKeepsActionResultsSeparateAndDerivesOverflowCountsFromDiagnostics()
    {
        var snapshot = new Snapshot();
        Assert.All(snapshot.OverflowCounts, count => Assert.Null(count));
        snapshot.ChannelDiagnostics[2] = new(2, 1, 0, 0, 17, 0, 0, 0, 0, 0, 0);
        snapshot.ChannelDiagnostics[3] = new(3, 0, byte.MaxValue, 0, 0, 0, 0, 0, 0, 0, 0);
        snapshot.ChannelDiagnostics[4] = new(4, 2, 1, 0, 9, 0, 0, 0, 0, 0, 0);
        Assert.Equal(new uint?[] { null, null, 17, null, 9, null }, snapshot.OverflowCounts);
        snapshot.ApplyAction(new(2, 2, 0));
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(snapshot, Codec.JsonOptions));
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
        var snapshot = new Snapshot();
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
        var snapshot = new Snapshot();
        snapshot.Apply(Sample(sequence: 1, interval: 500, uartTx: 100));
        snapshot.Apply(Sample(sequence: 2, interval: 750, uartTx: 850));
        Assert.True(snapshot.Connected);
        Assert.Equal(750, snapshot.RefreshIntervalMs);
        Assert.Equal(750ul, snapshot.Channels[0].ObservedIntervalMs);
        Assert.Equal(850d, snapshot.Channels[0].Totals.UartTx);
        Assert.Equal(750d, snapshot.Channels[0].Traffic.UartTx);
    }

    [Fact]
    public void PopulatedSnapshotsMatchTheSharedPythonContract()
    {
        using var cases = JsonDocument.Parse(File.ReadAllText(
            Path.Combine(AppContext.BaseDirectory, "Fixtures", "host-input-cases.json")));
        foreach (var testCase in cases.RootElement.EnumerateArray())
        {
            var snapshot = new Snapshot();
            foreach (var source in testCase.GetProperty("samples").EnumerateArray())
                snapshot.Apply(Sample(source.GetProperty("sequence").GetUInt16(),
                    source.GetProperty("interval_ms").GetUInt16(), source.GetProperty("uart_tx").GetUInt32()) with
                {
                    UartRxBytes = source.GetProperty("uart_rx").GetUInt32(),
                    CdcTxBytes = source.GetProperty("usb_tx").GetUInt32(),
                    CdcRxBytes = source.GetProperty("usb_rx").GetUInt32(),
                    UartMeasurementState = source.TryGetProperty("uart_measurement_state", out var state)
                        ? state.GetByte() : (byte)1
                });
            using var actual = JsonDocument.Parse(JsonSerializer.Serialize(snapshot.Channels[0], Codec.JsonOptions));
            foreach (var expected in testCase.GetProperty("expected").EnumerateObject())
                Assert.True(JsonElement.DeepEquals(expected.Value, actual.RootElement.GetProperty(expected.Name)),
                    $"{testCase.GetProperty("name").GetString()}: {expected.Name}");
        }
    }

    [Fact]
    public void InvalidOrGappedInputMarksContinuityIncomplete()
    {
        var snapshot = new Snapshot();
        snapshot.Apply(Sample(1, 500, 100));
        snapshot.Apply(Sample(3, 500, 200));
        snapshot.Apply(Sample(4, ushort.MaxValue, 300));
        Assert.True(snapshot.Channels[0].TrafficIncomplete);
        Assert.True(snapshot.Channels[0].TrafficGaps >= 1);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(ushort.MaxValue)]
    public void InvalidFirstInputIntervalMarksContinuityIncomplete(ushort interval)
    {
        var snapshot = new Snapshot();

        snapshot.Apply(Sample(1, interval, 100));

        Assert.True(snapshot.Channels[0].TrafficIncomplete);
        Assert.Equal(0, snapshot.Channels[0].TrafficGaps);
        Assert.Equal(0ul, snapshot.Channels[0].ObservedIntervalMs);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(ushort.MaxValue)]
    public void InvalidContiguousInputIntervalDoesNotCountAsSequenceGap(ushort interval)
    {
        var snapshot = new Snapshot();
        snapshot.Apply(Sample(1, 500, 100));

        snapshot.Apply(Sample(2, interval, 200));

        Assert.True(snapshot.Channels[0].TrafficIncomplete);
        Assert.Equal(0, snapshot.Channels[0].TrafficGaps);
    }

    [Fact]
    public void DuplicateSequenceDoesNotCreateTrafficGapOrReplaceSample()
    {
        var snapshot = new Snapshot();
        snapshot.Apply(Sample(10, 500, 100));
        var updatedAt = snapshot.Channels[0].UpdatedAt;

        snapshot.Apply(Sample(10, 500, 500));

        Assert.Equal(0, snapshot.Channels[0].TrafficGaps);
        Assert.False(snapshot.Channels[0].TrafficIncomplete);
        Assert.Equal(updatedAt, snapshot.Channels[0].UpdatedAt);
        Assert.Equal(100d, snapshot.Channels[0].Totals.UartTx);
    }

    [Fact]
    public void RepeatedOutOfOrderSamplesCountOneGapUntilSequenceRecovers()
    {
        var snapshot = new Snapshot();
        snapshot.Apply(Sample(10, 500, 100));
        snapshot.Apply(Sample(9, 500, 90));
        snapshot.Apply(Sample(9, 500, 90));

        Assert.Equal(1, snapshot.Channels[0].TrafficGaps);
        Assert.Equal((ushort)10, snapshot.Channels[0].Sequence);

        snapshot.Apply(Sample(11, 500, 200));
        snapshot.Apply(Sample(10, 500, 100));

        Assert.Equal(2, snapshot.Channels[0].TrafficGaps);
        Assert.Equal((ushort)11, snapshot.Channels[0].Sequence);
    }

    [Fact]
    public void ActionAndSaveStatusReflectReport20Result()
    {
        var snapshot = new Snapshot();
        snapshot.ApplyAction(new ActionResult(4, 2, 0));
        Assert.Equal((byte)4, snapshot.Control?.Action);
        Assert.Equal((byte)2, snapshot.ConfigurationSave?.Status);
        Assert.Equal((byte)0, snapshot.ConfigurationSave?.ErrorReason);
    }

    [Fact]
    public void MarkStaleSetsUnknownStateAfterTimeout()
    {
        var snapshot = new Snapshot();
        snapshot.Apply(Sample(1, 500, 100));
        snapshot.Channels[0].LastInputTimestamp -= 4L * System.Diagnostics.Stopwatch.Frequency;
        snapshot.MarkStale();
        var updatedAt = snapshot.Channels[0].UpdatedAt;
        snapshot.ApplyFeature(Sample(2, 500, 200));
        snapshot.ApplyFeature(Sample(3, 500, 300));
        snapshot.MarkStale();
        Assert.Equal("unknown", snapshot.Channels[0].State);
        Assert.Null(snapshot.Channels[0].ChannelState);
        Assert.Null(snapshot.Channels[0].CdcOpen);
        Assert.Null(snapshot.Channels[0].UartMeasurementState);
        Assert.Null(snapshot.Channels[0].BaudRate);
        Assert.True(snapshot.Channels[0].TrafficIncomplete);
        Assert.Equal(1, snapshot.Channels[0].TrafficGaps);
        Assert.Equal((ushort)1, snapshot.Channels[0].Sequence);
        Assert.Equal(updatedAt, snapshot.Channels[0].UpdatedAt);
        Assert.Equal(100d, snapshot.Channels[0].Totals.UartTx);

        snapshot.Apply(Sample(0, 500, 300));
        Assert.Equal("ready", snapshot.Channels[0].State);
        Assert.True(snapshot.Channels[0].CdcOpen);
        Assert.Null(snapshot.Channels[0].Traffic.UartTx);
        snapshot.Apply(Sample(1, 750, 450));
        Assert.Equal(150d, snapshot.Channels[0].Traffic.UartTx);
    }

    private static ChannelStatus Sample(ushort sequence, ushort interval, uint uartTx) =>
        new(0, "ready", 1, 115200, 8, 1, "none", sequence, interval, uartTx, 0, 0, 0);
}
